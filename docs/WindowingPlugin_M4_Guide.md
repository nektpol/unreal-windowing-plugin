# M4 implementation guide — Windows release packaging

M4 produces a versioned Win64 plugin ZIP, a separate sample project ZIP, and a recorded release check. The release uses the M0–M3 API for window lifecycle, placement, and focus. Use UE 5.8.4 for the first binary package and record the engine changelist alongside the release. Build and test again before claiming compatibility with another engine build.

## 1. Prepare the release descriptor and file filter

In the root `WindowingPlugin.uplugin`, update these entries in the existing JSON. Keep the existing module declaration and other fields. `Version` increases from the existing value of 1.

```json
"Version": 2,
"VersionName": "1.0.0",
"IsBetaVersion": false,
"SupportedTargetPlatforms": ["Win64"],
```

Within the existing `WindowingPlugin` module object, add this field after `LoadingPhase`. Put a comma after the preceding field.

```json
"PlatformAllowList": ["Win64"]
```

Create `Config/FilterPlugin.ini` in the plugin root with the following contents. The source sample is shipped as a separate ZIP.

```ini
[FilterPlugin]
/LICENSE
/README.md
/docs/...
```

Keep the plugin README's installation and Blueprint API summary. Include the MIT license in both archives. Record the tested engine patch and changelist, supported platform, and any outstanding checks in the release notes before publishing.

## 2. Add the release build script

Create `Tests/Build-WindowsRelease.ps1` with the following contents. Run it from a PowerShell session with the UE 5.8 Windows toolchain installed. The output directory must be new because Unreal's `BuildPlugin` can clear an existing package destination. The script stages its input outside the plugin checkout, so UAT can write its temporary build files there.

```powershell
[CmdletBinding()]
param(
    [string] $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string] $OutputRoot = (Join-Path ([IO.Path]::GetTempPath()) ('WpRel-' + [guid]::NewGuid().ToString('N').Substring(0, 8)))
)

$ErrorActionPreference = 'Stop'
$OutputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputRoot)
$plugin = Split-Path -Parent $PSScriptRoot
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (!(Test-Path -LiteralPath $uat -PathType Leaf)) {
    throw "RunUAT.bat not found at $uat"
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw "Use a new output directory: $OutputRoot"
}

$descriptor = Join-Path $plugin 'WindowingPlugin.uplugin'
$version = (Get-Content -LiteralPath $descriptor -Raw | ConvertFrom-Json).VersionName
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Set VersionName to a release version such as 1.0.0.'
}

$inputPlugin = Join-Path $OutputRoot 'Input\WindowingPlugin'
$package = Join-Path $OutputRoot 'Package\WindowingPlugin'
New-Item -ItemType Directory -Path $inputPlugin -Force | Out-Null
foreach ($name in @('WindowingPlugin.uplugin', 'Config', 'Source', 'Resources', 'LICENSE', 'README.md', 'docs')) {
    Copy-Item -LiteralPath (Join-Path $plugin $name) -Destination $inputPlugin -Recurse
}

$log = Join-Path $OutputRoot 'BuildPlugin.log'
$inputDescriptor = Join-Path $inputPlugin 'WindowingPlugin.uplugin'
& $uat BuildPlugin "-Plugin=$inputDescriptor" "-Package=$package" -HostPlatforms=Win64 -TargetPlatforms=Win64 -unattended 2>&1 |
    Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) {
    throw "BuildPlugin failed. See $log"
}

foreach ($name in @('WindowingPlugin.uplugin', 'Binaries\Win64', 'Source', 'LICENSE', 'README.md', 'docs')) {
    if (!(Test-Path -LiteralPath (Join-Path $package $name))) {
        throw "The plugin package is missing $name."
    }
}

$pluginZip = Join-Path $OutputRoot "WindowingPlugin-$version-Win64.zip"
Compress-Archive -LiteralPath $package -DestinationPath $pluginZip

$sample = Join-Path $OutputRoot 'WindowingPluginSample'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Sample') -Destination $sample -Recurse
Copy-Item -LiteralPath (Join-Path $plugin 'LICENSE') -Destination $sample
@(
    'Extract the Win64 plugin ZIP.'
    'Place its WindowingPlugin folder under this sample project''s Plugins directory.'
    'Open WindowingM1.uproject in Unreal Engine 5.8 and build when prompted.'
    'The sample is a C++ acceptance harness. The plugin README links to the interactive Blueprint sample instructions.'
) | Set-Content -LiteralPath (Join-Path $sample 'README.txt')
$sampleZip = Join-Path $OutputRoot "WindowingPlugin-$version-Sample.zip"
Compress-Archive -LiteralPath $sample -DestinationPath $sampleZip

Get-FileHash -Algorithm SHA256 -LiteralPath $pluginZip, $sampleZip |
    ForEach-Object { '{0}  {1}' -f $_.Hash, [IO.Path]::GetFileName($_.Path) } |
    Set-Content -LiteralPath (Join-Path $OutputRoot 'SHA256SUMS.txt')
Write-Host "Release candidates and checksums: $OutputRoot"
```

The plugin ZIP contains a single top-level `WindowingPlugin` folder. The sample ZIP contains the existing C++ harness project. Install the plugin ZIP into the sample before opening its project. Keep the build log with the local verification results.

## 3. Let the acceptance runner use an extracted release ZIP

In `Tests/Run-M1Acceptance.ps1`, add a comma after the existing `$OutputRoot` parameter and add this parameter before the closing `)`.

```powershell
[string] $PluginPackage
```

Find the existing `$pluginCopy = Join-Path $sample 'Plugins\WindowingPlugin'` line. Replace it, the following `New-Item` line, and the following `foreach` loop with the block below. Leave `$project = Join-Path $sample 'WindowingM1.uproject'` and the rest of the runner in place.

```powershell
$pluginsDirectory = Join-Path $sample 'Plugins'
$pluginCopy = Join-Path $pluginsDirectory 'WindowingPlugin'
New-Item -ItemType Directory -Path $pluginsDirectory -Force | Out-Null
if ($PluginPackage) {
    $candidate = (Resolve-Path -LiteralPath $PluginPackage).Path
    if (!(Test-Path -LiteralPath (Join-Path $candidate 'WindowingPlugin.uplugin') -PathType Leaf)) {
        throw 'PluginPackage must point to the extracted WindowingPlugin directory.'
    }
    Copy-Item -LiteralPath $candidate -Destination $pluginCopy -Recurse
} else {
    New-Item -ItemType Directory -Path $pluginCopy | Out-Null
    foreach ($name in @('WindowingPlugin.uplugin', 'Source', 'Resources')) {
        Copy-Item -LiteralPath (Join-Path $pluginRoot $name) -Destination $pluginCopy -Recurse
    }
}
```

The runner still supports its existing source-based use. For release testing, pass the extracted package. Unreal may recompile package source for the sample target during packaging, so include the clean editor install in the final check to exercise the supplied editor binaries.

## 4. Final release acceptance test

A disposable UE 5.8.4 preflight produced both ZIPs, verified their checksums, and loaded the extracted editor DLL in a clean Blueprint-only host. The M2 geometry and storage tests passed in that host. The packaged runtime scenarios and live release checks below remain to be run for the release candidate.

From the plugin repository root, run the following. It builds a new candidate, extracts the plugin ZIP, and runs the existing lifecycle, shutdown, and registration rollback scenarios against that extracted copy in both packaged configurations.

```powershell
$ErrorActionPreference = 'Stop'
$release = Join-Path ([IO.Path]::GetTempPath()) ('WpRel-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
& .\Tests\Build-WindowsRelease.ps1 -OutputRoot $release

$checksums = @(Get-Content -LiteralPath (Join-Path $release 'SHA256SUMS.txt'))
if ($checksums.Count -ne 2) {
    throw 'Expected one checksum per ZIP.'
}
foreach ($line in $checksums) {
    if ($line -notmatch '^([0-9A-Fa-f]{64})  (WindowingPlugin-[0-9.]+-(Win64|Sample)\.zip)$') {
        throw "Invalid checksum line: $line"
    }
    $expected = $Matches[1]
    $file = Join-Path $release $Matches[2]
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash -ne $expected) {
        throw "Checksum mismatch: $file"
    }
}

$zip = Get-ChildItem -LiteralPath $release -Filter 'WindowingPlugin-*-Win64.zip'
if (@($zip).Count -ne 1) {
    throw 'Expected exactly one Win64 plugin ZIP.'
}
$install = Join-Path $release 'ExtractedPlugin'
Expand-Archive -LiteralPath $zip.FullName -DestinationPath $install
$sampleZip = Get-ChildItem -LiteralPath $release -Filter 'WindowingPlugin-*-Sample.zip'
if (@($sampleZip).Count -ne 1) {
    throw 'Expected exactly one sample ZIP.'
}
$sampleInstall = Join-Path $release 'ExtractedSample'
Expand-Archive -LiteralPath $sampleZip.FullName -DestinationPath $sampleInstall
if (!(Test-Path -LiteralPath (Join-Path $sampleInstall 'WindowingPluginSample\WindowingM1.uproject'))) {
    throw 'The extracted sample is missing WindowingM1.uproject.'
}
& .\Tests\Run-M1Acceptance.ps1 -PluginPackage (Join-Path $install 'WindowingPlugin') `
    -OutputRoot (Join-Path $release 'Acceptance')
```

Finish with the live checks below. For the binary install check, put the extracted plugin in a fresh Blueprint project and open it with the same UE build that produced the candidate. Confirm it can open with the supplied module before running the interactive checks in a disposable copy of the Blueprint sample.

| Test | Pass condition |
|---|---|
| Clean install | The plugin is enabled in the new project. The supplied editor module loads, and the Blueprint window nodes and focus delegate are available. |
| Packaged lifecycle | Development and Shipping each report PASS for all three existing scenarios. The lifecycle case reports 100 cycles, 104 opens, and 104 closes. |
| Live M2 | Move and resize a keyed window, close it, restart Standalone, and reopen it. Normal bounds return. Repeat with a maximized window and check that Restore Down returns to the saved normal bounds. Run the packaged placement and display recovery checks before claiming those results. [M2 live check](WindowingPlugin_M2_Guide.md#check-a-real-window-in-the-editor) |
| Live M3 | Switch between two managed windows, the game window, and another application. Verify the active handle, keyboard focus while typing, and loss of focus after closing the active window. [M3 live check](WindowingPlugin_M3_Guide.md#3-test-with-real-windows-in-unreal-editor) |
| Distribution files | Extract both ZIPs independently. Confirm the plugin folder, sample project, documentation, and MIT license. Verify the ZIPs against `SHA256SUMS.txt`. |

Record the plugin commit, UE version and changelist, scenario reports, and any display or DPI setup used for the live checks. Publish the ZIPs and checksums after the checks pass.
