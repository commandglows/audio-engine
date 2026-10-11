import test from "node:test";
import assert from "node:assert/strict";
import {
  evaluate,
  interpolateRecord,
  interpolateSpeechWindow,
  prepareSpeechWindow,
  type Config,
  type RecordFixture,
} from "./scene.ts";

const digest = "a".repeat(64);
const music: RecordFixture = {
  streamId: "stream-1",
  sourceIdentity: "source-1",
  transformDigest: digest,
  originFrame: 0n,
  continuityId: 1n,
  centerFrame: 0n,
  sequence: 1n,
  validFrames: 480,
  envelopeBins: 64,
  physicalLevels: [0.2, 0.2, 0.2, 0.2, 0.2, 0.2],
  profile: "music",
  featureMask: 11,
  visualLevels: [0.25, 0.25, 0.25, 0.25, 0.25, 0.25],
  visualEnvelope: Array.from({ length: 256 }, (_, i) => (i % 4) / 4),
  visualBands: Array.from({ length: 64 }, (_, i) => i / 64),
};
const config = (mode: Config["mode"], extra: Partial<Config> = {}): Config => ({
  mode,
  profile: "music",
  featureMask: 11,
  transformDigest: digest,
  intensity: 0.65,
  motion: "full",
  ...extra,
});

test("bars and waveform geometry match the Dart fixture values", () => {
  const bars = evaluate(config("bars"), "live", music);
  assert.equal(bars.bars?.length, 64);
  assert.equal(bars.bars?.[63].height, 0.63984375);
  assert.equal(bars.bars?.[1].role, "secondary");
  const wave = evaluate(config("musicWave"), "live", music);
  assert.equal(wave.musicWave?.length, 64);
  assert.equal(wave.musicWave?.[0].leftMin, 0.25);
  assert.equal(wave.musicWave?.[0].leftMax, 0.25 - 0.2 * 0.65 * 0.25);
});

test("speech view checks identity, suffix validity, and fixed baseline", () => {
  const speechRecord: RecordFixture = {
    ...music,
    profile: "speech",
    featureMask: 5,
    visualBands: undefined,
    visualEnvelope: undefined,
  };
  const window = {
    streamId: "stream-1",
    sourceIdentity: "source-1",
    originFrame: 0n,
    continuityId: 1n,
    transformDigest: digest,
    endpointCenterFrame: 0n,
    validSuffixCount: 1,
    physicalRms: Array(150).fill(0),
    physicalPeak: Array(150).fill(0),
    visualRms: Array(150).fill(0),
    visualPeak: Array(150).fill(0),
  };
  window.visualRms[149] = 0.5;
  const scene = evaluate(
    { ...config("speechWave"), profile: "speech", featureMask: 5 },
    "live",
    speechRecord,
    window,
  );
  assert.equal(scene.speechWave?.length, 150);
  assert.equal(scene.speechWave?.[148].valid, false);
  assert.equal(scene.speechWave?.[149].rms, 0.35 * 0.65 * 0.5);
  const baseline = evaluate(
    {
      ...config("speechWave"),
      profile: "speech",
      featureMask: 5,
      intensity: 0,
    },
    "live",
    speechRecord,
    window,
  );
  assert.ok(baseline.baseline);
  assert.ok(
    baseline.speechWave?.every((v) => v.valid && v.rms === 0 && v.peak === 0),
  );
});

test("prepared speech windows keep the exact contiguous suffix and interpolate adjacent endpoints", () => {
  const record = (
    sequence: bigint,
    centerFrame: bigint,
    rms: number,
  ): RecordFixture => ({
    streamId: "speech",
    sourceIdentity: "wav-a",
    transformDigest: digest,
    originFrame: 0n,
    continuityId: 2n,
    centerFrame,
    sequence,
    validFrames: 480,
    envelopeBins: 0,
    profile: "speech",
    featureMask: 5,
    physicalLevels: [1, 1, 2, 1, 1, rms],
    visualLevels: [0.2, 0.2, 0.4, 0.2, 0.2, rms / 4],
  });
  const first = record(1n, 0n, 0.8),
    second = record(2n, 480n, 2.4);
  const left = prepareSpeechWindow([first], 0n);
  const right = prepareSpeechWindow([first, second], 480n);
  assert.equal(left.validSuffixCount, 1);
  assert.equal(right.validSuffixCount, 2);
  assert.equal(right.physicalRms[148], 0.8);
  const middle = interpolateSpeechWindow(left, right, 0.5);
  assert.equal(middle.validSuffixCount, 1);
  assert.equal(middle.physicalRms[148], 0);
  assert.equal(middle.physicalRms[149], 1.6);
  assert.equal(middle.visualRms[149], 0.4);
  assert.equal(interpolateSpeechWindow(left, right, 0), left);
  assert.equal(interpolateSpeechWindow(left, right, 1), right);
  assert.throws(
    () => interpolateSpeechWindow(left, { ...right, continuityId: 3n }, 0.5),
    /share identity/,
  );
});

test("record interpolation recomputes channel-combined physical fields", () => {
  const left = {
    ...music,
    sequence: 1n,
    centerFrame: 0n,
    physicalLevels: [1.6, 0, 1.6, 1.2, 0.4, Math.sqrt(0.8)],
  };
  const right = {
    ...music,
    sequence: 2n,
    centerFrame: 480n,
    physicalLevels: [0, 1.6, 1.6, 0.4, 1.2, Math.sqrt(0.8)],
  };
  const middle = interpolateRecord(left, right, 0.5);
  assert.equal(middle.physicalLevels?.[2], 0.8);
  assert.equal(middle.physicalLevels?.[5], 0.8);
  assert.equal(interpolateRecord(left, right, 0), left);
  assert.throws(
    () => interpolateRecord(left, { ...right, continuityId: 2n }, 0.5),
    /share identity/,
  );
});

test("radial values match the shared Dart reference fixture", () => {
  const radialRecord = { ...music, visualBands: Array(64).fill(0.5) };
  const scene = evaluate(
    config("radial", { timeSeconds: 1 }),
    "live",
    radialRecord,
  );
  assert.equal(scene.spokes?.length, 64);
  assert.equal(scene.rings?.length, 3);
  assert.ok(Math.abs(scene.spokes![0].outer - 0.2385) < 1e-12);
  assert.ok(Math.abs(scene.spokes![0].opacity - 0.49375) < 1e-12);
  assert.ok(Math.abs(scene.rings![0].radius - 0.1040625) < 1e-12);
  assert.ok(Math.abs(scene.field! - 0.1325) < 1e-12);
});

test("particles are deterministic across evaluation order", () => {
  const c = config("particles", {
    seed: 42,
    particleCount: 128,
    timeSeconds: 7.25,
  });
  const first = evaluate(c, "live", music).particles;
  evaluate(config("particles", { seed: 42, timeSeconds: 1 }), "live", music);
  const second = evaluate(c, "live", music).particles;
  assert.deepEqual(second, first);
  assert.equal(first?.length, 128);
  assert.ok(first![0].x >= 0 && first![0].x <= 1);
});

test("particle coordinates follow the specified uint32 hash vectors", () => {
  const particle = evaluate(
    config("particles", {
      motion: "reduced",
      seed: 0,
      particleCount: 1,
    }),
    "live",
    music,
  ).particles![0];
  assert.ok(Math.abs(particle.x - 0.8475585277192295) < 1e-15);
  assert.ok(Math.abs(particle.y - 0.1974032109137625) < 1e-15);
});

test("paused scenes retain the last supplied transformed geometry", () => {
  const scene = evaluate(
    config("particles", {
      motion: "full",
      seed: 42,
      particleCount: 8,
      timeSeconds: 1.25,
    }),
    "paused",
    music,
  );
  assert.equal(scene.status, "paused");
  assert.equal(scene.label, "Audio visualization paused · Procedural audio particles");
  assert.equal(scene.particles?.length, 8);
});

test("status and feature mismatches stay explicit", () => {
  assert.equal(
    evaluate(config("bars"), "paused", music).label,
    "Audio visualization paused · Audio spectrum bars",
  );
  assert.equal(
    evaluate(config("bars", { featureMask: 3 }), "live", music).status,
    "unavailable",
  );
  assert.equal(
    evaluate(config("bars"), "off", music).label,
    "Visualization off",
  );
});
