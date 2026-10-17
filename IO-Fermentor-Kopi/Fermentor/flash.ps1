<#
.SYNOPSIS
Build and flash the active Fermentor Kopi Rust firmware to an ESP32-S3.
.EXAMPLE
.\flash.ps1 -Port COM9
.EXAMPLE
.\flash.ps1 -Port COM9 -Monitor -Offline
.EXAMPLE
.\flash.ps1 -BuildOnly -Offline
.EXAMPLE
.\flash.ps1 -Port COM9 -WhatIf
.NOTES
Disconnect dashboard USB and close terminal monitors before flashing.
Flashing resets the board: automatic mixing may start after 30 seconds
when the Nextion interlock is ready. Make the machine physically safe first.
The default 16 MB matches the last detected board; use -FlashSize 8mb only
for hardware with 8 MB flash. No monitor opens unless -Monitor is requested.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')]
    [string]$Port,
    [switch]$Monitor,
    [switch]$Offline,
    [switch]$BuildOnly,
    [ValidateSet('8mb', '16mb')]
    [string]$FlashSize = '16mb'
)

$ErrorActionPreference = 'Stop'
if ($BuildOnly -and $Monitor) {
    throw '-Monitor cannot be used with -BuildOnly.'
}
if (-not $BuildOnly -and -not $Port) {
    throw 'Specify the ESP32 port, for example: .\flash.ps1 -Port COM9. For build only, use -BuildOnly.'
}

$firmwareTarget = 'xtensa-esp32s3-none-elf'
$firmwareTargetDir = Join-Path $PSScriptRoot 'target'
$firmwareElf = Join-Path $firmwareTargetDir "$firmwareTarget\release\fermentor_kopi"
$cargoArgs = @('build', '--release', '--locked', '--target', $firmwareTarget, '--target-dir', $firmwareTargetDir)
if ($Offline) { $cargoArgs += '--offline' }
$flashArgs = @('-S', 'flash', '--chip', 'esp32s3', '--port', $Port,
    '--non-interactive', '--flash-mode', 'dio', '--flash-freq', '80mhz', '--flash-size', $FlashSize)
if ($Monitor) { $flashArgs += @('--monitor', '--monitor-baud', '115200') }
$flashArgs += $firmwareElf

Write-Host "Firmware: $firmwareElf"
Write-Host ('cargo ' + ($cargoArgs -join ' '))
if (-not $BuildOnly) {
    Write-Host ('espflash ' + ($flashArgs -join ' '))
    Write-Warning 'Disconnect dashboard USB / close serial monitors. Flashing resets the mixer schedule; motion may start after 30 seconds. Make the machine safe first.'
}
$operation = if ($BuildOnly) { 'Build release firmware' } else { "Build and flash ESP32-S3 on $Port (resets board)" }
if (-not $PSCmdlet.ShouldProcess($PSScriptRoot, $operation)) { return }

$cargoCommand = Get-Command cargo -ErrorAction SilentlyContinue
if (-not $cargoCommand) { throw 'cargo not found. Install/load the ESP Rust toolchain and reopen PowerShell.' }
if (-not $BuildOnly) {
    $flashCommand = Get-Command espflash -ErrorAction SilentlyContinue
    if (-not $flashCommand) { throw 'espflash not found. Install it with: cargo install espflash --locked' }
}

Push-Location -LiteralPath $PSScriptRoot
try {
    & $cargoCommand @cargoArgs
    if ($LASTEXITCODE -ne 0) { throw "Firmware build failed (exit $LASTEXITCODE). Nothing was flashed." }
    if (-not (Test-Path -LiteralPath $firmwareElf -PathType Leaf)) {
        throw "Build did not produce $firmwareElf. Nothing was flashed."
    }
    if ($BuildOnly) {
        Write-Host "Build complete: $firmwareElf"
        return
    }
    & $flashCommand @flashArgs
    if ($LASTEXITCODE -ne 0) {
        throw "espflash failed (exit $LASTEXITCODE). Check the COM port, USB cable, and close any app holding USB before retrying."
    }
    Write-Host 'Flash command completed. Reconnect USB in the dashboard to read live data.'
}
finally {
    Pop-Location
}
