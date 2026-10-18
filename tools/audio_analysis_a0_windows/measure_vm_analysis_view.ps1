param(
  [Parameter(Mandatory=$true)][string]$VmServiceUri,
  [Parameter(Mandatory=$true)][string]$OutputPath,
  [string]$IsolateId,
  [int]$MaximumObjects=10000,
  [int]$MaximumEdges=20000,
  [int]$PageSize=256
)

# Read-only reachable-object size inventory for the private A0 AnalysisView.
# No GC, accumulator reset, class tracing, or evaluate RPC is issued.
$ErrorActionPreference = 'Stop'
if($MaximumObjects -lt 1 -or $MaximumObjects -gt 10000){throw 'MaximumObjects must be 1..10000'}
if($MaximumEdges -lt 1 -or $MaximumEdges -gt 20000){throw 'MaximumEdges must be 1..20000'}
if($PageSize -lt 1 -or $PageSize -gt 256){throw 'PageSize must be 1..256'}
if(Test-Path -LiteralPath $OutputPath){throw 'Refusing to overwrite an existing result'}

$service = [Uri]$VmServiceUri
if($service.Scheme -ne 'http' -or $service.Host -notin @('127.0.0.1','localhost','::1')){
  throw 'VM Service URI must be local HTTP (127.0.0.1, localhost or ::1)'
}
if(-not $service.AbsolutePath.EndsWith('/')){$service=[Uri]::new($service.AbsoluteUri+'/')}

$result = [ordered]@{
  kind = 'a0_vm_analysis_view_reachable_object_sizes'
  started_utc = (Get-Date).ToUniversalTime().ToString('o')
  isolate_id = $null
  status = 'incomplete'
  scope = 'Shallow Obj.size values for the service-exposed object graph reachable from AnalysisView.latest and AnalysisView.speech, plus the AnalysisView object. Excludes _CalibrationPageState, inherited ChangeNotifier/listener fields, unreachable objects, transient poll batches, and VM/framework totals. Service-exposed graph size is not a physical peak or allocator-committed-byte measure.'
  runtime = $null
  limits = [ordered]@{maximum_objects=$MaximumObjects;maximum_edges=$MaximumEdges;page_size=$PageSize}
  state_instance_id = $null
  view_instance_id = $null
  initial_view = $null
  final_view = $null
  stability = $null
  graph = [ordered]@{node_count=0;edge_count=0;known_zero_inline_value_count=0;known_size_sum_lower_bound_bytes=0;unknown_size_count=0;sum_is_complete=$false;incomplete_reasons=@();nodes=@();edges=@()}
  errors = @()
  service_uri_recorded = $false
  gc_requested = $false
  reset_requested = $false
  evaluate_requested = $false
  completion_note = 'A complete status means every reachable reference in the selected graph had an Obj.size or a service-confirmed zero-size inline Smi. It does not certify hidden backing storage or physical allocation peak.'
}

$nodes = [System.Collections.Generic.List[object]]::new()
$edges = [System.Collections.Generic.List[object]]::new()
$queue = [System.Collections.Generic.Queue[object]]::new()
$visited = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$incomplete = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$script:graphEdgeCount = 0
$script:knownZeroCount = 0
$script:runningSizeSum = [long]0
$script:idZoneId = $null
$script:webSocket = $null
$script:rpcSequence = 0

function Add-Incomplete([string]$Reason){[void]$incomplete.Add($Reason)}

function Invoke-VmServiceRpc([string]$Method,[hashtable]$Parameters=@{}){
  if($null -eq $script:webSocket -or $script:webSocket.State -ne [System.Net.WebSockets.WebSocketState]::Open){throw 'Persistent VM Service WebSocket is not open'}
  $script:rpcSequence++
  $requestId=[string]$script:rpcSequence
  $request=[ordered]@{jsonrpc='2.0';id=$requestId;method=$Method;params=$Parameters}
  $payload=[Text.Encoding]::UTF8.GetBytes(($request | ConvertTo-Json -Depth 20 -Compress))
  $sendSegment=[ArraySegment[byte]]::new($payload)
  $timeout=[Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(15))
  $message=$null
  try{
    $null=$script:webSocket.SendAsync($sendSegment,[System.Net.WebSockets.WebSocketMessageType]::Text,$true,$timeout.Token).GetAwaiter().GetResult()
    $receiveBuffer=New-Object byte[] 65536
    $message=New-Object System.IO.MemoryStream
    do{
      $receiveSegment=[ArraySegment[byte]]::new($receiveBuffer)
      $received=$script:webSocket.ReceiveAsync($receiveSegment,$timeout.Token).GetAwaiter().GetResult()
      if($received.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close){throw 'VM Service WebSocket closed during RPC'}
      $message.Write($receiveBuffer,0,$received.Count)
    }while(-not $received.EndOfMessage)
    $reply=[Text.Encoding]::UTF8.GetString($message.ToArray()) | ConvertFrom-Json
    if([string]$reply.id -ne $requestId){throw 'VM Service WebSocket response id mismatch'}
  }finally{
    $timeout.Dispose()
    if($message){$message.Dispose()}
  }
  if($reply.error){throw ($Method+' RPC '+$reply.error.code+': '+$reply.error.message)}
  if($null -eq $reply.result){throw ($Method+' returned no result')}
  return $reply.result
}

function Open-VmServiceWebSocket{
  $builder=[UriBuilder]::new($service)
  $builder.Scheme='ws'
  $builder.Path=$service.AbsolutePath.TrimEnd('/')+'/ws'
  $builder.Query=$service.Query.TrimStart('?')
  $script:webSocket=[System.Net.WebSockets.ClientWebSocket]::new()
  $timeout=[Threading.CancellationTokenSource]::new([TimeSpan]::FromSeconds(15))
  try{$null=$script:webSocket.ConnectAsync($builder.Uri,$timeout.Token).GetAwaiter().GetResult()}
  finally{$timeout.Dispose()}
}

function Get-Object([string]$ObjectId,[int]$Offset=-1,[int]$Count=-1){
  $parameters=@{isolateId=$IsolateId;objectId=$ObjectId}
  if($null -ne $script:idZoneId){$parameters.idZoneId=$script:idZoneId}
  # PowerShell unwraps Nullable[int] in function binding; use ordinary ints
  # with -1 as the omitted sentinel rather than Nullable.Value.
  if($Offset -ge 0){$parameters.offset=[string][int]$Offset}
  if($Count -ge 0){$parameters.count=[string][int]$Count}
  Invoke-VmServiceRpc 'getObject' $parameters
}

function Get-Field($Object,[string]$Name){
  foreach($field in @($Object.fields)){
    if([string]$field.name -ceq $Name){return $field}
  }
  return $null
}

function Get-RefValue($Field){if($null -ne $Field){return $Field.value};return $null}
function Get-RefId($Reference){if($null -eq $Reference){return $null};return [string]$Reference.id}
function Get-ScalarValue($Reference){
  if($null -eq $Reference){return $null}
  if($null -ne $Reference.valueAsString){return [string]$Reference.valueAsString}
  if($Reference.kind -eq 'Null'){return 'null'}
  if($Reference.kind -eq 'Bool'){return [string]$Reference.valueAsString}
  return $null
}

function Get-ViewSnapshot([string]$ViewId){
  $view=Get-Object $ViewId
  if($view.type -eq 'Sentinel' -or $view.type -ne 'Instance'){throw 'AnalysisView getObject was missing or collected'}
  if([string]$view.class.name -notmatch '(^|\.)AnalysisView(<|$)'){throw 'View reference no longer identifies AnalysisView'}
  $latestField=Get-Field $view 'latest'
  $speechField=Get-Field $view 'speech'
  if($null -eq $latestField -or $null -eq $speechField){throw 'AnalysisView latest/speech field missing from service object'}
  $speechRef=Get-RefValue $speechField
  $speechId=Get-RefId $speechRef
  if(-not $speechId){throw 'AnalysisView speech list has no object id'}
  $speechObject=Get-Object $speechId
  if($speechObject.type -eq 'Sentinel' -or $speechObject.type -ne 'Instance'){throw 'AnalysisView speech container was missing or collected'}
  $speechClass=[string]$speechObject.class.name
  $speechStorageId=$speechId
  $speechStorageCapacity=$null
  if($speechClass -match '^CalibrationHopList(<|$)'){
    $lengthField=Get-Field $speechObject '_length'
    $storageField=Get-Field $speechObject '_storage'
    if($null -eq $lengthField -or $null -eq $storageField){throw 'CalibrationHopList storage/count fields missing'}
    $lengthText=Get-ScalarValue (Get-RefValue $lengthField)
    if($lengthText -notmatch '^[0-9]+$'){throw 'CalibrationHopList _length is not a readable nonnegative integer'}
    $speechLength=[int]$lengthText
    if($speechLength -gt 151){throw 'CalibrationHopList _length exceeds the fixed 151-slot bound'}
    $speechStorageId=Get-RefId (Get-RefValue $storageField)
    if(-not $speechStorageId){throw 'CalibrationHopList _storage has no object id'}
    $storageObject=Get-Object $speechStorageId
    if($storageObject.type -eq 'Sentinel' -or $storageObject.type -ne 'Instance' -or $null -eq $storageObject.length){throw 'CalibrationHopList backing list length unavailable'}
    $speechStorageCapacity=[int]$storageObject.length
    if($speechStorageCapacity -ne 151){throw 'CalibrationHopList backing list capacity is not exactly 151'}
  }else{
    if($null -eq $speechObject.length){throw 'AnalysisView speech list length unavailable'}
    $speechLength=[int]$speechObject.length
    $speechStorageCapacity=$speechLength
  }
  $activeElementIds=@(Get-ListElementIds $speechStorageId $speechLength)
  $counters=[ordered]@{}
  foreach($name in @('cursor','latestPublished','received','missing','resyncs','pcmResets','compactHopHighWater')){
    $field=Get-Field $view $name
    $counters[$name]=if($null -eq $field){$null}else{Get-ScalarValue (Get-RefValue $field)}
  }
  [ordered]@{
    utc=(Get-Date).ToUniversalTime().ToString('o')
    view_id=$ViewId
    view_size=$view.size
    cursor=$counters.cursor
    latest_published=$counters.latestPublished
    counters=$counters
    latest_id=(Get-RefId (Get-RefValue $latestField))
    speech_id=$speechId
    speech_length=$speechLength
    speech_class=$speechClass
    speech_storage_id=$speechStorageId
    speech_storage_capacity=$speechStorageCapacity
    active_element_ids=$activeElementIds
  }
}

function Add-Reference($Reference,[string]$FromId,[string]$Label){
  $script:graphEdgeCount++
  if($script:graphEdgeCount -gt $MaximumEdges){Add-Incomplete 'maximum_edges_reached';return}
  if($null -eq $Reference){Add-Incomplete ('null_reference_without_service_kind:'+ $Label);return}
  $kind=[string]$Reference.kind
  $id=Get-RefId $Reference
  if(-not $id){
    if($kind -in @('Int','Null','Bool') -and ($kind -ne 'Int' -or $null -ne $Reference.valueAsString)){
      $script:knownZeroCount++
      $edges.Add([ordered]@{from=$FromId;field=$Label;to=$null;kind=$kind;size_bytes=0;size_status='inline_no_heap_object'})
      return
    }
    Add-Incomplete ('reference_without_id_or_confirmed_inline_size:'+ $Label)
    $edges.Add([ordered]@{from=$FromId;field=$Label;to=$null;kind=$kind;size_bytes=$null;size_status='unknown'})
    return
  }
  $edges.Add([ordered]@{from=$FromId;field=$Label;to=$id;kind=$kind;size_status='queued_or_deduplicated'})
  if($visited.Contains($id)){return}
  if($visited.Count + $queue.Count -ge $MaximumObjects){Add-Incomplete 'maximum_objects_reached';return}
  [void]$visited.Add($id)
  $queue.Enqueue([ordered]@{id=$id;reference=$Reference;parent=$FromId;label=$Label})
}

function Get-ListPage([string]$ObjectId,[int]$Offset,[int]$Count){
  $page=Get-Object $ObjectId $Offset $Count
  if($page.type -eq 'Sentinel' -or $page.type -ne 'Instance'){throw ('List element page missing or collected: '+$ObjectId)}
  $actualOffset=if($null -eq $page.offset){0}else{[int]$page.offset}
  $elements=@($page.elements)
  if($actualOffset -ne $Offset -or $elements.Count -ne $Count){throw ('List element page offset/count mismatch: '+$ObjectId)}
  return $page
}

function Get-ListElementIds([string]$ObjectId,[int]$ElementCount){
  $ids=[System.Collections.Generic.List[string]]::new()
  $list=Get-Object $ObjectId
  if($list.type -eq 'Sentinel' -or $list.type -ne 'Instance' -or $null -eq $list.length){throw ('List length unavailable: '+$ObjectId)}
  if($ElementCount -lt 0 -or $ElementCount -gt [int]$list.length){throw ('Requested active list range exceeds backing length: '+$ObjectId)}
  for($offset=0;$offset -lt $ElementCount;$offset+=$PageSize){
    $count=[Math]::Min($PageSize,$ElementCount-$offset)
    $page=Get-ListPage $ObjectId $offset $count
    foreach($element in @($page.elements)){
      $id=Get-RefId $element
      if(-not $id){throw ('Active speech element has no object id at '+$ObjectId+'['+($offset+$ids.Count)+']')}
      $ids.Add($id)
    }
  }
  if($ids.Count -ne $ElementCount){throw ('Active speech element count mismatch: '+$ObjectId)}
  return $ids.ToArray()
}

try {
  Open-VmServiceWebSocket
  $vm=Invoke-VmServiceRpc 'getVM'
  $result.runtime=[ordered]@{target_cpu=$vm.targetCPU;target_os=$vm.targetOS;vm_name=$vm.name}
  if(-not $IsolateId){
    $mains=@($vm.isolates | Where-Object { $_.name -eq 'main' })
    if($mains.Count -ne 1){throw 'Could not discover exactly one main isolate; pass -IsolateId'}
    $IsolateId=[string]$mains[0].id
  }
  $result.isolate_id=$IsolateId

  # Keep one ID namespace across all WebSocket RPCs so repeated references to
  # the same VM object retain a stable ID while the graph is deduplicated.
  $zone=Invoke-VmServiceRpc 'createIdZone' @{
    isolateId=$IsolateId;backingBufferKind='Ring';idAssignmentPolicy='ReuseExisting';capacity=$MaximumEdges
  }
  if(-not $zone.id){throw 'VM did not return a service ID zone'}
  $script:idZoneId=[string]$zone.id
  $result.id_zone=[ordered]@{created=$true;id=$script:idZoneId;backing_buffer_kind=$zone.backingBufferKind;assignment_policy=$zone.idAssignmentPolicy;capacity=$MaximumEdges;deleted=$false}

  $classList=Invoke-VmServiceRpc 'getClassList' @{isolateId=$IsolateId}
  $stateClasses=@($classList.classes | Where-Object { $_.name -eq '_CalibrationPageState' })
  if($stateClasses.Count -ne 1){throw 'Expected exactly one _CalibrationPageState class'}
  $stateSet=Invoke-VmServiceRpc 'getInstances' @{isolateId=$IsolateId;objectId=[string]$stateClasses[0].id;limit=2;includeSubclasses=$false;idZoneId=$script:idZoneId}
  $stateInstances=@($stateSet.instances)
  if([int]$stateSet.totalCount -ne 1 -or $stateInstances.Count -ne 1){throw 'Expected exactly one live _CalibrationPageState instance'}
  $stateId=[string]$stateInstances[0].id
  if(-not $stateId){throw 'State instance has no service object id'}
  $result.state_instance_id=$stateId

  $stateObject=Get-Object $stateId
  if($stateObject.type -eq 'Sentinel' -or $stateObject.type -ne 'Instance'){throw '_CalibrationPageState was missing or collected'}
  $viewField=Get-Field $stateObject 'view'
  if($null -eq $viewField){throw '_CalibrationPageState.view field is missing'}
  $viewId=Get-RefId (Get-RefValue $viewField)
  if(-not $viewId){throw '_CalibrationPageState.view has no object id'}
  $result.view_instance_id=$viewId

  $initial=Get-ViewSnapshot $viewId
  $result.initial_view=$initial
  if($null -eq $initial.view_size){Add-Incomplete 'AnalysisView_size_missing'}

  # Count the root view, but traverse only its data fields. Inherited
  # ChangeNotifier/listener fields are deliberately excluded.
  $viewObject=Get-Object $viewId
  if($null -eq $viewObject.size){Add-Incomplete 'AnalysisView_size_missing'}
  else{$script:runningSizeSum=[long]$viewObject.size}
  [void]$visited.Add($viewId)
  $nodes.Add([ordered]@{
    id=$viewId;class=[string]$viewObject.class.name;kind=[string]$viewObject.kind;size_bytes=$viewObject.size
    size_status=if($null -eq $viewObject.size){'unknown'}else{'reported'}
    selected_fields=@('latest','speech');excluded_fields='All other AnalysisView fields, including inherited ChangeNotifier/listener fields'
    raw_service_object=$viewObject
  })
  foreach($fieldName in @('latest','speech')){
    $field=Get-Field $viewObject $fieldName
    Add-Reference (Get-RefValue $field) $viewId $fieldName
  }

  while($queue.Count -gt 0 -and $visited.Count -le $MaximumObjects){
    $item=$queue.Dequeue()
    $object=Get-Object ([string]$item.id)
    if($object.type -eq 'Sentinel' -or $object.type -ne 'Instance'){
      Add-Incomplete ('getObject_missing_or_collected:'+ [string]$item.id)
      continue
    }
    $objectSize=$object.size
    $sizeStatus='reported'
    if($null -eq $objectSize){
      $isSmi=$false
      if($object.kind -eq 'Int' -and $null -ne $object.valueAsString -and [string]$vm.targetCPU -match 'x64|x86_64|amd64'){
        $integerText=[string]$object.valueAsString
        if($integerText -match '^-?[0-9]+$'){
          try{$integerValue=[System.Numerics.BigInteger]::Parse($integerText);$isSmi=($integerValue -ge [System.Numerics.BigInteger]::Parse('-4611686018427387904') -and $integerValue -le [System.Numerics.BigInteger]::Parse('4611686018427387903'))}catch{$isSmi=$false}
        }
      }
      if($isSmi){$objectSize=0;$sizeStatus='known_zero_smi'}
      else{$sizeStatus='unknown_missing_size';Add-Incomplete ('object_size_missing:'+ [string]$item.id)}
    }
    if($null -ne $objectSize){$script:runningSizeSum += [long]$objectSize}
    $className=[string]$object.class.name
    $fieldNames=@($object.fields | ForEach-Object { [string]$_.name })
    $node=[ordered]@{
      id=[string]$item.id;class=$className;kind=[string]$object.kind;size_bytes=$objectSize;size_status=$sizeStatus
      length=$object.length;field_names=$fieldNames;raw_service_object=$object;raw_list_pages=@()
    }
    $nodes.Add($node)

    if($null -ne $object.fields){
      foreach($field in $object.fields){
        $fieldName=[string]$field.name
        Add-Reference $field.value ([string]$item.id) $fieldName
      }
    }

    if($null -ne $object.length){
      $length=[int]$object.length
      if($length -lt 0){Add-Incomplete ('negative_list_length:'+ [string]$item.id);continue}
      $remainingEdges=[Math]::Max(0,$MaximumEdges-$script:graphEdgeCount)
      $elementsToRead=[Math]::Min($length,$remainingEdges)
      if($elementsToRead -lt $length){Add-Incomplete ('list_truncated_by_edge_cap:'+ [string]$item.id)}
      if($elementsToRead -gt 0 -and -not ($object.kind -match 'List|String|TypedData|ByteData')){
        # length also describes strings, maps, records and other non-list kinds.
        # Only list-like collection kinds have separately addressable elements.
        $elementsToRead=0
      }
      for($offset=0;$offset -lt $elementsToRead;$offset+=$PageSize){
        $count=[Math]::Min($PageSize,$elementsToRead-$offset)
        $page=Get-ListPage ([string]$item.id) $offset $count
        $node.raw_list_pages+=@($page)
        $pageElements=@($page.elements)
        for($index=0;$index -lt $pageElements.Count;$index++){
          Add-Reference $pageElements[$index] ([string]$item.id) ('element['+($offset+$index)+']')
        }
      }
    }

    $viewLike=($className -match 'View|TypedData|Float(32|64)List|Int(8|16|32|64)List|Uint(8|16|32|64)List')
    if($viewLike -and $fieldNames.Count -eq 0 -and $null -ne $object.length){
      Add-Incomplete ('typed_or_view_backing_not_exposed:'+ [string]$item.id)
    }
    if($className -match '_GrowableList' -and ($fieldNames -notcontains '_data' -and $fieldNames -notcontains 'data')){
      Add-Incomplete ('growable_list_capacity_backing_not_exposed:'+ [string]$item.id)
    }
  }

  if($queue.Count -gt 0){Add-Incomplete 'maximum_objects_reached_with_pending_references'}
  $final=Get-ViewSnapshot $viewId
  $result.final_view=$final
  $stateFinal=Get-Object $stateId
  $finalViewField=Get-Field $stateFinal 'view'
  $finalStateViewId=Get-RefId (Get-RefValue $finalViewField)
  $initialElementIds=@($initial.active_element_ids)
  $finalElementIds=@($final.active_element_ids)
  $sameElements=($initialElementIds.Count -eq $finalElementIds.Count)
  if($sameElements){for($index=0;$index -lt $initialElementIds.Count;$index++){if($initialElementIds[$index] -cne $finalElementIds[$index]){$sameElements=$false;break}}}
  $stable=($finalStateViewId -eq $viewId -and $initial.cursor -eq $final.cursor -and
    $initial.latest_published -eq $final.latest_published -and $initial.latest_id -eq $final.latest_id -and
    $initial.speech_id -eq $final.speech_id -and $initial.speech_length -eq $final.speech_length -and
    $initial.speech_storage_id -eq $final.speech_storage_id -and
    $initial.speech_storage_capacity -eq $final.speech_storage_capacity -and $sameElements)
  $result.stability=[ordered]@{
    same_state_view_reference=($finalStateViewId -eq $viewId)
    cursor_unchanged=($initial.cursor -eq $final.cursor)
    latest_published_unchanged=($initial.latest_published -eq $final.latest_published)
    latest_record_unchanged=($initial.latest_id -eq $final.latest_id)
    speech_list_unchanged=($initial.speech_id -eq $final.speech_id -and $initial.speech_length -eq $final.speech_length -and $sameElements)
    speech_storage_unchanged=($initial.speech_storage_id -eq $final.speech_storage_id -and $initial.speech_storage_capacity -eq $final.speech_storage_capacity)
    stable=$stable
    initial_speech_element_count=$initialElementIds.Count
    final_speech_element_count=$finalElementIds.Count
  }
  if(-not $stable){Add-Incomplete 'AnalysisView_changed_during_graph_walk'}
}catch{
  $result.errors+=($_.Exception.Message -replace 'http://(?:127\.0\.0\.1|localhost|\[::1\]):[^ ]+','[local service]')
  Add-Incomplete ('fatal:'+($_.Exception.Message -replace '[^A-Za-z0-9_ -]',''))
}finally{
  $result.completed_utc=(Get-Date).ToUniversalTime().ToString('o')
  $result.graph.node_count=$nodes.Count
  $result.graph.edge_count=$edges.Count
  $result.graph.known_zero_inline_value_count=$script:knownZeroCount
  $result.graph.known_size_sum_lower_bound_bytes=$script:runningSizeSum
  $result.graph.unknown_size_count=@($nodes | Where-Object { $_.size_status -eq 'unknown_missing_size' -or $_.size_status -eq 'unknown' }).Count
  if($script:idZoneId){
    try{
      $deleted=Invoke-VmServiceRpc 'deleteIdZone' @{isolateId=$IsolateId;idZoneId=$script:idZoneId}
      $result.id_zone.deleted=($deleted.type -eq 'Success')
      if(-not $result.id_zone.deleted){Add-Incomplete 'id_zone_cleanup_not_confirmed'}
    }catch{
      $result.id_zone.deleted=$false
      Add-Incomplete 'id_zone_did_not_persist_or_cleanup_failed'
      $result.errors+=('ID zone cleanup failed: '+$_.Exception.Message)
    }
  }
  if($script:webSocket){
    try{$script:webSocket.Dispose()}catch{}
    $script:webSocket=$null
  }
  $result.graph.incomplete_reasons=@($incomplete)
  $result.graph.nodes=@($nodes)
  $result.graph.edges=@($edges)
  $result.graph.sum_is_complete=($incomplete.Count -eq 0 -and $result.errors.Count -eq 0 -and $null -ne $result.stability -and $result.stability.stable)
  if($result.errors.Count -eq 0 -and $incomplete.Count -eq 0 -and $null -ne $result.stability -and $result.stability.stable){$result.status='service_exposed_graph_complete'}
  $parent=Split-Path -Parent $OutputPath
  if($parent -and -not (Test-Path -LiteralPath $parent)){New-Item -ItemType Directory -Path $parent -Force | Out-Null}
  $result | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $OutputPath -Encoding utf8
}

if($result.status -ne 'service_exposed_graph_complete'){
  throw ('AnalysisView graph is incomplete or unstable; inspect '+$OutputPath)
}
'AnalysisView reachable object-size graph recorded; this is a scoped object graph, not a VM or physical peak'
