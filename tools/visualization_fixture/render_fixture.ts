import { createHash } from "node:crypto";
import { spawn, spawnSync } from "node:child_process";
import {
  access,
  mkdir,
  mkdtemp,
  readFile,
  rename,
  rm,
  writeFile,
} from "node:fs/promises";
import { createServer } from "node:http";
import { dirname, join, resolve } from "node:path";
import { tmpdir } from "node:os";
import { fileURLToPath } from "node:url";
import {
  evaluate,
  interpolateRecord,
  interpolateSpeechWindow,
  prepareSpeechWindow,
  type Config,
  type RecordFixture,
} from "./scene.ts";
import { FixtureSidecarReader } from "./sidecar.ts";

const repo = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const fps = 30,
  width = 640,
  height = 360,
  segmentFrames = 60,
  totalFrames = segmentFrames * 3;
const transform = {
  sensitivity_db: 0,
  amplitude_gamma: 0.8,
  attack_ms: 20,
  release_ms: 180,
  spectrum_floor_db: -80,
  spectrum_ceiling_db: 0,
};
const outputArg = process.argv.indexOf("--output");
if (outputArg >= 0 && !process.argv[outputArg + 1])
  throw new Error("--output requires a file path.");
let outputPath = resolve(
  outputArg >= 0
    ? process.argv[outputArg + 1]
    : join(repo, "build/visualization_fixture/stage-a.webm"),
);
const edgePath =
  process.env.SHIPGLOWS_EDGE_PATH ??
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
const dartPath =
  process.env.SHIPGLOWS_DART_PATH ??
  "C:\\Users\\Diane\\AppData\\Local\\ShipGlows\\flutter\\bin\\cache\\dart-sdk\\bin\\dart.exe";
const exe = join(
  repo,
  "build/tools/audio_analysis_cli/shipglows_audio_analysis_cli.exe",
);
const hash = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function finalWav(seconds: number): Buffer {
  const frames = seconds * 48000,
    out = Buffer.alloc(44 + frames * 8);
  out.write("RIFF", 0);
  out.writeUInt32LE(out.length - 8, 4);
  out.write("WAVEfmt ", 8);
  out.writeUInt32LE(16, 16);
  out.writeUInt16LE(3, 20);
  out.writeUInt16LE(2, 22);
  out.writeUInt32LE(48000, 24);
  out.writeUInt32LE(48000 * 8, 28);
  out.writeUInt16LE(8, 32);
  out.writeUInt16LE(32, 34);
  out.write("data", 36);
  out.writeUInt32LE(frames * 8, 40);
  for (let i = 0; i < frames; i++) {
    const secondsAt = i / 48000,
      fade =
        0.2 + 0.8 * (0.5 + 0.5 * Math.sin((2 * Math.PI * secondsAt) / 1.5));
    out.writeFloatLE(
      0.22 * fade * Math.sin(2 * Math.PI * 220 * secondsAt),
      44 + i * 8,
    );
    out.writeFloatLE(
      0.18 * fade * Math.sin(2 * Math.PI * 330 * secondsAt),
      48 + i * 8,
    );
  }
  return out;
}

async function runAnalysis(
  work: string,
  jobId: string,
  wavHash: string,
  sampleCount: number,
  mask: number,
  engineRevision: string,
) {
  const manifest = {
    schema_version: 1,
    job_id: jobId,
    input_path: "final.wav",
    sample_count: sampleCount,
    max_duration_frames: sampleCount,
    max_memory_bytes: 8_388_608,
    max_output_bytes: 20_000_000,
    max_runtime_ms: 120_000,
    block_frames: 4096,
    feature_mask: mask,
    input_sha256: wavHash,
    engine_revision: engineRevision,
    ...transform,
  };
  const manifestPath = `${jobId}.json`;
  const written = spawnSync(
    process.execPath,
    [
      "-e",
      `require('node:fs').writeFileSync(process.argv[1], process.argv[2])`,
      join(work, manifestPath),
      JSON.stringify(manifest),
    ],
    { encoding: "utf8" },
  );
  if (written.status !== 0)
    throw new Error(written.stderr || "Manifest creation failed.");
  const run = spawnSync(exe, ["--job-root", work, "--manifest", manifestPath], {
    cwd: repo,
    encoding: "utf8",
    timeout: 150_000,
  });
  if (run.status !== 0)
    throw new Error(
      `Offline analysis failed (${run.status}): ${run.stderr || run.stdout}`,
    );
  const cliSummary = JSON.parse(run.stdout.trim()),
    jobPath = join(work, "jobs", jobId);
  const summary = JSON.parse(
    await readFile(join(jobPath, "result.json"), "utf8"),
  );
  if (
    summary.cache_key !== cliSummary.cache_key ||
    summary.sample_count !== cliSummary.sample_count ||
    summary.feature_count !== cliSummary.feature_count
  )
    throw new Error("Published job summary differs from the CLI response.");
  return { summary, path: join(jobPath, "analysis-v2") };
}

function sameJson(a: unknown, b: unknown) {
  return JSON.stringify(a) === JSON.stringify(b);
}
function jsonRecord(r: RecordFixture) {
  return {
    ...r,
    sequence: r.sequence.toString(),
    centerFrame: r.centerFrame.toString(),
    continuityId: r.continuityId.toString(),
    originFrame: r.originFrame.toString(),
  };
}
function compareParity(
  actual: any,
  expected: any,
  at = "$",
  max = { value: 0 },
): number {
  if (typeof actual === "number" && typeof expected === "number") {
    const delta = Math.abs(actual - expected);
    max.value = Math.max(max.value, delta);
    if (delta > 1e-5)
      throw new Error(`Dart/TypeScript scene drift ${delta} at ${at}`);
  } else if (Array.isArray(actual) && Array.isArray(expected)) {
    if (actual.length !== expected.length)
      throw new Error(`Scene array length differs at ${at}`);
    actual.forEach((v, i) => compareParity(v, expected[i], `${at}[${i}]`, max));
  } else if (
    actual &&
    expected &&
    typeof actual === "object" &&
    typeof expected === "object"
  ) {
    const a = Object.keys(actual).sort(),
      b = Object.keys(expected).sort();
    if (!sameJson(a, b))
      throw new Error(`Scene fields differ at ${at}: ${a} != ${b}`);
    for (const key of a)
      compareParity(actual[key], expected[key], `${at}.${key}`, max);
  } else if (actual !== expected)
    throw new Error(`Scene values differ at ${at}: ${actual} != ${expected}`);
  return max.value;
}

function htmlPage(frameStates: unknown[], wavHash: string): string {
  const data = JSON.stringify(frameStates).replaceAll("<", "\\u003c");
  return `<!doctype html><meta charset="utf-8"><canvas id="c" width="${width}" height="${height}"></canvas>
<script>
const states=${data}, fps=${fps}, wavUrl='/final.wav', canvas=document.getElementById('c'), ctx=canvas.getContext('2d');
const colors={primary:'#63e6be',secondary:'#74c0fc',accent:'#ffd43b'};
function paint(s, globalFrame){
  ctx.fillStyle='#10151d';ctx.fillRect(0,0,${width},${height});
  ctx.fillStyle='#f1f3f5';ctx.font='18px sans-serif';ctx.fillText(s.label,24,30);
  ctx.fillStyle='#9aa4b2';ctx.font='12px sans-serif';ctx.fillText('global frame '+globalFrame+'  /  '+fps+' fps  /  fixture quality 640x360',24,52);
  if(s.bars) for(const b of s.bars){const bw=${width}/64;ctx.fillStyle=colors[b.role];const h=b.height*245;ctx.fillRect(b.x*${width}-bw*.38,${height}-28-h,bw*.76,h);}
  if(s.musicWave) for(const w of s.musicWave){const x=w.x*${width};ctx.lineWidth=2;for(const [a,b,color] of [[w.leftMin,w.leftMax,colors.primary],[w.rightMin,w.rightMax,colors.secondary]]){ctx.strokeStyle=color;ctx.beginPath();ctx.moveTo(x,a*${height});ctx.lineTo(x,b*${height});ctx.stroke();}}
  if(s.speechWave) for(const b of s.speechWave){if(!b.valid)continue;const x=b.x*${width},rh=b.rms*210,ph=b.peak*210;ctx.fillStyle=colors.secondary;ctx.fillRect(x-1,${height / 2}-ph,2,ph*2);ctx.fillStyle=colors.primary;ctx.fillRect(x-1.5,${height / 2}-rh,3,rh*2);}
  ctx.fillStyle='#9aa4b2';ctx.font='12px sans-serif';ctx.fillText('source WAV SHA-256: '+${JSON.stringify(wavHash)},24,${height - 12});
}
window.renderFixture=async()=>{
 const response=await fetch(wavUrl),ab=await response.arrayBuffer(),audio=new AudioContext({sampleRate:48000});
 const buffer=await audio.decodeAudioData(ab),dest=audio.createMediaStreamDestination(),source=audio.createBufferSource();source.buffer=buffer;source.connect(dest);
 const stream=canvas.captureStream(0),videoTrack=stream.getVideoTracks()[0];
 const combined=new MediaStream([...stream.getVideoTracks(),...dest.stream.getAudioTracks()]);
 if(combined.getVideoTracks().length!==1||combined.getAudioTracks().length!==1)throw Error('Expected exactly one video track and the sole final-WAV audio track.');
 const mime=['video/webm;codecs=vp9,opus','video/webm;codecs=vp8,opus'].find(MediaRecorder.isTypeSupported);
 if(!mime)throw Error('No supported WebM video/audio mux profile.');
 const chunks=[],recorder=new MediaRecorder(combined,{mimeType:mime,videoBitsPerSecond:1800000,audioBitsPerSecond:128000});
 recorder.ondataavailable=e=>{if(e.data.size)chunks.push(e.data)};
 const stopped=new Promise((resolve,reject)=>{recorder.onstop=resolve;recorder.onerror=e=>reject(e.error||Error('MediaRecorder failed'));});
 recorder.start();const start=performance.now();source.start(0);
 for(let frame=0;frame<states.length;frame++){
   const target=start+frame*1000/fps,wait=target-performance.now();if(wait>0)await new Promise(r=>setTimeout(r,wait));
   paint(states[frame],frame);videoTrack.requestFrame();
   if([0,60,120,179].includes(frame)){
     const png=await new Promise(resolve=>canvas.toBlob(resolve,'image/png'));
     const frameSaved=await fetch('/frame?index='+frame,{method:'POST',headers:{'content-type':'image/png'},body:png});
     if(!frameSaved.ok)throw Error('Could not retain a visual QA frame.');
   }
 }
 await new Promise((resolve,reject)=>{source.onended=resolve;setTimeout(()=>reject(Error('Final WAV playback did not finish')),buffer.duration*1000+3000);});
 recorder.stop();await stopped;await audio.close();
 const blob=new Blob(chunks,{type:mime});if(blob.size<4096)throw Error('Encoded fixture video is unexpectedly small.');
 const saved=await fetch('/result',{method:'POST',headers:{'content-type':mime},body:blob});if(!saved.ok)throw Error(await saved.text());
 const url=URL.createObjectURL(blob),probe=document.createElement('video');probe.muted=true;probe.src=url;
 await new Promise((resolve,reject)=>{probe.onloadedmetadata=resolve;probe.onerror=()=>reject(Error('Encoded WebM could not be decoded.'));});
 if(probe.videoWidth!==${width}||probe.videoHeight!==${height}||probe.duration<5.9)throw Error('Encoded WebM metadata is incomplete.');
 URL.revokeObjectURL(url);
 return {...await saved.json(),mime,frames:states.length,audioSeconds:buffer.duration,audioTracks:combined.getAudioTracks().length,videoTracks:combined.getVideoTracks().length,
   decodedDuration:probe.duration,decodedWidth:probe.videoWidth,decodedHeight:probe.videoHeight};
};
</script>`;
}

async function main() {
  await access(exe);
  await access(edgePath);
  await access(dartPath);
  if (outputArg < 0) {
    const base = outputPath.replace(/\.webm$/i, ""),
      stamp = new Date().toISOString().replace(/[:.]/g, "-");
    try {
      await access(outputPath);
      outputPath = `${base}-${stamp}.webm`;
    } catch (e) {
      if ((e as NodeJS.ErrnoException).code !== "ENOENT") throw e;
    }
  }
  try {
    await access(outputPath);
    throw new Error(
      `Output already exists; choose another path: ${outputPath}`,
    );
  } catch (e) {
    if ((e as NodeJS.ErrnoException).code !== "ENOENT") throw e;
  }
  const qaPath = outputPath.replace(/\.webm$/i, "") + "-frames";
  try {
    await access(qaPath);
    throw new Error(`QA frame folder already exists: ${qaPath}`);
  } catch (e) {
    if ((e as NodeJS.ErrnoException).code !== "ENOENT") throw e;
  }
  const work = await mkdtemp(join(tmpdir(), "shipglows-viz-a7-"));
  let browser: ReturnType<typeof spawn> | undefined,
    server: ReturnType<typeof createServer> | undefined,
    devtoolsSocket: WebSocket | undefined;
  try {
    const wav = finalWav(6),
      wavPath = join(work, "final.wav"),
      wavHash = hash(wav),
      sampleCount = 6 * 48000;
    await writeFile(wavPath, wav);
    const revision = spawnSync("git", ["rev-parse", "HEAD"], {
      cwd: repo,
      encoding: "utf8",
    });
    if (revision.status !== 0 || !/^[0-9a-f]{40}$/.test(revision.stdout.trim()))
      throw new Error("Could not identify the local engine revision.");
    const engineRevision = revision.stdout.trim();
    const musicJob = await runAnalysis(
      work,
      "fixture-music",
      wavHash,
      sampleCount,
      11,
      engineRevision,
    );
    const speechJob = await runAnalysis(
      work,
      "fixture-speech",
      wavHash,
      sampleCount,
      5,
      engineRevision,
    );
    if (
      musicJob.summary.sample_count !== speechJob.summary.sample_count ||
      musicJob.summary.cache_key === speechJob.summary.cache_key
    )
      throw new Error(
        "Analysis packages do not describe the same WAV under distinct feature masks.",
      );
    const readerExpectation = (mask: number, summary: any) => ({
      inputSha256: wavHash,
      featureMask: mask,
      cacheKey: summary.cache_key,
      metadataSha256: summary.metadata_sha256,
      pageIndexSha256: summary.page_index_sha256,
      transformDigest: summary.transform_digest,
    });
    const musicReader = await FixtureSidecarReader.open(
      musicJob.path,
      readerExpectation(11, musicJob.summary),
    );
    const speechReader = await FixtureSidecarReader.open(
      speechJob.path,
      readerExpectation(5, speechJob.summary),
    );
    const randomOrder = Array.from(
      { length: totalFrames },
      (_, i) => (i * 73) % totalFrames,
    );
    const sceneStates: unknown[] = Array(totalFrames),
      parityFrames: unknown[] = Array(totalFrames);
    const musicRecords = new Map<number, RecordFixture>(),
      speechRecords = new Map<number, RecordFixture>();
    const recordAt = async (profile: "music" | "speech", i: number) => {
      const reader = profile === "music" ? musicReader : speechReader;
      const cache = profile === "music" ? musicRecords : speechRecords;
      const index = Math.min(i, reader.metadata.feature_count - 1),
        found = cache.get(index);
      if (found) return found;
      const record = await reader.readRecord(index);
      cache.set(index, record);
      return record;
    };
    const speechWindows = new Map<
      number,
      ReturnType<typeof prepareSpeechWindow>
    >();
    const windowAt = async (i: number) => {
      const found = speechWindows.get(i);
      if (found) return found;
      const history: RecordFixture[] = [];
      for (let n = Math.max(0, i - 149); n <= i; n++)
        history.push(await recordAt("speech", n));
      const endpoint = (await recordAt("speech", i)).centerFrame;
      const window = prepareSpeechWindow(history, endpoint);
      speechWindows.set(i, window);
      return window;
    };
    for (const globalFrame of randomOrder) {
      const profile = globalFrame < 2 * segmentFrames ? "music" : "speech";
      const reader = profile === "music" ? musicReader : speechReader;
      const sample = BigInt(globalFrame) * 1600n,
        lo = Number(sample / 480n),
        rem = sample % 480n;
      const rightIndex = Math.min(lo + 1, reader.metadata.feature_count - 1);
      const alpha = Number(rem) / 480,
        left = await recordAt(profile, lo),
        right = await recordAt(profile, rightIndex);
      const record =
        alpha === 0 || right === left
          ? left
          : interpolateRecord(left, right, alpha);
      const segment = Math.floor(globalFrame / segmentFrames),
        mode =
          segment === 0 ? "bars" : segment === 1 ? "musicWave" : "speechWave";
      const config: Config = {
        mode,
        profile,
        featureMask: profile === "music" ? 11 : 5,
        transformDigest: left.transformDigest,
        intensity: 0.72,
        motion: "full",
        timeSeconds: globalFrame / fps,
      };
      let speechWindow;
      if (profile === "speech") {
        const loWindow = await windowAt(lo),
          hiWindow = await windowAt(rightIndex);
        speechWindow =
          alpha === 0 || hiWindow === loWindow
            ? loWindow
            : interpolateSpeechWindow(loWindow, hiWindow, alpha);
      }
      sceneStates[globalFrame] = evaluate(config, "live", record, speechWindow);
      parityFrames[globalFrame] = {
        profile,
        leftIndex: lo,
        rightIndex,
        alpha,
        mode,
        config: { ...config },
      };
    }
    for (let i = 0; i < totalFrames; i++) {
      const globalFrame = i,
        segment = Math.floor(globalFrame / segmentFrames),
        profile = segment < 2 ? "music" : "speech";
      if (
        (parityFrames[i] as any).config.timeSeconds !== globalFrame / fps ||
        (parityFrames[i] as any).config.profile !== profile
      )
        throw new Error("Nested scene global-frame mapping failed.");
      if (profile === "speech" && !(sceneStates[i] as any).speechWave)
        throw new Error("Prepared speech context was not rendered.");
    }
    const parityInput = join(work, "dart-parity-input.json"),
      parityOutput = join(work, "dart-parity-output.json");
    const serializeCache = (cache: Map<number, RecordFixture>) =>
      [...cache.entries()].map(([index, record]) => ({
        index,
        record: jsonRecord(record),
      }));
    await writeFile(
      parityInput,
      JSON.stringify({
        musicRecords: serializeCache(musicRecords),
        speechRecords: serializeCache(speechRecords),
        frames: parityFrames,
      }),
    );
    const dart = spawnSync(
      dartPath,
      ["run", "tool/scene_fixture_parity.dart", parityInput, parityOutput],
      {
        cwd: join(repo, "flutter/shipglows_visualization"),
        encoding: "utf8",
        timeout: 120_000,
      },
    );
    if (dart.status !== 0)
      throw new Error(
        `Dart scene parity runner failed: ${(dart.stderr || dart.stdout).slice(-3000)}`,
      );
    const dartStates = JSON.parse(await readFile(parityOutput, "utf8"));
    if (dartStates.length !== totalFrames)
      throw new Error("Dart did not evaluate every global frame.");
    let maxSceneDelta = 0;
    for (let i = 0; i < totalFrames; i++)
      maxSceneDelta = Math.max(
        maxSceneDelta,
        compareParity(sceneStates[i], dartStates[i], `frame ${i}`),
      );
    const states = sceneStates.map((scene: any, globalFrame) => ({
      ...scene,
      globalFrame,
      segmentIndex: Math.floor(globalFrame / segmentFrames),
      localFrame: globalFrame % segmentFrames,
      quality: "fixture-640x360-30fps",
    }));
    if (
      states.some(
        (state: any, i) =>
          state.globalFrame !== i ||
          state.localFrame !== i % segmentFrames ||
          state.segmentIndex !== Math.floor(i / segmentFrames) ||
          state.quality !== "fixture-640x360-30fps",
      )
    )
      throw new Error(
        "Global frame, nested frame, or quality binding changed.",
      );
    const distinctAudioSources = new Set([
      musicJob.summary.sample_count + ":" + wavHash,
      speechJob.summary.sample_count + ":" + wavHash,
    ]);
    if (
      distinctAudioSources.size !== 1 ||
      musicJob.summary.sample_count !== sampleCount
    )
      throw new Error(
        "Fixture analyses reference duplicate or mismatched audio sources.",
      );

    let port = 0;
    server = createServer(async (req, res) => {
      if (req.method === "GET" && req.url === "/") {
        res.writeHead(200, { "content-type": "text/html" });
        res.end(htmlPage(states, wavHash));
        return;
      }
      if (req.method === "GET" && req.url === "/final.wav") {
        res.writeHead(200, {
          "content-type": "audio/wav",
          "content-length": wav.length,
        });
        res.end(wav);
        return;
      }
      if (req.method === "POST" && req.url === "/result") {
        const parts: Buffer[] = [];
        let size = 0;
        for await (const part of req) {
          const b = Buffer.from(part);
          size += b.length;
          if (size > 50_000_000) {
            res.writeHead(413);
            res.end();
            return;
          }
          parts.push(b);
        }
        const data = Buffer.concat(parts);
        await writeFile(join(work, "render.webm"), data);
        res.writeHead(200, { "content-type": "application/json" });
        res.end(JSON.stringify({ bytes: data.length, sha256: hash(data) }));
        return;
      }
      if (req.method === "POST" && req.url?.startsWith("/frame?index=")) {
        const index = Number(
          new URL(req.url, "http://localhost").searchParams.get("index"),
        );
        if (![0, 60, 120, 179].includes(index)) {
          res.writeHead(400);
          res.end();
          return;
        }
        const parts: Buffer[] = [];
        for await (const part of req) parts.push(Buffer.from(part));
        await writeFile(join(work, `frame-${index}.png`), Buffer.concat(parts));
        res.writeHead(204);
        res.end();
        return;
      }
      res.writeHead(404);
      res.end();
    });
    await new Promise<void>((r) => server!.listen(0, "127.0.0.1", r));
    port = (server.address() as any).port;
    const debugPortServer = createServer();
    await new Promise<void>((r) => debugPortServer.listen(0, "127.0.0.1", r));
    const debugPort = (debugPortServer.address() as any).port;
    await new Promise<void>((r) => debugPortServer.close(() => r()));
    const profileDir = join(work, "edge-profile");
    browser = spawn(
      edgePath,
      [
        "--headless=new",
        "--disable-gpu",
        "--disable-background-networking",
        "--disable-default-apps",
        "--no-first-run",
        "--autoplay-policy=no-user-gesture-required",
        `--remote-debugging-port=${debugPort}`,
        `--user-data-dir=${profileDir}`,
        `http://127.0.0.1:${port}/`,
      ],
      { stdio: "ignore", windowsHide: true },
    );
    const targetUrl = `http://127.0.0.1:${debugPort}/json/list`;
    let target: any;
    for (let i = 0; i < 100; i++) {
      try {
        const tabs = (await (await fetch(targetUrl)).json()) as any[];
        target = tabs.find((t) => t.type === "page" && t.webSocketDebuggerUrl);
        if (target) break;
      } catch {}
      await sleep(100);
    }
    if (!target)
      throw new Error("Headless Edge did not expose a local DevTools page.");
    const ws = (devtoolsSocket = new WebSocket(target.webSocketDebuggerUrl));
    await new Promise<void>((r, j) => {
      ws.addEventListener("open", () => r(), { once: true });
      ws.addEventListener(
        "error",
        () => j(new Error("Could not open local DevTools WebSocket.")),
        { once: true },
      );
    });
    let id = 0;
    const pending = new Map<number, (m: any) => void>();
    ws.addEventListener("message", (event) => {
      const m = JSON.parse(String(event.data));
      if (m.id && pending.has(m.id)) {
        pending.get(m.id)!(m);
        pending.delete(m.id);
      }
    });
    const cdp = (method: string, params: any = {}) =>
      new Promise<any>((resolve, reject) => {
        const callId = ++id,
          timer = setTimeout(() => {
            pending.delete(callId);
            reject(new Error(`DevTools timed out: ${method}`));
          }, 30_000);
        pending.set(callId, (m) => {
          clearTimeout(timer);
          m.error ? reject(new Error(m.error.message)) : resolve(m.result);
        });
        ws.send(JSON.stringify({ id: callId, method, params }));
      });
    await cdp("Page.enable");
    let ready = false;
    for (let i = 0; i < 100; i++) {
      const state = await cdp("Runtime.evaluate", {
        expression:
          'document.readyState === "complete" && typeof window.renderFixture === "function"',
        returnByValue: true,
      });
      if (state.result.value === true) {
        ready = true;
        break;
      }
      await sleep(100);
    }
    if (!ready)
      throw new Error("Fixture page did not finish loading in headless Edge.");
    const evaluation = await cdp("Runtime.evaluate", {
      expression: "window.renderFixture()",
      awaitPromise: true,
      returnByValue: true,
      timeout: 30_000,
    });
    if (evaluation.exceptionDetails)
      throw new Error(
        evaluation.exceptionDetails.text || "Browser fixture render failed.",
      );
    const browserResult = evaluation.result.value;
    if (
      !browserResult ||
      browserResult.audioTracks !== 1 ||
      browserResult.videoTracks !== 1 ||
      browserResult.frames !== totalFrames ||
      browserResult.decodedWidth !== width ||
      browserResult.decodedHeight !== height ||
      browserResult.decodedDuration < 5.9
    )
      throw new Error(
        "Browser mux result did not prove one audio source and one video track.",
      );
    ws.close();
    devtoolsSocket = undefined;
    await mkdir(qaPath, { recursive: true });
    for (const frame of [0, 60, 120, 179])
      await rename(
        join(work, `frame-${frame}.png`),
        join(qaPath, `frame-${frame}.png`),
      );
    await mkdir(dirname(outputPath), { recursive: true });
    await rename(join(work, "render.webm"), outputPath);
    process.stdout.write(
      JSON.stringify({
        status: "rendered",
        output: outputPath,
        source_sha256: wavHash,
        output_sha256: browserResult.sha256,
        bytes: browserResult.bytes,
        frames: browserResult.frames,
        fps,
        dart_typescript_parity_frames: totalFrames,
        max_scene_delta: maxSceneDelta,
        audio_seconds: browserResult.audioSeconds,
        decoded_duration: browserResult.decodedDuration,
        dimensions: `${browserResult.decodedWidth}x${browserResult.decodedHeight}`,
        audio_tracks: browserResult.audioTracks,
        video_tracks: browserResult.videoTracks,
        source_track: "final.wav (sole WAV)",
        quality: "fixture-640x360-30fps",
        source_sha256_verified: true,
        music_cache_key: musicJob.summary.cache_key,
        speech_cache_key: speechJob.summary.cache_key,
      }) + "\n",
    );
  } finally {
    if (devtoolsSocket) devtoolsSocket.close();
    if (browser?.pid) {
      const pid = browser.pid;
      spawnSync("taskkill", ["/PID", String(pid), "/T", "/F"], {
        stdio: "ignore",
      });
      if (browser.exitCode === null)
        await Promise.race([
          new Promise<void>((r) => browser!.once("exit", () => r())),
          sleep(5000),
        ]);
      browser = undefined;
    }
    if (server) await new Promise<void>((r) => server!.close(() => r()));
    const resolved = resolve(work);
    if (
      !resolved.startsWith(
        resolve(tmpdir()) + (process.platform === "win32" ? "\\" : "/"),
      ) ||
      !resolved.includes("shipglows-viz-a7-")
    )
      throw new Error(
        "Refusing to clean a temporary path outside the fixture workspace.",
      );
    for (let attempt = 0; attempt < 5; attempt++) {
      try {
        await rm(resolved, {
          recursive: true,
          force: true,
          maxRetries: 3,
          retryDelay: 250,
        });
        break;
      } catch (error) {
        if (attempt === 4) throw error;
        await sleep(500);
      }
    }
  }
}

main().catch((error) => {
  process.stderr.write(
    `fixture render failed: ${error instanceof Error ? error.message : String(error)}\n`,
  );
  process.exitCode = 1;
});
