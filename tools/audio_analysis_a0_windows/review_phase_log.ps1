param([Parameter(Mandatory=$true)][string]$LogPath,
      [Parameter(Mandatory=$true)][string]$OutputPath,
      [int]$MinimumPhaseSeconds=600)
$ErrorActionPreference='Stop'
$content=Get-Content -LiteralPath $LogPath -Raw
$phases=@([regex]::Matches($content,'(?m)^A0_RESULT (.+)$') | ForEach-Object {
  $_.Groups[1].Value | ConvertFrom-Json
})
$issues=@()
if($phases.Count -ne 2){$issues+='Expected exactly two completed phases'}
if($content -match 'A0_ERROR'){$issues+='Diagnostic error output present'}
for($i=0;$i -lt $phases.Count;$i++){
  $p=$phases[$i]
  if($p.elapsed_seconds -lt $MinimumPhaseSeconds){$issues+="Phase $i shorter than required duration"}
  if($p.flutter_build_mode -ne 'profile'){$issues+="Phase $i is not Profile"}
  if($p.enabled -ne ($i -eq 1)){$issues+="Phase $i has unexpected OFF/ON order"}
  if($p.mask -notin @(3,11,5,13)){$issues+="Phase $i unknown diagnostic mask"}
  else{
    $vectors=1
    $elements=6
    if($p.mask -band 2){$vectors++;$elements+=256}
    if($p.mask -band 8){$vectors++;$elements+=64}
    $expectedVectors=$p.received_records*$vectors
    $expectedElements=$p.received_records*$elements
    if($null -eq $p.dart_parser_copies -or
       $p.dart_parser_copies.completed_vector_copies -ne $expectedVectors -or
       $p.dart_parser_copies.copied_numeric_elements -ne $expectedElements -or
       $p.dart_parser_copies.logical_float64_payload_bytes -ne $expectedElements*8){
      $issues+="Phase $i parser copy counters disagree with received records/mask"
    }
  }
  foreach($field in @('worker_failed','stop_failed')){
    if($p.native.$field -ne $false){$issues+="Phase $i $field true or missing"}
  }
  foreach($field in @('queue_drops','pcm_resets','underruns','submission_failures')){
    # OFF has no processor and therefore legitimately omits pcm_resets.
    if($i -eq 0 -and $field -eq 'pcm_resets'){continue}
    if($null -eq $p.native.$field -or $p.native.$field -ne 0){$issues+="Phase $i $field nonzero or missing"}
  }
  if($null -eq $p.native.endpoint_period_100ns -or
     $null -eq $p.native.submission_p99_us_upper_bound -or
     $p.native.submission_p99_us_upper_bound*10 -ge $p.native.endpoint_period_100ns){
    $issues+="Phase $i submission p99 upper bound does not prove strictly below period"
  }
  if($i -eq 1){
    foreach($field in @('missing_records','undelivered_tail_records','resynchronizations','pcm_continuity_changes','no_forward_intervals_over_100ms')){
      if($null -eq $p.$field -or $p.$field -ne 0){$issues+="Phase ON $field nonzero or missing"}
    }
    if($p.final_batch_accepted -ne $true -or $p.published_record_coverage -ne 1){$issues+='Incomplete final published coverage'}
  }
}
if($phases.Count -eq 2 -and ($phases[0].endpoint.endpoint_id -ne $phases[1].endpoint.endpoint_id -or
    $phases[0].mask -ne $phases[1].mask -or $phases[0].silent_output -ne $phases[1].silent_output)){
  $issues+='Mismatched endpoint/mask/silent mode'
}
$report=[ordered]@{kind='private_a0_phase_review';observed_utc=(Get-Date).ToUniversalTime().ToString('o');
  completed_phase_count=$phases.Count;minimum_phase_seconds=$MinimumPhaseSeconds;
  phase_observation_status=$(if($phases.Count -lt 2){'incomplete'}elseif($issues.Count){'failed_checks'}else{'listed_checks_pass'});
  issues=$issues;phases=$phases;
  limits='Only listed phase checks. Not A0 acceptance, allocator-event proof, clock uncertainty, audible lag or measured CPU default approval. Power-of-two p99 is an upper bound, not an exact percentile.'}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding utf8
"Phase review: $($report.phase_observation_status); completed=$($phases.Count); issues=$($issues.Count)"
