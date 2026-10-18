param([Parameter(Mandatory=$true)][string]$LogPath,
      [Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$content=Get-Content -LiteralPath $LogPath -Raw
$reviews=@([regex]::Matches($content,'(?m)^A0_RESULT (.+)$') | ForEach-Object {
  $phase=$_.Groups[1].Value | ConvertFrom-Json
  $n=$phase.native
  $issues=@()
  $required=@('device_clock_frequency','device_clock_after_startup_first_ticks',
    'device_clock_after_startup_first_qpc_100ns','device_clock_last_accurate_ticks',
    'device_clock_last_accurate_qpc_100ns','device_clock_after_startup_relative_rate_ppm',
    'device_clock_after_startup_max_nominal_residual_ns')
  foreach($field in $required){if($null -eq $n.$field){$issues+="Missing $field"}}
  $ppm=$null
  $finalResidual=$null
  if(!$issues.Count){
    # Decimal subtraction preserves integer tick/QPC deltas before division.
    $ticks=[decimal]$n.device_clock_last_accurate_ticks-[decimal]$n.device_clock_after_startup_first_ticks
    $qpc=[decimal]$n.device_clock_last_accurate_qpc_100ns-[decimal]$n.device_clock_after_startup_first_qpc_100ns
    if($ticks -lt 0 -or $qpc -le 0 -or $n.device_clock_frequency -le 0){$issues+='Invalid raw clock interval'}
    else{
      $ppm=[double](($ticks*10000000/([decimal]$n.device_clock_frequency*$qpc)-1)*1000000)
      $finalResidual=[double]($ticks*1000000000/[decimal]$n.device_clock_frequency-$qpc*100)
      if([math]::Abs($ppm-$n.device_clock_after_startup_relative_rate_ppm) -gt 0.001){$issues+='Native/raw rate disagreement exceeds 0.001 ppm'}
      if($n.device_clock_after_startup_max_nominal_residual_ns -lt 0){$issues+='Negative residual'}
      if($n.device_clock_after_startup_max_nominal_residual_ns+1 -lt [math]::Abs($finalResidual)){$issues+='Reported maximum smaller than raw final residual'}
    }
  }
  foreach($field in @('device_clock_regressions','device_clock_frequency_changes','device_clock_inaccurate_readings')){
    if($null -eq $n.$field -or $n.$field -ne 0){$issues+="$field nonzero or missing"}
  }
  if($n.device_clock_after_startup_rate_available -ne $true){$issues+='Post-startup clock rate unavailable'}
  [ordered]@{enabled=$phase.enabled;elapsed_seconds=$phase.elapsed_seconds;
    raw_interval_relative_rate_ppm=$ppm;
    raw_final_nominal_residual_ns=$finalResidual;
    native_relative_rate_ppm=$n.device_clock_after_startup_relative_rate_ppm;
    max_nominal_residual_ns=$n.device_clock_after_startup_max_nominal_residual_ns;
    issues=$issues;status=$(if($issues.Count){'incomplete_or_inconsistent'}else{'listed_checks_pass'})}
})
[ordered]@{kind='private_a0_clock_review';observed_utc=(Get-Date).ToUniversalTime().ToString('o');
  completed_phase_count=$reviews.Count;phases=$reviews;
  limits='Independent endpoint-rate recomputation only. Maximum residual comes from native observations, not raw per-callback replay. No absolute timestamp uncertainty, acoustic latency, production anchor or A0 acceptance claim.'} |
  ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding utf8
"Clock review: $($reviews.Count) completed phases"
