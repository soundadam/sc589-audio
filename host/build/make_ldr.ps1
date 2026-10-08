<#
.SYNOPSIS
  Build the SC589 audio_loopback loader streams (.ldr) from already-built Debug executables.

.DESCRIPTION
  Combines Core0 (ARM), Core1 and Core2 (SHARC) with the EZ-Board initcode
  that CCES ships in SHARC\ldr, which sets up CGU/DDR the way the emulator
  preload does in a debug session (ADR 0001). Only Core2 ends in a FINAL
  block, since the Boot ROM stops reading the stream at the first one.
  Writes one .ldr per boot mode:
    SPIMASTER  - written to flash with CLDP, BMODE=1 (B1)
    UARTSLAVE  - sent over UART0 by the host (B2)

  Build the three cores first, e.g. with the CCES headless builder:
    ccesc.exe -nosplash -consoleLog -application com.analog.crosscore.headlesstools `
      -data <workspace> -project firmware\audio_loopback\cces\SC589\sharc\adc_dac_playback_Core1 -build Debug

.EXAMPLE
  .\host\build\make_ldr.ps1
#>
param(
    [string]$Config = "Debug",
    [string]$Cces = "C:\Analog Devices\CrossCore Embedded Studio 2.9.3",
    # Initcode executable; default is the CCES SC589 EZ-Board initcode
    [string]$InitFile,
    [string]$OutDir
)

$ErrorActionPreference = "Stop"
$Board = "SC589"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$proj = Join-Path $repo "firmware\audio_loopback\cces\$Board\sharc"
if (-not $OutDir) { $OutDir = Join-Path $proj $Config }
New-Item -ItemType Directory -Force $OutDir | Out-Null

$name = "ADC_DAC_Playback_${Board}_SHARC"
# elfloader wants forward slashes in -coreN= paths
$core0 = (Join-Path $proj "adc_dac_playback_Core0\$Config\${name}_Core0") -replace "\\", "/"
$core1 = (Join-Path $proj "adc_dac_playback_Core1\$Config\${name}_Core1.dxe") -replace "\\", "/"
$core2 = (Join-Path $proj "adc_dac_playback_Core2\$Config\${name}_Core2.dxe") -replace "\\", "/"
$init = if ($InitFile) { $InitFile } else { Join-Path $Cces "SHARC\ldr\ezkitSC589_initcode_core0_v10" }

foreach ($f in $core0, $core1, $core2, $init) {
    if (-not (Test-Path $f)) { throw "missing $f; build the three cores first" }
}

foreach ($mode in "SPIMASTER", "UARTSLAVE") {
    # BCODE 1 and width 8 are the loader defaults the POST projects boot with;
    # BCODE only exists in SPI master mode
    $extra = if ($mode -eq "SPIMASTER") { @("-bcode", "1") } else { @() }
    $out = Join-Path $OutDir ("adc_dac_playback_{0}_{1}.ldr" -f $Board, $mode.ToLower())
    & (Join-Path $Cces "elfloader.exe") -proc "ADSP-$Board" -si-revision any -b $mode -f BINARY -width 8 @extra `
        -init $init "-core0=$core0" "-core1=$core1" "-core2=$core2" `
        "-NoFinalTag=$core0" "-NoFinalTag=$core1" -o $out
    if ($LASTEXITCODE -ne 0) { throw "elfloader failed for $mode ($LASTEXITCODE)" }
    "{0}  {1} bytes" -f $out, (Get-Item $out).Length
}
