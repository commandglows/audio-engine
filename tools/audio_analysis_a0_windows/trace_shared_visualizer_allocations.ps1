param(
  [Parameter(Mandatory=$true)][string]$LogPath,
  [Parameter(Mandatory=$true)][string]$OutputPath,
  [int]$TraceSeconds=8
)
$ErrorActionPreference='Stop'
if($TraceSeconds -lt 1 -or $TraceSeconds -gt 20){throw 'TraceSeconds must be 1..20'}
$deadline=[DateTime]::UtcNow.AddSeconds(90)
$base=$null
while([DateTime]::UtcNow -lt $deadline -and $null -eq $base){
  if(Test-Path -LiteralPath $LogPath){
    $content=Get-Content -LiteralPath $LogPath -Raw
    $match=[regex]::Match($content,'A Dart VM Service on Windows is available at: (http://127\.0\.0\.1:[0-9]+/[^\s]+/)')
    if($match.Success){$base=$match.Groups[1].Value}
  }
  if($null -eq $base){Start-Sleep -Milliseconds 200}
}
if($null -eq $base){throw 'Flutter VM service was not found in the local run log'}
$traceDeadline=[DateTime]::UtcNow.AddSeconds(45)
$traceWindowFound=$false
while([DateTime]::UtcNow -lt $traceDeadline -and -not $traceWindowFound){
  $content=Get-Content -LiteralPath $LogPath -Raw
  $traceWindowFound=$content.Contains('SHARED_CLIENT_ALLOCATION_TRACE_BEGIN')
  if(-not $traceWindowFound){Start-Sleep -Milliseconds 100}
}
if(-not $traceWindowFound){throw 'Shared client trace window marker was not found'}

$report=[ordered]@{
  kind='shared_visualizer_targeted_allocation_trace'
  utc=(Get-Date).ToUniversalTime().ToString('o')
  trace_seconds=$TraceSeconds
  classes=@()
  errors=@()
  restored=@()
  limits='Separate targeted trace; instrumentation changes runtime overhead. Sampled target-class allocations only, not total bytes, complete allocator throughput, or an AC-A4 pass.'
}
$enabled=@()
function Invoke-LocalVmRpc([string]$Method,[hashtable]$Parameters=@{}){
  $query=@($Parameters.GetEnumerator()|ForEach-Object{
    [uri]::EscapeDataString($_.Key)+'='+[uri]::EscapeDataString([string]$_.Value)
  }) -join '&'
  $reply=Invoke-RestMethod -Uri ($base+$Method+'?'+$query) -TimeoutSec 5
  if($reply.error){throw ($Method+' RPC '+$reply.error.code+': '+$reply.error.message)}
  return $reply.result
}
try{
  $vm=Invoke-LocalVmRpc 'getVM'
  $isolate=$vm.isolates|Where-Object name -eq 'main'|Select-Object -First 1
  if(-not $isolate){throw 'Main isolate was not found'}
  $all=Invoke-LocalVmRpc 'getClassList' @{isolateId=$isolate.id}
  $wanted=@('AnalysisBatch','VisualRecord','SpeechWindow','SpeechWaveBin',
            'SceneState','_List')
  $targets=@($all.classes|Where-Object{$_.name -in $wanted})
  if($targets.Count -eq 0){throw 'No shared-visualizer trace classes were found'}
  $report.discovered_classes=@($targets|ForEach-Object{$_.name})
  foreach($class in $targets){
    try{
      $null=Invoke-LocalVmRpc 'setTraceClassAllocation' @{isolateId=$isolate.id;classId=$class.id;enable='true'}
      $enabled+=$class
    }catch{$report.errors+=($class.name+': '+$_.Exception.Message)}
  }
  if($enabled.Count -gt 0){
    Start-Sleep -Seconds $TraceSeconds
    foreach($class in $enabled){
      try{
        $trace=Invoke-LocalVmRpc 'getAllocationTraces' @{isolateId=$isolate.id;classId=$class.id}
        $report.classes+=[ordered]@{
          name=$class.name
          sample_count=$trace.sampleCount
          returned_samples=@($trace.samples).Count
          time_origin_micros=$trace.timeOriginMicros
          time_extent_micros=$trace.timeExtentMicros
        }
      }catch{$report.errors+=($class.name+': '+$_.Exception.Message)}
    }
  }
}catch{$report.errors+=$_.Exception.Message -replace 'http://[^ ]+','[local VM service]'}
finally{
  foreach($class in $enabled){
    try{
      $null=Invoke-LocalVmRpc 'setTraceClassAllocation' @{isolateId=$isolate.id;classId=$class.id;enable='false'}
      $report.restored+=$class.name
    }catch{$report.errors+=('Restore '+$class.name+': '+$_.Exception.Message)}
  }
  $report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $OutputPath -Encoding utf8
}
'Shared visualizer target allocation traces captured; inspect classes, errors, and extent.'
