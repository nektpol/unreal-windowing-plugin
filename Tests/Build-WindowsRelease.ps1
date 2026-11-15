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
foreach ($name in @('WindowingPlugin.uplugin', 'Config', 'Source', 'Resources', 'LICENSE', 'README.md')) {
    Copy-Item -LiteralPath (Join-Path $plugin $name) -Destination $inputPlugin -Recurse
}
$publicDocs = Join-Path $inputPlugin 'docs'
New-Item -ItemType Directory -Path $publicDocs | Out-Null
Copy-Item -LiteralPath (Join-Path $plugin 'docs\Usage.md') -Destination $publicDocs

$log = Join-Path $OutputRoot 'BuildPlugin.log'
$inputDescriptor = Join-Path $inputPlugin 'WindowingPlugin.uplugin'
& $uat BuildPlugin "-Plugin=$inputDescriptor" "-Package=$package" -HostPlatforms=Win64 -TargetPlatforms=Win64 -unattended 2>&1 |
    Tee-Object -FilePath $log
if ($LASTEXITCODE -ne 0) {
    throw "BuildPlugin failed. See $log"
}

foreach ($name in @('WindowingPlugin.uplugin', 'Binaries\Win64\UnrealEditor-WindowingPlugin.dll', 'Source', 'LICENSE', 'README.md', 'docs\Usage.md')) {
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
    'The sample is a C++ acceptance harness. The plugin README links to the public usage guide.'
) | Set-Content -LiteralPath (Join-Path $sample 'README.txt')
$sampleZip = Join-Path $OutputRoot "WindowingPlugin-$version-Sample.zip"
Compress-Archive -LiteralPath $sample -DestinationPath $sampleZip

Get-FileHash -Algorithm SHA256 -LiteralPath $pluginZip, $sampleZip |
    ForEach-Object { '{0}  {1}' -f $_.Hash, [IO.Path]::GetFileName($_.Path) } |
    Set-Content -LiteralPath (Join-Path $OutputRoot 'SHA256SUMS.txt')
Write-Host "Release candidates and checksums: $OutputRoot"
