param(
  [Parameter(Mandatory=$true)][string]$VmServiceUri,
  [Parameter(Mandatory=$true)][string]$OutputPath,
  [string]$IsolateId,
  [int]$CaptureSeconds=15
)

# Bounded, separate Profile-runtime experiment. This script resets only the
# target isolate's allocation accumulators; it does not request garbage
# collection, evaluate application code, or enable class-allocation tracing.
$ErrorActionPreference = 'Stop'
if($CaptureSeconds -lt 1 -or $CaptureSeconds -gt 60){throw 'CaptureSeconds must be 1..60'}
if(Test-Path -LiteralPath $OutputPath){throw 'Refusing to overwrite an existing result'}

$service = [Uri]$VmServiceUri
if($service.Scheme -ne 'http' -or $service.Host -notin @('127.0.0.1','localhost','::1')){
  throw 'VM Service URI must be local HTTP (127.0.0.1, localhost or ::1)'
}
if(-not $service.AbsolutePath.EndsWith('/')){
  $service = [Uri]::new($service.AbsoluteUri + '/')
}

$result = [ordered]@{
  kind = 'a0_vm_allocation_profile_reset_probe'
  started_utc = (Get-Date).ToUniversalTime().ToString('o')
  requested_capture_seconds = $CaptureSeconds
  isolate_id = $null
  reset_requested = $true
  gc_requested = $false
  baseline = $null
  after = $null
  comparison = $null
  errors = @()
  interpretation = 'bytesCurrent/instancesCurrent are live snapshots, never throughput. Nonzero class counters and reset-window checks do not prove counters include retired allocations. Accumulated counters equal to current remain inconclusive. This is an isolate VM profile measurement, not proof of renderer/framework completeness or allocator committed bytes.'
}

function Invoke-VmServiceRpc([string]$Method,[hashtable]$Parameters=@{}){
  $pairs = @($Parameters.GetEnumerator() | ForEach-Object {
    [Uri]::EscapeDataString([string]$_.Key) + '=' + [Uri]::EscapeDataString([string]$_.Value)
  })
  $uriText = $service.AbsoluteUri + $Method
  if($pairs.Count){$uriText += '?' + ($pairs -join '&')}
  $reply = Invoke-RestMethod -Uri $uriText -TimeoutSec 10
  if($reply.error){throw ($Method+' RPC '+$reply.error.code+': '+$reply.error.message)}
  return $reply.result
}

function Get-AllocationSnapshot([string]$TargetIsolate,[bool]$Reset){
  $parameters = @{isolateId=$TargetIsolate}
  # The installed vm_service client omits gc unless true and documents reset
  # as happening before profile collection. Omission here means no GC request.
  if($Reset){$parameters.reset='true'}
  Invoke-VmServiceRpc 'getAllocationProfile' $parameters
}

function Get-ProfileSummary($Profile){
  $members = @($Profile.members)
  $byName = [ordered]@{}
  foreach($member in $members){
    $name = [string]$member.class.name
    if([string]::IsNullOrWhiteSpace($name)){$name = '[unnamed]'}
    $byName[$name] = [ordered]@{
      bytes_current = $member.bytesCurrent
      instances_current = $member.instancesCurrent
      accumulated_size = $member.accumulatedSize
      instances_accumulated = $member.instancesAccumulated
    }
  }
  [ordered]@{
    date_last_service_gc = $Profile.dateLastServiceGC
    date_last_accumulator_reset = $Profile.dateLastAccumulatorReset
    member_count = $members.Count
    bytes_current_sum = ($members | Measure-Object -Property bytesCurrent -Sum).Sum
    instances_current_sum = ($members | Measure-Object -Property instancesCurrent -Sum).Sum
    accumulated_size_sum = ($members | Measure-Object -Property accumulatedSize -Sum).Sum
    instances_accumulated_sum = ($members | Measure-Object -Property instancesAccumulated -Sum).Sum
    expected_positive_control_classes = [ordered]@{
      AnalysisRecord = $byName['AnalysisRecord']
      CompactHop = $byName['CompactHop']
    }
  }
}

try {
  $vm = Invoke-VmServiceRpc 'getVM'
  if(-not $IsolateId){
    $mainIsolate = @($vm.isolates | Where-Object { $_.name -eq 'main' })
    if($mainIsolate.Count -ne 1){throw 'Could not discover exactly one main isolate; pass -IsolateId explicitly'}
    $IsolateId = [string]$mainIsolate[0].id
  }
  $result.isolate_id = $IsolateId

  # The first response is preserved as returned. reset=true asks the VM to
  # clear accumulated allocation counters before collecting that snapshot.
  # The gc argument is omitted: no service collection is requested.
  $baselineStart = (Get-Date).ToUniversalTime().ToString('o')
  $baseline = Get-AllocationSnapshot $IsolateId $true
  $baselineEnd = (Get-Date).ToUniversalTime().ToString('o')
  $result.baseline = [ordered]@{
    request_start_utc = $baselineStart
    request_end_utc = $baselineEnd
    raw = $baseline
    summary = Get-ProfileSummary $baseline
  }

  Start-Sleep -Seconds $CaptureSeconds

  $afterStart = (Get-Date).ToUniversalTime().ToString('o')
  $after = Get-AllocationSnapshot $IsolateId $false
  $afterEnd = (Get-Date).ToUniversalTime().ToString('o')
  $result.after = [ordered]@{
    request_start_utc = $afterStart
    request_end_utc = $afterEnd
    raw = $after
    summary = Get-ProfileSummary $after
  }

  $baseReset = $baseline.dateLastAccumulatorReset
  $afterReset = $after.dateLastAccumulatorReset
  $record = @($after.members | Where-Object { $_.class.name -eq 'AnalysisRecord' } | Select-Object -First 1)
  $hop = @($after.members | Where-Object { $_.class.name -eq 'CompactHop' } | Select-Object -First 1)
  $controlsPresent = $record.Count -eq 1 -and $hop.Count -eq 1
  $controlsAdvanced = $controlsPresent -and
    [double]$record[0].instancesAccumulated -gt 0 -and
    [double]$hop[0].instancesAccumulated -gt 0
  $result.comparison = [ordered]@{
    baseline_reset_timestamp = $baseReset
    after_reset_timestamp = $afterReset
    reset_timestamp_unchanged = ($null -ne $baseReset -and $baseReset -eq $afterReset)
    expected_control_classes_present = $controlsPresent
    expected_control_classes_have_accumulated_instances = $controlsAdvanced
    positive_control_valid = ($null -ne $baseReset -and $baseReset -eq $afterReset -and $controlsAdvanced)
    aggregate_accumulated_size_bytes_vm_reported = $after.members | Measure-Object -Property accumulatedSize -Sum | Select-Object -ExpandProperty Sum
    aggregate_accumulated_instances_vm_reported = $after.members | Measure-Object -Property instancesAccumulated -Sum | Select-Object -ExpandProperty Sum
    current_bytes_are_throughput = $false
    classes_with_accumulated_size_above_current = @($after.members | Where-Object { $null -ne $_.accumulatedSize -and $null -ne $_.bytesCurrent -and $_.accumulatedSize -gt $_.bytesCurrent }).Count
    allocation_throughput_proven = $false
  }
  if(-not $result.comparison.positive_control_valid){
    $result.errors += 'Nonzero control classes or reset-window validation failed; do not use accumulator values as a phase total.'
  }
}catch{
  $result.errors += ($_.Exception.Message -replace 'http://(?:127\.0\.0\.1|localhost|\[::1\]):[^ ]+','[local service]')
}finally{
  $result.completed_utc = (Get-Date).ToUniversalTime().ToString('o')
  $parent = Split-Path -Parent $OutputPath
  if($parent -and -not (Test-Path -LiteralPath $parent)){New-Item -ItemType Directory -Path $parent -Force | Out-Null}
  $result | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}

if($result.errors.Count){throw ('VM allocation-profile probe incomplete; inspect '+$OutputPath)}
'VM allocation-profile probe recorded; review reset timestamp, positive-control classes and raw VM fields'
