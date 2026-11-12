# M4 implementation guide — Windows release packaging

M4 produces a versioned Win64 plugin ZIP, a separate sample project ZIP, and a recorded release check. The release uses the M0–M3 API for window lifecycle, placement, and focus. Use UE 5.8.4 for the first binary package and record the engine changelist alongside the release. Build and test again before claiming compatibility with another engine build.

## 1. Review the release inputs

The plugin descriptor declares version 1.0.0 and Win64 support. Review the version before the next release. The package filter includes the README, documentation, and MIT license. Keep the sample project in its separate ZIP.

## 2. Build from PowerShell

From the plugin repository root, run the release script. It stages the source in a new directory, builds the Win64 package with Unreal Automation Tool, and prints the directory containing the plugin ZIP, sample ZIP, and SHA-256 checksums.

```powershell
.\Tests\Build-WindowsRelease.ps1
```

Pass `-EngineRoot` if UE 5.8 is installed elsewhere. Pass `-OutputRoot` to choose a new output directory. Keep `BuildPlugin.log` with the local verification results. [Release script](../Tests/Build-WindowsRelease.ps1)

## 3. Test the extracted package

The existing acceptance runner takes `-PluginPackage` with the path to an extracted `WindowingPlugin` folder. Without that parameter, it copies the plugin source from the checkout. Unreal may recompile package source for the sample game target. The clean Blueprint project check exercises the supplied editor DLL. [Acceptance runner](../Tests/Run-M1Acceptance.ps1)

## 4. Final release acceptance test

The checked-in release script built the Win64 editor, Development, and Shipping targets and produced both 1.0.0 candidate ZIPs. Both checksums and archive entry points were verified. A separate UE 5.8.4 preflight loaded the extracted editor DLL in a clean Blueprint-only host, where the M2 geometry and storage tests passed. The final packaged runtime scenarios and live release checks below are still pending.

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
