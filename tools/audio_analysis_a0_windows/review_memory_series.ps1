param([Parameter(Mandatory=$true)][string]$InputPath,
      [Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$samples=@(Get-Content -LiteralPath $InputPath | Where-Object {$_.Trim()} | ForEach-Object {$_ | ConvertFrom-Json})
$groups=@($samples | Group-Object completed_phases | ForEach-Object {
  $rows=@($_.Group | Sort-Object { [datetimeoffset]$_.utc })
  $metrics=[ordered]@{}
  foreach($name in @('process_working_set_bytes','process_private_bytes','dart_current_bytes','heapUsage','externalUsage')){
    $values=@($rows | ForEach-Object {
      $value=if($name -in @('heapUsage','externalUsage')){$_.memory.$name}else{$_.$name}
      if($null -ne $value){[double]$value}
    })
    $metrics[$name]=if($values.Count){[ordered]@{sample_count=$values.Count;
      minimum=($values | Measure-Object -Minimum).Minimum;maximum=($values | Measure-Object -Maximum).Maximum;
      first=$values[0];last=$values[-1];last_minus_first=$values[-1]-$values[0]}}else{$null}
  }
  $cpuRows=@($rows | Where-Object {$null -ne $_.process_cpu_seconds})
  $cpuPercent=$null
  if($cpuRows.Count -ge 2){
    $seconds=([datetimeoffset]$cpuRows[-1].utc-[datetimeoffset]$cpuRows[0].utc).TotalSeconds
    if($seconds -gt 0){$cpuPercent=100*($cpuRows[-1].process_cpu_seconds-$cpuRows[0].process_cpu_seconds)/$seconds}
  }
  [ordered]@{completed_phases=[int]$_.Name;sample_count=$rows.Count;first_utc=$rows[0].utc;last_utc=$rows[-1].utc;
    error_samples=@($rows | Where-Object {$_.error -or $_.memory_rpc_error -or $_.allocation_rpc_error}).Count;
    sampled_process_cpu_one_core_percent=$cpuPercent;metrics=$metrics}
})
[ordered]@{kind='private_a0_memory_series_review';observed_utc=(Get-Date).ToUniversalTime().ToString('o');
  total_samples=$samples.Count;groups=$groups;
  limits='Groups use completed-phase count at sample start, not precise OFF/ON boundaries. Sparse snapshots cannot establish allocation throughput, a memory cap or a flat working set. Whole-process CPU includes profiling and UI; no isolated causal attribution.'} |
  ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
"Memory review: $($samples.Count) samples; $($groups.Count) phase-count groups"
