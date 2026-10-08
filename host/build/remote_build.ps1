<#
.SYNOPSIS
  Build the three SC589 audio_loopback cores with the CCES headless builder,
  then pack the loader streams with make_ldr.ps1.

.DESCRIPTION
  Runs on the Windows build host, from the clone that host/build/remote_build.sh
  has just checked out. The headless builder gets its own workspace next to
  the clone, so it does not share a workspace lock with an open CCES IDE.

  The workspace is recreated on every run and the add-ins regenerate their
  sources (-regensrc): the SRU configuration and the startup code and LDF
  are not in git but generated from system.svc, and a reused workspace
  keeps a cached resource tree that can drop the linked sources.

  Each core's executable is deleted before its build and the script stops
  on a failed core, so a failed compile cannot pass with a stale .dxe.

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File host\build\remote_build.ps1
#>
param(
    [string]$Config = "Debug",
    [string]$Cces = "C:\Analog Devices\CrossCore Embedded Studio 2.9.3",
    [string]$Workspace
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Workspace) { $Workspace = Join-Path (Split-Path -Parent $repo) "cces-headless" }
$proj = Join-Path $repo "firmware\audio_loopback\cces\SC589\sharc"
$ccesc = Join-Path $Cces "Eclipse\ccesc.exe"
if (Test-Path $Workspace) { Remove-Item -Recurse -Force $Workspace }

foreach ($core in 0, 1, 2) {
    $dir = Join-Path $proj "adc_dac_playback_Core$core"
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $dir "$Config\ADC_DAC_Playback_SC589_SHARC_Core$core*")
    "== Core$core"
    # Piping makes PowerShell wait for ccesc and shows its console log
    & $ccesc -nosplash -consoleLog -application com.analog.crosscore.headlesstools `
        -data $Workspace -project $dir -regensrc -build $Config 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "Core$core build failed (ccesc exit $LASTEXITCODE)" }
}

& (Join-Path $PSScriptRoot "make_ldr.ps1") -Config $Config -Cces $Cces
