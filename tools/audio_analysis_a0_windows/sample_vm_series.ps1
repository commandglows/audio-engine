param([Parameter(Mandatory=$true)][string]$LogPath,
      [Parameter(Mandatory=$true)][string]$OutputPath,
      [Parameter(Mandatory=$true)][int]$ProbeProcessId,
      [int]$MaximumSamples=30,
      [int]$IntervalSeconds=45)
$ErrorActionPreference='Stop'
if($MaximumSamples -lt 1 -or $MaximumSamples -gt 40 -or $IntervalSeconds -lt 10 -or $IntervalSeconds -gt 60){throw 'Invalid bounded sampling schedule'}
$probe=Get-Process -Id $ProbeProcessId -ErrorAction Stop
if($probe.ProcessName -ne 'audio_analysis_a0'){throw 'Unexpected probe process'}
for($index=0;$index -lt $MaximumSamples;$index++){
  # Hold the original process handle; never attach to a reused PID or restart.
  if($probe.HasExited){'Original probe exited; sampling ended';exit 0}
  & "$PSScriptRoot/sample_vm_memory.ps1" -LogPath $LogPath -OutputPath $OutputPath
  $content=Get-Content -LiteralPath $LogPath -Raw
  if(([regex]::Matches($content,'A0_RESULT ')).Count -ge 2){'Both phase results observed; sampling ended';exit 0}
  if($content -match 'A0_ERROR|Lost connection to device'){'Terminal diagnostic output observed; sampling ended';exit 1}
  Start-Sleep -Seconds $IntervalSeconds
}
'Bounded sample schedule exhausted; inspect original run separately'
