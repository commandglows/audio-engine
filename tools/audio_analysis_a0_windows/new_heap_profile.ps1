param([Parameter(Mandatory=$true)][int]$ProbeProcessId,
      [Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$probe=Get-Process -Id $ProbeProcessId -ErrorAction Stop
if($probe.ProcessName -ne 'audio_analysis_a0'){throw 'Heap target must be the private A0 diagnostic'}
if(Test-Path -LiteralPath $OutputPath){throw 'Refusing to overwrite an existing trace profile'}
$template=Get-Content -LiteralPath "$PSScriptRoot/heap_profile.wprp.template" -Raw
$template.Replace('__PROBE_PID__',[string]$ProbeProcessId) | Set-Content -LiteralPath $OutputPath -Encoding utf8
# Generation only: no trace, elevation, registry configuration or process start.
"Heap profile generated for diagnostic PID $ProbeProcessId; 8 MiB nominal collector buffers"
# WPR CLI selects Name plus DetailLevel; LoggingMode is carried by the XML ID.
'Start with: wpr.exe -start "{0}!A0Heap.Verbose"' -f $OutputPath
