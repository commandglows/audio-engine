param([Parameter(Mandatory=$true)][string]$LogPath,
      [Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$content=Get-Content -LiteralPath $LogPath -Raw
$match=[regex]::Match($content,'A Dart VM Service on Windows is available at: (http://127\.0\.0\.1:[0-9]+/[^\s]+/)')
if(-not $match.Success){throw 'A0 service not ready'}
$base=$match.Groups[1].Value
$sample=[ordered]@{utc=(Get-Date).ToUniversalTime().ToString('o');completed_phases=([regex]::Matches($content,'A0_RESULT ')).Count;gc_requested=$false;accumulators_reset=$false}
try {
  $vm=Invoke-RestMethod -Uri ($base+'getVM') -TimeoutSec 5
  if($vm.error){throw ('getVM RPC '+$vm.error.code)}
  $process=Get-Process -Id $vm.result.pid -ErrorAction Stop
  $sample.process_working_set_bytes=$process.WorkingSet64
  $sample.process_private_bytes=$process.PrivateMemorySize64
  $sample.process_cpu_seconds=$process.CPU
  $isolate=$vm.result.isolates | Where-Object { $_.name -eq 'main' } | Select-Object -First 1
  if(-not $isolate){throw 'Main isolate unavailable'}
  $id=[uri]::EscapeDataString($isolate.id)
  $memory=Invoke-RestMethod -Uri ($base+'getMemoryUsage?isolateId='+$id) -TimeoutSec 5
  if($memory.error){$sample.memory_rpc_error=$memory.error.code}else{$sample.memory=$memory.result}
  $allocation=Invoke-RestMethod -Uri ($base+'getAllocationProfile?isolateId='+$id) -TimeoutSec 5
  if($allocation.error){$sample.allocation_rpc_error=$allocation.error.code}else {
    $members=$allocation.result.members
    $sample.class_count=@($members).Count
    $sample.allocation_date_last_service_gc=$allocation.result.dateLastServiceGC
    $sample.allocation_date_last_accumulator_reset=$allocation.result.dateLastAccumulatorReset
    $sample.dart_current_bytes=($members | Measure-Object bytesCurrent -Sum).Sum
    $sample.dart_current_instances=($members | Measure-Object instancesCurrent -Sum).Sum
    $sample.dart_accumulated_bytes=($members | Measure-Object accumulatedSize -Sum).Sum
    $sample.dart_accumulated_instances=($members | Measure-Object instancesAccumulated -Sum).Sum
    # Class snapshots describe observed heap statistics, not event throughput.
    # Keep a bounded set useful for diagnosing vector/list retention. Missing
    # protocol fields remain null instead of being silently converted to zero.
    $sample.top_current_classes=@($members | Sort-Object bytesCurrent -Descending | Select-Object -First 12 | ForEach-Object {
      [ordered]@{name=$_.class.name;bytes_current=$_.bytesCurrent;instances_current=$_.instancesCurrent;
        accumulated_size=$_.accumulatedSize;instances_accumulated=$_.instancesAccumulated}
    })
    $sample.accumulated_equals_current_classes=@($members | Where-Object {
      $null -ne $_.accumulatedSize -and $null -ne $_.bytesCurrent -and
      $_.accumulatedSize -eq $_.bytesCurrent -and $_.instancesAccumulated -eq $_.instancesCurrent
    }).Count
    $sample.interpretation='Heap/class snapshot only; accumulated fields may equal current in Profile. No allocation event count or allocation throughput claim.'
  }
}catch{$sample.error=$_.Exception.Message -replace 'http://[^ ]+','[local service]'}
$sample | ConvertTo-Json -Depth 4 -Compress | Add-Content -LiteralPath $OutputPath -Encoding utf8
'A0 VM memory sample recorded (no forced GC or reset)'
