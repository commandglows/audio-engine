param([Parameter(Mandatory=$true)][string]$LogPath,
      [Parameter(Mandatory=$true)][string]$OutputPath,
      [int]$TraceSeconds=15)
$ErrorActionPreference='Stop'
if($TraceSeconds -lt 1 -or $TraceSeconds -gt 30){throw 'Trace duration must be 1..30 seconds'}
$content=Get-Content -LiteralPath $LogPath -Raw
$match=[regex]::Match($content,'A Dart VM Service on Windows is available at: (http://127\.0\.0\.1:[0-9]+/[^\s]+/)')
if(-not $match.Success){throw 'Original VM service not ready'}
$base=$match.Groups[1].Value
function Invoke-ProbeRpc([string]$Method,[hashtable]$Parameters=@{}){
  $query=@($Parameters.GetEnumerator() | ForEach-Object { [uri]::EscapeDataString($_.Key)+'='+[uri]::EscapeDataString([string]$_.Value) }) -join '&'
  $reply=Invoke-RestMethod -Uri ($base+$Method+'?'+$query) -TimeoutSec 5
  if($reply.error){throw ($Method+' RPC '+$reply.error.code+': '+$reply.error.message)}
  return $reply.result
}
$report=[ordered]@{kind='a0_targeted_dart_allocation_trace';utc=(Get-Date).ToUniversalTime().ToString('o');trace_seconds=$TraceSeconds;classes=@();errors=@();restored=@();
  limits='Separate diagnostic run; tracing changes overhead. Target classes only, no total bytes or exhaustive allocation throughput claim. Empty traces are inconclusive.'}
$enabled=@()
try{
 $vm=Invoke-ProbeRpc 'getVM'
 $main=$vm.isolates | Where-Object name -eq main | Select-Object -First 1
 if(-not $main){throw 'Main isolate unavailable'}
 $classes=Invoke-ProbeRpc 'getClassList' @{isolateId=$main.id}
 $targets=@($classes.classes | Where-Object { $_.name -in @('AnalysisRecord','CompactHop') })
 if($targets.Count -ne 2){throw 'Expected both private A0 record classes'}
 foreach($class in $targets){
   try{ $null=Invoke-ProbeRpc 'setTraceClassAllocation' @{isolateId=$main.id;classId=$class.id;enable='true'};$enabled+=$class }
   catch{$report.errors+=$_.Exception.Message}
 }
 if($enabled.Count){
  Start-Sleep -Seconds $TraceSeconds
  foreach($class in $enabled){
   $traces=Invoke-ProbeRpc 'getAllocationTraces' @{isolateId=$main.id;classId=$class.id}
   $report.classes+= [ordered]@{name=$class.name;sample_count=$traces.sampleCount;returned_samples=@($traces.samples).Count;
     time_origin_micros=$traces.timeOriginMicros;time_extent_micros=$traces.timeExtentMicros}
  }
 }
}catch{$report.errors+=$_.Exception.Message}
finally{
 foreach($class in $enabled){
  try{$null=Invoke-ProbeRpc 'setTraceClassAllocation' @{isolateId=$main.id;classId=$class.id;enable='false'};$report.restored+=$class.name}
  catch{$report.errors+=('Restore failed: '+$_.Exception.Message)}
 }
 $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}
'Targeted allocation trace recorded; inspect errors and sample coverage'
