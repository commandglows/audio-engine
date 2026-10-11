param(
  [Parameter(Mandatory = $true)]
  [string]$CliPath
)

$ErrorActionPreference = 'Stop'
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
  'shipglows-cli-smoke-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempRoot | Out-Null

try {
  $stream = [System.IO.MemoryStream]::new()
  $writer = [System.IO.BinaryWriter]::new($stream)
  $ascii = [System.Text.Encoding]::ASCII
  $writer.Write($ascii.GetBytes('RIFF'))
  $writer.Write([uint32]36)
  $writer.Write($ascii.GetBytes('WAVEfmt '))
  $writer.Write([uint32]16)
  $writer.Write([uint16]3)
  $writer.Write([uint16]2)
  $writer.Write([uint32]48000)
  $writer.Write([uint32]384000)
  $writer.Write([uint16]8)
  $writer.Write([uint16]32)
  $writer.Write($ascii.GetBytes('data'))
  $writer.Write([uint32]0)
  $writer.Flush()

  $wavPath = Join-Path $tempRoot 'silence.wav'
  [System.IO.File]::WriteAllBytes($wavPath, $stream.ToArray())
  $inputHash = (Get-FileHash -LiteralPath $wavPath -Algorithm SHA256).Hash.ToLowerInvariant()
  $manifest = [ordered]@{
    schema_version = 1
    job_id = 'cli-smoke'
    input_path = 'silence.wav'
    sample_count = 0
    max_duration_frames = 2880000
    max_memory_bytes = 4194304
    max_output_bytes = 81984
    max_runtime_ms = 60000
    block_frames = 257
    feature_mask = 5
    input_sha256 = $inputHash
    engine_revision = ('a' * 40)
    sensitivity_db = 0
    amplitude_gamma = 0.8
    attack_ms = 20
    release_ms = 180
    spectrum_floor_db = -80
    spectrum_ceiling_db = 0
  } | ConvertTo-Json -Compress
  [System.IO.File]::WriteAllText(
    (Join-Path $tempRoot 'job.json'), $manifest,
    [System.Text.UTF8Encoding]::new($false))

  $result = & $CliPath --job-root $tempRoot --manifest job.json
  if ($LASTEXITCODE -ne 0) {
    throw "CLI returned exit code $LASTEXITCODE."
  }
  $published = Join-Path $tempRoot 'jobs/cli-smoke'
  if (-not (Test-Path -LiteralPath (Join-Path $published 'result.json')) -or
      -not (Test-Path -LiteralPath (Join-Path $published 'analysis-v2/metadata.json'))) {
    throw 'CLI did not atomically publish the validated package.'
  }
  if ($result -notmatch '"status":"published"') {
    throw 'CLI did not return its machine-readable success status.'
  }
  'CLI process smoke passed.'
} finally {
  $resolvedTemp = [System.IO.Path]::GetFullPath($tempRoot)
  $tempPrefix = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
  if ($resolvedTemp.StartsWith($tempPrefix, [System.StringComparison]::OrdinalIgnoreCase) -and
      (Split-Path -Leaf $resolvedTemp).StartsWith('shipglows-cli-smoke-')) {
    Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
  }
}
