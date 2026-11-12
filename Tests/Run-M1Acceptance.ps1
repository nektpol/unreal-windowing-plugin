[CmdletBinding()]
param(
    [string] $EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateSet('Development', 'Shipping')]
    [string[]] $Configuration = @('Development', 'Shipping'),
    [string] $OutputRoot = (Join-Path ([IO.Path]::GetTempPath()) ('WindowingM1-' + [guid]::NewGuid().ToString('N')))
)

$ErrorActionPreference = 'Stop'
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (!(Test-Path -LiteralPath $uat -PathType Leaf)) {
    throw "RunUAT.bat not found at $uat"
}
if (Test-Path -LiteralPath $OutputRoot) {
    throw "Use a new output directory. Path already exists at $OutputRoot"
}

$pluginRoot = Split-Path -Parent $PSScriptRoot
$sample = Join-Path $OutputRoot 'Sample'
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Sample') -Destination $sample -Recurse
$pluginCopy = Join-Path $sample 'Plugins\WindowingPlugin'
New-Item -ItemType Directory -Path $pluginCopy -Force | Out-Null
foreach ($name in @('WindowingPlugin.uplugin', 'Source', 'Resources')) {
    Copy-Item -LiteralPath (Join-Path $pluginRoot $name) -Destination $pluginCopy -Recurse
}

$project = Join-Path $sample 'WindowingM1.uproject'
Write-Host "M1 output and reports: $OutputRoot"

foreach ($config in $Configuration) {
    $archive = Join-Path $OutputRoot $config
    $buildLog = Join-Path $OutputRoot "$config-package.log"
    $uatArgs = @(
        'BuildCookRun', "-project=$project", '-platform=Win64', "-clientconfig=$config",
        '-build', '-cook', '-map=/Engine/Maps/Entry', '-stage', '-pak', '-package', '-archive',
        "-archivedirectory=$archive", '-nop4', '-unattended', '-utf8output'
    )
    & $uat @uatArgs 2>&1 | Tee-Object -FilePath $buildLog
    if ($LASTEXITCODE -ne 0) {
        throw "$config packaging failed. See $buildLog"
    }

    # Launch the game binary directly so the exit code comes from the tested process.
    $exe = if ($config -eq 'Shipping') { 'WindowingM1-Win64-Shipping.exe' } else { 'WindowingM1.exe' }
    $game = Join-Path $archive "Windows\WindowingM1\Binaries\Win64\$exe"
    if (!(Test-Path -LiteralPath $game -PathType Leaf)) {
        throw "Packaged game executable not found at $game"
    }

    foreach ($scenario in @('lifecycle', 'shutdown', 'registration-rollback')) {
        $report = Join-Path $OutputRoot "$config-$scenario.txt"
        $log = Join-Path $OutputRoot "$config-$scenario.log"
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $game
        $start.WorkingDirectory = Split-Path -Parent $game
        $start.UseShellExecute = $false
        $start.Arguments = "-M1Scenario=$scenario -M1Report=`"$report`" -abslog=`"$log`" -unattended -NoCrashDialog -nosplash -nosound -windowed -ResX=960 -ResY=640"
        if ($scenario -eq 'registration-rollback') {
            $start.Arguments += ' -RenderOffScreen'
        }
        $process = [Diagnostics.Process]::Start($start)
        try {
            if (!$process.WaitForExit(120000)) {
                $process.Kill()
                $process.WaitForExit()
                throw "$config $scenario timed out. See $log"
            }
            $lines = if (Test-Path -LiteralPath $report) { @(Get-Content -LiteralPath $report) } else { @() }
            if ($process.ExitCode -ne 0 -or $lines.Count -eq 0 -or $lines[0] -ne 'PASS') {
                throw "$config $scenario failed with exit code $($process.ExitCode). $($lines -join ' ') See $log"
            }
            Write-Host "$config $scenario PASS ($($lines[-1]))"
        }
        finally {
            $process.Dispose()
        }
    }
}
