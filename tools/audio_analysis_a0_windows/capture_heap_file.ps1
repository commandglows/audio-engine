param([ValidateRange(10,120)][int]$Seconds = 60)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from Administrator PowerShell.'
}
$engineRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$evidenceDir = Join-Path $engineRoot 'shipglows_data/workflow/evidence/audio-analysis-a0/2026-10-07'
$probe = Get-Process -Name audio_analysis_a0 -ErrorAction Stop |
    Where-Object { $_.Path -like '*\beatglows\spikes\audio_analysis_a0\build\windows\x64\runner\Profile\audio_analysis_a0.exe' } |
    Sort-Object StartTime -Descending | Select-Object -First 1
if (-not $probe) { throw 'Private A0 Profile diagnostic not found.' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$profileFile = Join-Path $evidenceDir "A0Heap-file-$stamp-$($probe.Id).wprp"
$traceFile = Join-Path $evidenceDir "A0Heap-file-$stamp-$($probe.Id).etl"
& (Join-Path $PSScriptRoot 'new_heap_profile.ps1') -ProbeProcessId $probe.Id -OutputPath $profileFile
wpr.exe -start "${profileFile}!A0Heap.Verbose" -filemode
if ($LASTEXITCODE -ne 0) { throw "WPR start failed: $LASTEXITCODE. No existing recording was stopped." }
try {
    Write-Host "Capturing PID $($probe.Id) to disk for $Seconds seconds."
    Start-Sleep -Seconds $Seconds
} finally {
    wpr.exe -stop $traceFile "A0 file-mode heap PID $($probe.Id)"
    if ($LASTEXITCODE -ne 0) { throw "WPR stop failed: $LASTEXITCODE" }
}
Get-Item -LiteralPath $traceFile | Select-Object FullName, Length
