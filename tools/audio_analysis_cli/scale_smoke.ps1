param(
  [int[]]$DurationsSeconds = @(6, 180),
  [int[]]$FeatureMasks = @(11, 5),
  [string]$CliPath = "build/tools/audio_analysis_cli/shipglows_audio_analysis_cli.exe"
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cli = (Resolve-Path (Join-Path $repo $CliPath)).Path
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureRoot = Join-Path $tempBase ("shipglows-a6-scale-" + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixtureRoot)

function New-SilentFloatWav([string]$Path, [int]$Seconds) {
  if ($Seconds -lt 1 -or $Seconds -gt 3600) { throw 'Duration must be between 1 second and 1 hour.' }
  [long]$frames = [long]$Seconds * 48000
  [long]$dataBytes = $frames * 8
  $header = [byte[]]::new(44)
  [Text.Encoding]::ASCII.GetBytes('RIFF').CopyTo($header, 0)
  [BitConverter]::GetBytes([uint32](36 + $dataBytes)).CopyTo($header, 4)
  [Text.Encoding]::ASCII.GetBytes('WAVEfmt ').CopyTo($header, 8)
  [BitConverter]::GetBytes([uint32]16).CopyTo($header, 16)
  [BitConverter]::GetBytes([uint16]3).CopyTo($header, 20)
  [BitConverter]::GetBytes([uint16]2).CopyTo($header, 22)
  [BitConverter]::GetBytes([uint32]48000).CopyTo($header, 24)
  [BitConverter]::GetBytes([uint32]384000).CopyTo($header, 28)
  [BitConverter]::GetBytes([uint16]8).CopyTo($header, 32)
  [BitConverter]::GetBytes([uint16]32).CopyTo($header, 34)
  [Text.Encoding]::ASCII.GetBytes('data').CopyTo($header, 36)
  [BitConverter]::GetBytes([uint32]$dataBytes).CopyTo($header, 40)

  $stream = [IO.FileStream]::new($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
  try {
    $stream.Write($header, 0, $header.Length)
    $zeros = [byte[]]::new(1MB)
    [long]$remaining = $dataBytes
    while ($remaining -gt 0) {
      $count = [int][Math]::Min($zeros.Length, $remaining)
      $stream.Write($zeros, 0, $count)
      $remaining -= $count
    }
  } finally { $stream.Dispose() }
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Invoke-Analysis([string]$WavPath, [string]$WavHash, [int]$Seconds, [int]$Mask) {
  $jobId = "scale-$Seconds-mask-$Mask"
  $sampleCount = [long]$Seconds * 48000
  $manifest = [ordered]@{
    schema_version = 1; job_id = $jobId; input_path = [IO.Path]::GetFileName($WavPath)
    sample_count = $sampleCount; max_duration_frames = $sampleCount
    max_memory_bytes = 8388608; max_output_bytes = 1500000000; max_runtime_ms = 1200000
    block_frames = 4096; feature_mask = $Mask; input_sha256 = $WavHash
    engine_revision = (git -C $repo rev-parse HEAD).Trim()
    sensitivity_db = 0; amplitude_gamma = 0.8; attack_ms = 20; release_ms = 180
    spectrum_floor_db = -80; spectrum_ceiling_db = 0
  }
  $manifestPath = Join-Path $fixtureRoot "$jobId.json"
  $manifest | ConvertTo-Json -Compress | Set-Content -LiteralPath $manifestPath -Encoding ascii
  $stdoutPath = Join-Path $fixtureRoot "$jobId.stdout.json"
  $stderrPath = Join-Path $fixtureRoot "$jobId.stderr.txt"
  $arguments = "--job-root `"$fixtureRoot`" --manifest `"$jobId.json`""
  $clock = [Diagnostics.Stopwatch]::StartNew()
  $process = Start-Process -FilePath $cli -ArgumentList $arguments -WorkingDirectory $repo `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
  [long]$peakWorkingSet = 0
  while (-not $process.HasExited) {
    try { $process.Refresh(); $peakWorkingSet = [Math]::Max($peakWorkingSet, $process.PeakWorkingSet64) } catch { }
    Start-Sleep -Milliseconds 50
  }
  $process.WaitForExit(); $process.Refresh()
  $peakWorkingSet = [Math]::Max($peakWorkingSet, $process.PeakWorkingSet64)
  $clock.Stop()
  $exitCode = $process.ExitCode
  $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -Raw -LiteralPath $stderrPath } else { '' }
  $stdout = if (Test-Path -LiteralPath $stdoutPath) { Get-Content -Raw -LiteralPath $stdoutPath } else { '' }
  if ($null -ne $exitCode -and $exitCode -ne 0) { throw "Offline scale job failed (exit $exitCode): $stderr" }
  $cliSummary = $stdout | ConvertFrom-Json
  if ($cliSummary.status -ne 'published') { throw "CLI did not confirm publication: stdout=$stdout stderr=$stderr" }
  $jobSummary = Get-Content -Raw -LiteralPath (Join-Path $fixtureRoot "jobs/$jobId/result.json") | ConvertFrom-Json
  if ($cliSummary.cache_key -ne $jobSummary.cache_key -or $jobSummary.sample_count -ne $sampleCount -or
      $jobSummary.feature_count -ne [Math]::Ceiling($sampleCount / 480.0) -or
      $jobSummary.chunk_count -ne [Math]::Ceiling($jobSummary.feature_count / 100.0) -or
      $jobSummary.page_count -ne [Math]::Ceiling($jobSummary.chunk_count / 256.0)) {
    throw 'CLI summary and published sidecar summary do not agree.'
  }
  [pscustomobject]@{
    duration_seconds = $Seconds; feature_mask = $Mask; sample_count = $sampleCount
    feature_count = $jobSummary.feature_count; chunk_count = $jobSummary.chunk_count
    page_count = $jobSummary.page_count; output_bytes = $jobSummary.output_bytes
    manifest_memory_budget_bytes = $manifest.max_memory_bytes
    process_peak_working_set_bytes = $peakWorkingSet
    elapsed_seconds = [Math]::Round($clock.Elapsed.TotalSeconds, 3)
    cache_key = $jobSummary.cache_key; input_sha256 = $WavHash
  }
}

try {
  foreach ($seconds in $DurationsSeconds) {
    $wavPath = Join-Path $fixtureRoot "silent-$seconds.wav"
    $wavHash = New-SilentFloatWav $wavPath $seconds
    foreach ($mask in $FeatureMasks) {
      if ($mask -notin @(3, 5, 11, 13)) { throw "Unsupported feature mask: $mask" }
      Invoke-Analysis $wavPath $wavHash $seconds $mask
    }
  }
} finally {
  $resolved = [IO.Path]::GetFullPath($fixtureRoot)
  $prefix = $tempBase.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
  if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
      -not (Split-Path -Leaf $resolved).StartsWith('shipglows-a6-scale-', [StringComparison]::Ordinal)) {
    throw 'Refusing to remove a path outside this script-owned temporary directory.'
  }
  Remove-Item -LiteralPath $resolved -Recurse -Force
}
