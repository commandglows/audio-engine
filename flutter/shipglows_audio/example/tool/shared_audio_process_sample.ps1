param(
  [Parameter(Mandatory = $true)][int]$TargetProcessId,
  [Parameter(Mandatory = $true)][int]$DurationSeconds
)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$process = [System.Diagnostics.Process]::GetProcessById($TargetProcessId)
$previousCpu = $process.TotalProcessorTime.TotalMilliseconds
$previousAt = [System.Diagnostics.Stopwatch]::StartNew()
$previousCpuTime = $previousAt.Elapsed.TotalMilliseconds
$cpuSamples = [System.Collections.Generic.List[double]]::new()
$workingSetPeak = [long]0
$privateBytesPeak = [long]0
$deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
while ([DateTime]::UtcNow -lt $deadline) {
  Start-Sleep -Milliseconds 250
  try { $process.Refresh() } catch { break }
  $now = $previousAt.Elapsed.TotalMilliseconds
  $cpu = $process.TotalProcessorTime.TotalMilliseconds
  $elapsed = $now - $previousCpuTime
  if ($elapsed -gt 0) { $cpuSamples.Add([Math]::Max(0, 100 * ($cpu - $previousCpu) / $elapsed)) }
  $previousCpu = $cpu
  $previousCpuTime = $now
  $workingSetPeak = [Math]::Max($workingSetPeak, $process.WorkingSet64)
  $privateBytesPeak = [Math]::Max($privateBytesPeak, $process.PrivateMemorySize64)
}
$average = if ($cpuSamples.Count) { ($cpuSamples | Measure-Object -Average).Average } else { 0 }
$maximum = if ($cpuSamples.Count) { ($cpuSamples | Measure-Object -Maximum).Maximum } else { 0 }
@{ samples = $cpuSamples.Count; average_one_core_percent = $average; peak_one_core_percent = $maximum; working_set_peak_bytes = $workingSetPeak; private_bytes_peak = $privateBytesPeak } | ConvertTo-Json -Compress
