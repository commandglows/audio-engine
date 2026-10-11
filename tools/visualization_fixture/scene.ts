/** Stateless TypeScript mirror of the shared Dart scene evaluator.
 * This private fixture harness consumes already-transformed records; it does
 * not implement audio analysis or smoothing.
 */
export type Mode = "bars" | "musicWave" | "speechWave" | "radial" | "particles";
export type Status =
  | "live"
  | "warmingUp"
  | "paused"
  | "ended"
  | "gap"
  | "unavailable"
  | "failed"
  | "off";
export type Motion = "full" | "reduced" | "static";
export type Role = "primary" | "secondary" | "accent";

export interface RecordFixture {
  streamId: string;
  sourceIdentity: string;
  transformDigest: string;
  originFrame: bigint;
  continuityId: bigint;
  centerFrame: bigint;
  sequence: bigint;
  validFrames: number;
  envelopeBins: number;
  physicalLevels?: readonly number[];
  physicalEnvelope?: readonly number[];
  physicalSpectrum?: readonly number[];
  profile: "music" | "speech";
  featureMask: number;
  visualLevels: readonly number[];
  visualEnvelope?: readonly number[];
  visualBands?: readonly number[];
}
export interface SpeechFixture {
  streamId: string;
  sourceIdentity: string;
  transformDigest?: string;
  originFrame: bigint;
  continuityId: bigint;
  endpointCenterFrame: bigint;
  validSuffixCount: number;
  physicalRms: readonly number[];
  physicalPeak: readonly number[];
  visualRms: readonly number[];
  visualPeak: readonly number[];
}
export interface Config {
  mode: Mode;
  profile: "music" | "speech";
  featureMask: number;
  transformDigest: string;
  intensity: number;
  motion: Motion;
  seed?: number;
  particleCount?: number;
  timeSeconds?: number;
  energySource?: "levels" | "spectrum";
  spokeCount?: number;
  ringCount?: number;
}
export interface Scene {
  status: Status;
  label: string;
  intensity: number;
  validSuffixCount: number;
  baseline?: boolean;
  bars?: { index: number; x: number; height: number; role: Role }[];
  musicWave?: {
    index: number;
    x: number;
    leftMin: number;
    leftMax: number;
    rightMin: number;
    rightMax: number;
  }[];
  speechWave?: {
    index: number;
    x: number;
    rms: number;
    peak: number;
    valid: boolean;
  }[];
  field?: number;
  spokes?: {
    index: number;
    angle: number;
    inner: number;
    outer: number;
    opacity: number;
    role: Role;
  }[];
  rings?: { index: number; radius: number; role: Role }[];
  particles?: {
    index: number;
    x: number;
    y: number;
    radius: number;
    opacity: number;
    role: Role;
  }[];
}

const role = (i: number): Role =>
  (["primary", "secondary", "accent"] as const)[i % 3];
const clamp = (n: number): number => Math.max(0, Math.min(1, n));
const statusLabel: Record<Status, string> = {
  live: "Audio visualization live",
  warmingUp: "Audio visualization warming up",
  paused: "Audio visualization paused",
  ended: "Audio visualization ended",
  gap: "Audio signal gap",
  unavailable: "Audio visualization unavailable",
  failed: "Audio visualization failed",
  off: "Visualization off",
};
const blank = (
  status: Status,
  label: string,
  intensity: number,
  validSuffixCount = 0,
): Scene => ({ status, label, intensity, validSuffixCount });

/** Port of SpeechWindow.fromHistory: retain only the adjacent identity-matched suffix. */
export function prepareSpeechWindow(
  records: readonly RecordFixture[],
  endpointCenterFrame: bigint,
): SpeechFixture {
  const endpoints = records
    .filter(
      (r) => r.profile === "speech" && r.centerFrame === endpointCenterFrame,
    )
    .sort((a, b) =>
      a.sequence < b.sequence ? -1 : a.sequence > b.sequence ? 1 : 0,
    );
  if (!endpoints.length)
    throw new RangeError("The requested endpoint is not retained.");
  const endpoint = endpoints[endpoints.length - 1];
  const matching = records.filter(
    (r) =>
      sameIdentity(r, endpoint) &&
      r.profile === "speech" &&
      r.centerFrame <= endpointCenterFrame,
  );
  const byCenter = new Map<bigint, RecordFixture>();
  for (const r of matching) {
    if (byCenter.has(r.centerFrame))
      throw new RangeError("Duplicate speech center in retained history.");
    byCenter.set(r.centerFrame, r);
  }
  const suffix: RecordFixture[] = [];
  let center = endpointCenterFrame,
    previous: RecordFixture | undefined;
  while (suffix.length < 150) {
    const r = byCenter.get(center);
    if (
      !r ||
      !sameIdentity(r, endpoint) ||
      (previous && previous.sequence - r.sequence !== 1n)
    )
      break;
    suffix.push(r);
    previous = r;
    if (center < 480n) break;
    center -= 480n;
  }
  const ordered = suffix.reverse(),
    start = 150 - ordered.length;
  const physicalRms = Array(150).fill(0),
    physicalPeak = Array(150).fill(0);
  const visualRms = Array(150).fill(0),
    visualPeak = Array(150).fill(0);
  ordered.forEach((r, i) => {
    if (
      !r.physicalLevels ||
      r.physicalLevels.length !== 6 ||
      r.visualLevels.length !== 6
    )
      throw new RangeError("Speech levels are malformed.");
    const at = start + i;
    physicalRms[at] = r.physicalLevels[5];
    physicalPeak[at] = r.physicalLevels[2];
    visualRms[at] = r.visualLevels[5];
    visualPeak[at] = r.visualLevels[2];
  });
  return Object.freeze({
    streamId: endpoint.streamId,
    sourceIdentity: endpoint.sourceIdentity,
    transformDigest: endpoint.transformDigest,
    originFrame: endpoint.originFrame,
    continuityId: endpoint.continuityId,
    endpointCenterFrame,
    validSuffixCount: ordered.length,
    physicalRms: Object.freeze(physicalRms),
    physicalPeak: Object.freeze(physicalPeak),
    visualRms: Object.freeze(visualRms),
    visualPeak: Object.freeze(visualPeak),
  });
}

/** Port of SpeechWindow.interpolateWith; only the shared contiguous suffix is mixed. */
export function interpolateSpeechWindow(
  left: SpeechFixture,
  right: SpeechFixture,
  alpha: number,
): SpeechFixture {
  if (!Number.isFinite(alpha) || alpha < 0 || alpha > 1)
    throw new RangeError("Interpolation alpha must be in [0, 1].");
  if (
    left.streamId !== right.streamId ||
    left.sourceIdentity !== right.sourceIdentity ||
    left.transformDigest !== right.transformDigest ||
    left.continuityId !== right.continuityId ||
    left.originFrame !== right.originFrame ||
    right.endpointCenterFrame - left.endpointCenterFrame !== 480n
  )
    throw new RangeError("Speech windows must be adjacent and share identity.");
  if (alpha === 0) return left;
  if (alpha === 1) return right;
  const validSuffixCount = Math.min(
      left.validSuffixCount,
      right.validSuffixCount,
    ),
    first = 150 - validSuffixCount;
  const mix = (a: readonly number[], b: readonly number[]) =>
    Object.freeze(
      Array.from({ length: 150 }, (_, i) =>
        i < first ? 0 : a[i] + (b[i] - a[i]) * alpha,
      ),
    );
  return Object.freeze({
    ...left,
    endpointCenterFrame: left.endpointCenterFrame,
    validSuffixCount,
    physicalRms: mix(left.physicalRms, right.physicalRms),
    physicalPeak: mix(left.physicalPeak, right.physicalPeak),
    visualRms: mix(left.visualRms, right.visualRms),
    visualPeak: mix(left.visualPeak, right.visualPeak),
  });
}

function sameIdentity(a: RecordFixture, b: RecordFixture): boolean {
  return (
    a.streamId === b.streamId &&
    a.sourceIdentity === b.sourceIdentity &&
    a.transformDigest === b.transformDigest &&
    a.profile === b.profile &&
    a.featureMask === b.featureMask &&
    a.originFrame === b.originFrame &&
    a.continuityId === b.continuityId
  );
}

/** Stateless counterpart of VisualRecord.interpolateWith for one 480-frame bracket. */
export function interpolateRecord(
  left: RecordFixture,
  right: RecordFixture,
  alpha: number,
): RecordFixture {
  if (!Number.isFinite(alpha) || alpha < 0 || alpha > 1)
    throw new RangeError("Interpolation alpha must be in [0, 1].");
  if (
    !sameIdentity(left, right) ||
    right.centerFrame - left.centerFrame !== 480n ||
    right.sequence - left.sequence !== 1n
  )
    throw new RangeError("Records must be adjacent and share identity.");
  if (alpha === 0) return left;
  if (alpha === 1) return right;
  const mix = (a?: readonly number[], b?: readonly number[]) => {
    if (a === undefined && b === undefined) return undefined;
    if (!a || !b || a.length !== b.length)
      throw new RangeError("Record features differ across the bracket.");
    return a.map((v, i) => v + (b[i] - v) * alpha);
  };
  const physicalLevels = mix(left.physicalLevels, right.physicalLevels);
  if (physicalLevels) {
    physicalLevels[2] = Math.max(physicalLevels[0], physicalLevels[1]);
    physicalLevels[5] = Math.sqrt(
      (physicalLevels[3] ** 2 + physicalLevels[4] ** 2) / 2,
    );
  }
  return Object.freeze({
    ...left,
    sequence: right.sequence,
    centerFrame: left.centerFrame,
    physicalLevels: physicalLevels && Object.freeze(physicalLevels),
    visualLevels: Object.freeze(mix(left.visualLevels, right.visualLevels)!),
    physicalEnvelope: freeze(
      mix(left.physicalEnvelope, right.physicalEnvelope),
    ),
    visualEnvelope: freeze(mix(left.visualEnvelope, right.visualEnvelope)),
    physicalSpectrum: freeze(
      mix(left.physicalSpectrum, right.physicalSpectrum),
    ),
    visualBands: freeze(mix(left.visualBands, right.visualBands)),
  });
}

function freeze<T extends number[] | undefined>(values: T): T {
  return (values && Object.freeze(values)) as T;
}

export function evaluate(
  config: Config,
  status: Status,
  record?: RecordFixture,
  speech?: SpeechFixture,
): Scene {
  const intensity = config.intensity;
  if (status === "off") return blank(status, "Visualization off", intensity);
  if (intensity === 0)
    return baseline(config, status, speech?.validSuffixCount ?? 0);
  if ((status !== "live" && status !== "paused") || !record)
    return blank(
      status,
      statusLabel[status],
      intensity,
      speech?.validSuffixCount ?? 0,
    );
  if (
    record.profile !== config.profile ||
    record.featureMask !== config.featureMask ||
    record.transformDigest !== config.transformDigest
  )
    return blank("unavailable", "Visualization data unavailable", intensity);
  const unavailable = (): Scene =>
    blank("unavailable", "Requested audio feature unavailable", intensity);
  const label = (value: string) =>
    status === "paused" ? `Audio visualization paused · ${value}` : value;
  switch (config.mode) {
    case "bars": {
      if (!record.visualBands) return unavailable();
      return {
        ...blank(status, label("Audio spectrum bars"), intensity),
        bars: record.visualBands.map((v, i) => ({
          index: i,
          x: (i + 0.5) / 64,
          height: intensity * v,
          role: role(i),
        })),
      };
    }
    case "musicWave": {
      if (!record.visualEnvelope) return unavailable();
      const scale = 0.2 * intensity;
      return {
        ...blank(
          status,
          label("Music waveform, 42.7 millisecond oscilloscope"),
          intensity,
        ),
        musicWave: Array.from({ length: 64 }, (_, i) => {
          const o = i * 4,
            e = record.visualEnvelope!;
          return {
            index: i,
            x: (i + 0.5) / 64,
            leftMin: 0.25 - scale * e[o],
            leftMax: 0.25 - scale * e[o + 1],
            rightMin: 0.75 - scale * e[o + 2],
            rightMax: 0.75 - scale * e[o + 3],
          };
        }),
      };
    }
    case "speechWave": {
      if (
        !speech ||
        speech.streamId !== record.streamId ||
        speech.sourceIdentity !== record.sourceIdentity ||
        speech.originFrame !== record.originFrame ||
        speech.continuityId !== record.continuityId ||
        speech.endpointCenterFrame !== record.centerFrame ||
        speech.validSuffixCount < 0 ||
        speech.validSuffixCount > 150 ||
        speech.visualRms.length !== 150 ||
        speech.visualPeak.length !== 150
      )
        return unavailable();
      const first = 150 - speech.validSuffixCount;
      return {
        ...blank(
          status,
          label("Speech envelope, 1.5 second window"),
          intensity,
          speech.validSuffixCount,
        ),
        speechWave: Array.from({ length: 150 }, (_, i) => ({
          index: i,
          x: (i + 0.5) / 150,
          rms: 0.35 * intensity * speech.visualRms[i],
          peak: 0.35 * intensity * speech.visualPeak[i],
          valid: i >= first,
        })),
      };
    }
    case "radial": {
      if (!record.visualBands) return unavailable();
      const level = clamp(record.visualLevels[5]),
        time = config.motion === "full" ? (config.timeSeconds ?? 0) : 0;
      const spokes = config.spokeCount ?? 64,
        rings = config.ringCount ?? 3;
      return {
        ...blank(status, label("Immersive radial audio scene"), intensity),
        field: 0.1 + 0.2 * intensity * level,
        spokes: Array.from({ length: spokes }, (_, i) => {
          const e = clamp(record.visualBands![i]);
          return {
            index: i,
            angle: (2 * Math.PI * i) / spokes,
            inner: 0.18,
            outer: 0.18 + 0.18 * intensity * e,
            opacity: 0.25 + 0.75 * intensity * e,
            role: role(i),
          };
        }),
        rings: Array.from({ length: rings }, (_, i) => ({
          index: i,
          radius: Math.max(
            0.05,
            Math.min(
              0.45,
              0.1 +
                0.07 * i +
                (config.motion === "full"
                  ? 0.025 *
                    intensity *
                    level *
                    Math.sin((2 * Math.PI * time) / 4 + (2 * Math.PI * i) / 3)
                  : 0),
            ),
          ),
          role: role(i),
        })),
      };
    }
    case "particles": {
      if (config.energySource === "spectrum" && !record.visualBands)
        return unavailable();
      const time = config.motion === "full" ? (config.timeSeconds ?? 0) : 0,
        rms = clamp(record.visualLevels[5]);
      return {
        ...blank(status, label("Procedural audio particles"), intensity),
        particles: Array.from({ length: config.particleCount ?? 128 }, (_, i) =>
          particle(
            config,
            i,
            config.energySource === "spectrum"
              ? clamp(record.visualBands![i % 64])
              : rms,
            time,
          ),
        ),
      };
    }
  }
}

function baseline(c: Config, status: Status, validSuffixCount: number): Scene {
  const base = blank(status, "intensity_zero", 0, validSuffixCount);
  base.baseline = true;
  if (c.mode === "bars")
    base.bars = Array.from({ length: 64 }, (_, i) => ({
      index: i,
      x: (i + 0.5) / 64,
      height: 0,
      role: role(i),
    }));
  if (c.mode === "musicWave")
    base.musicWave = Array.from({ length: 64 }, (_, i) => ({
      index: i,
      x: (i + 0.5) / 64,
      leftMin: 0.25,
      leftMax: 0.25,
      rightMin: 0.75,
      rightMax: 0.75,
    }));
  if (c.mode === "speechWave")
    base.speechWave = Array.from({ length: 150 }, (_, i) => ({
      index: i,
      x: (i + 0.5) / 150,
      rms: 0,
      peak: 0,
      valid: true,
    }));
  if (c.mode === "radial") {
    base.field = 0.1;
    base.spokes = Array.from({ length: c.spokeCount ?? 64 }, (_, i) => ({
      index: i,
      angle: (2 * Math.PI * i) / (c.spokeCount ?? 64),
      inner: 0.18,
      outer: 0.18,
      opacity: 0.25,
      role: role(i),
    }));
    base.rings = Array.from({ length: c.ringCount ?? 3 }, (_, i) => ({
      index: i,
      radius: 0.1 + 0.07 * i,
      role: role(i),
    }));
  }
  if (c.mode === "particles")
    base.particles = Array.from({ length: c.particleCount ?? 128 }, (_, i) =>
      particle(c, i, 0, 0, true),
    );
  return base;
}
function hash32(value: number): number {
  let h = (Math.imul(value >>> 0, 1664525) + 1013904223) >>> 0;
  h = (h ^ (h >>> 16)) >>> 0;
  h = (Math.imul(h, 22695477) + 1) >>> 0;
  return (h ^ (h >>> 13)) >>> 0;
}
function particle(
  c: Config,
  i: number,
  energy: number,
  time: number,
  zero = false,
) {
  const seed = (c.seed ?? 0) >>> 0,
    phi = hash32(seed ^ ((i + 1) >>> 0)) / 4294967296;
  const full = c.motion === "full" && !zero,
    z = full ? Math.floor(time / 2 + phi) : 0,
    q = full ? time / 2 + phi - z : phi;
  const h = hash32(seed ^ Math.imul(i, 0x9e3779b9) ^ z),
    u = hash32(h ^ 1) / 4294967296,
    v = hash32(h ^ 2) / 4294967296;
  const influence = zero ? 0 : c.intensity * energy;
  if (full) {
    const pulse = Math.sin(Math.PI * q),
      x = (((u + (v - 0.5) * 0.12 * q) % 1) + 1) % 1;
    return {
      index: i,
      x,
      y: 1 - q,
      radius: (0.002 + 0.006 * influence) * pulse,
      opacity: influence * pulse * pulse,
      role: role(i),
    };
  }
  return {
    index: i,
    x: u,
    y: 1 - phi,
    radius: zero ? 0.002 : 0.002 + 0.004 * influence,
    opacity: zero ? 0 : influence,
    role: role(i),
  };
}
