import { createHash } from "node:crypto";
import { lstat, readFile } from "node:fs/promises";
import { join, relative, resolve, sep } from "node:path";
import type { RecordFixture } from "./scene.ts";

const sha256 = (bytes: Uint8Array) =>
  createHash("sha256").update(bytes).digest("hex");
const u32 = (b: Buffer, o: number) => b.readUInt32LE(o);
const u64 = (b: Buffer, o: number) => b.readBigUInt64LE(o);
const digestAt = (b: Buffer, o: number) =>
  b.subarray(o, o + 32).toString("hex");
function descriptor(b: Buffer, at: number) {
  const pathBytes = b.subarray(at + 64, at + 128),
    zero = pathBytes.indexOf(0);
  if (zero < 1 || pathBytes.subarray(zero).some((v) => v !== 0))
    throw new Error("Invalid sidecar descriptor path.");
  const path = pathBytes.subarray(0, zero).toString("ascii");
  if (path.startsWith("/") || path.includes("..") || path.includes("\\"))
    throw new Error("Unsafe sidecar descriptor path.");
  return {
    index: u64(b, at),
    first: u64(b, at + 8),
    count: u32(b, at + 16),
    bytes: u64(b, at + 24),
    digest: digestAt(b, at + 32),
    path,
  };
}
async function safeRead(
  root: string,
  rel: string,
  maxBytes: number,
): Promise<Buffer> {
  const absolute = resolve(root, rel),
    back = relative(root, absolute);
  if (
    !back ||
    back.startsWith(`..${sep}`) ||
    back === ".." ||
    resolve(root) === absolute
  )
    throw new Error("Sidecar path escaped its root.");
  const info = await lstat(absolute);
  if (!info.isFile() || info.isSymbolicLink() || info.size > maxBytes)
    throw new Error("Sidecar entry is linked, nonregular, or over budget.");
  return readFile(absolute);
}

/** Random-access reader for the draft analysis-v2 package. Admission checks the full index digest;
 * each page and requested chunk is independently checked against its authenticated descriptor.
 */
export class FixtureSidecarReader {
  private readonly root: string;
  readonly metadata: any;
  private readonly index: Buffer;
  private readonly pageCache = new Map<string, Buffer>();
  private readonly chunkCache = new Map<string, Buffer>();
  private constructor(root: string, metadata: any, index: Buffer) {
    this.root = root;
    this.metadata = metadata;
    this.index = index;
  }
  static async open(
    root: string,
    expected: {
      inputSha256: string;
      featureMask: number;
      cacheKey: string;
      metadataSha256: string;
      pageIndexSha256: string;
      transformDigest: string;
    },
  ): Promise<FixtureSidecarReader> {
    const absolute = resolve(root),
      rootInfo = await lstat(absolute);
    if (!rootInfo.isDirectory() || rootInfo.isSymbolicLink())
      throw new Error("Sidecar root must be a real directory.");
    const metadataBytes = await safeRead(absolute, "metadata.json", 65536);
    const metadata = JSON.parse(metadataBytes.toString("utf8"));
    const checks = [
      metadata.schema_version === 2,
      metadata.feature_mask === expected.featureMask,
      metadata.input_sha256 === expected.inputSha256,
      metadata.source_identity === expected.inputSha256,
      metadata.cache_key === expected.cacheKey,
      metadata.transform_digest === expected.transformDigest,
      metadata.page_index_sha256 === expected.pageIndexSha256,
      sha256(metadataBytes) === expected.metadataSha256,
      metadata.hop_frames === 480,
      metadata.sample_rate === 48000,
      metadata.channels === 2,
      Number.isSafeInteger(metadata.sample_count) && metadata.sample_count >= 1,
      Number.isSafeInteger(metadata.feature_count) &&
        metadata.feature_count >= 1,
      Number.isSafeInteger(metadata.chunk_count) &&
        Number.isSafeInteger(metadata.page_count),
      metadata.chunk_records === 100,
      metadata.chunks_per_page === 256,
      metadata.page_index_path === "pages.sgai",
      metadata.feature_count === Math.ceil(metadata.sample_count / 480),
      metadata.chunk_count === Math.ceil(metadata.feature_count / 100),
      metadata.page_count === Math.ceil(metadata.chunk_count / 256),
      metadata.record_bytes ===
        (metadata.feature_mask === 11
          ? 6492
          : metadata.feature_mask === 5
            ? 88
            : metadata.feature_mask === 3
              ? 2136
              : 4444),
    ];
    const failed = checks.findIndex((ok) => !ok);
    if (failed !== -1)
      throw new Error(`Sidecar metadata check ${failed} failed.`);
    if (
      !/^[0-9a-f]{64}$/.test(metadata.page_index_sha256) ||
      !/^[0-9a-f]{64}$/.test(metadata.transform_digest)
    )
      throw new Error("Sidecar metadata digest is malformed.");
    const index = await safeRead(
      absolute,
      "pages.sgai",
      64 + metadata.page_count * 128,
    );
    if (
      index.length !== 64 + metadata.page_count * 128 ||
      sha256(index) !== metadata.page_index_sha256 ||
      index.subarray(0, 8).toString("ascii") !== "SGAVI002" ||
      u32(index, 8) !== 2 ||
      u32(index, 12) !== 128 ||
      u64(index, 16) !== 0n ||
      u32(index, 24) !== metadata.page_count ||
      u32(index, 28) !== 0 ||
      u64(index, 32) !== BigInt(metadata.page_count * 128)
    )
      throw new Error("Sidecar page index failed validation.");
    return new FixtureSidecarReader(absolute, metadata, index);
  }
  async readRecord(index: number): Promise<RecordFixture> {
    if (
      !Number.isSafeInteger(index) ||
      index < 0 ||
      index >= this.metadata.feature_count
    )
      throw new RangeError("Record index is outside the sidecar.");
    const ci = Math.floor(index / 100),
      pi = Math.floor(ci / 256),
      local = ci % 256;
    const pd = descriptor(this.index, 64 + pi * 128);
    if (
      pd.index !== BigInt(pi) ||
      pd.first !== BigInt(pi * 256) ||
      pd.path !== `pages/page-${pi.toString(16).padStart(16, "0")}.sgip` ||
      pd.count < local + 1 ||
      pd.bytes !== BigInt(64 + pd.count * 128)
    )
      throw new Error("Page descriptor does not match its index.");
    let page = this.pageCache.get(pd.path);
    if (!page) {
      page = await safeRead(this.root, pd.path, Number(pd.bytes));
      this.pageCache.set(pd.path, page);
    }
    if (
      BigInt(page.length) !== pd.bytes ||
      sha256(page) !== pd.digest ||
      page.subarray(0, 8).toString("ascii") !== "SGAVP002" ||
      u32(page, 8) !== 2 ||
      u32(page, 12) !== 128 ||
      u64(page, 16) !== pd.first ||
      u32(page, 24) !== pd.count ||
      u32(page, 28) !== 0 ||
      u64(page, 32) !== BigInt(pd.count * 128)
    )
      throw new Error("Sidecar page failed validation.");
    const cd = descriptor(page, 64 + local * 128),
      chunkFirst = ci * 100;
    const recordBytes =
      this.metadata.feature_mask === 11
        ? 6492
        : this.metadata.feature_mask === 5
          ? 88
          : this.metadata.feature_mask === 3
            ? 2136
            : 4444;
    const count = Math.min(100, this.metadata.feature_count - chunkFirst),
      payloadBytes = count * recordBytes;
    if (
      cd.index !== BigInt(ci) ||
      cd.first !== BigInt(chunkFirst) ||
      cd.count !== count ||
      cd.bytes !== BigInt(64 + payloadBytes) ||
      cd.path !== `chunks/chunk-${ci.toString(16).padStart(16, "0")}.sgav`
    )
      throw new Error("Chunk descriptor does not match its page.");
    let chunk = this.chunkCache.get(cd.path);
    if (!chunk) {
      chunk = await safeRead(this.root, cd.path, Number(cd.bytes));
      this.chunkCache.set(cd.path, chunk);
    }
    const profile =
      this.metadata.feature_mask === 3 || this.metadata.feature_mask === 11
        ? 1
        : 2;
    const expectedCenter = BigInt(chunkFirst) * 480n;
    if (
      BigInt(chunk.length) !== cd.bytes ||
      sha256(chunk) !== cd.digest ||
      chunk.subarray(0, 8).toString("ascii") !== "SGAVC002" ||
      u32(chunk, 8) !== 2 ||
      u32(chunk, 12) !== recordBytes ||
      u64(chunk, 16) !== BigInt(chunkFirst) ||
      u32(chunk, 24) !== count ||
      u32(chunk, 28) !== this.metadata.feature_mask ||
      u32(chunk, 32) !== profile ||
      u64(chunk, 40) !== expectedCenter ||
      u64(chunk, 48) !== BigInt(payloadBytes) ||
      u32(chunk, 56) !== 64 ||
      u32(chunk, 60) !== 0
    )
      throw new Error("Sidecar chunk failed validation.");
    const at = 64 + (index - chunkFirst) * recordBytes,
      bytes = chunk.subarray(at, at + recordBytes);
    const sequence = u64(bytes, 0),
      center = u64(bytes, 8),
      continuity = u64(bytes, 16),
      valid = u32(bytes, 24),
      envBins = u32(bytes, 28);
    if (
      sequence !== BigInt(index + 1) ||
      center !== BigInt(index) * 480n ||
      continuity !== 1n ||
      valid !== Math.min(480, this.metadata.sample_count - index * 480) ||
      envBins !== (this.metadata.feature_mask & 2 ? 64 : 0) ||
      u32(bytes, 32) !== 0 ||
      u32(bytes, 36) !== 0
    )
      throw new Error("Sidecar visual record failed identity checks.");
    let offset = 88;
    const floats = (start: number, n: number) =>
      Array.from({ length: n }, (_, i) => bytes.readFloatLE(start + i * 4));
    const physicalLevels = floats(40, 6),
      visualLevels = floats(64, 6);
    let physicalEnvelope: number[] | undefined,
      visualEnvelope: number[] | undefined;
    let physicalSpectrum: number[] | undefined,
      visualBands: number[] | undefined;
    if (this.metadata.feature_mask & 2) {
      physicalEnvelope = floats(offset, 256);
      visualEnvelope = floats(offset + 1024, 256);
      offset += 2048;
    }
    if (this.metadata.feature_mask & 8) {
      physicalSpectrum = floats(offset, 1025);
      offset += 4100;
      visualBands = floats(offset, 64);
    }
    return {
      streamId: `offline-${this.metadata.cache_key.slice(0, 16)}`,
      sourceIdentity: this.metadata.input_sha256,
      transformDigest: this.metadata.transform_digest,
      originFrame: 0n,
      continuityId: continuity,
      centerFrame: center,
      sequence,
      validFrames: valid,
      envelopeBins: envBins,
      profile: profile === 1 ? "music" : "speech",
      featureMask: this.metadata.feature_mask,
      physicalLevels,
      visualLevels,
      physicalEnvelope,
      visualEnvelope,
      physicalSpectrum,
      visualBands,
    };
  }
}
