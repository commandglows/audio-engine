// Generates ORIGINAL synthetic sine-tone fixtures for the Windows MF decoder
// trials. Usage: shipglows_fixture_gen <output-dir>
//
// WAV files are written directly as RIFF/WAVE. AAC, ALAC and MP3 are encoded
// by the Windows Media Foundation encoder MFTs through IMFSinkWriter. When an
// encoder is not present on the machine, the fixture is recorded as
// "not_generated" with the HRESULT; no substitute bytes are written.
// Results are written to <output-dir>/generated.tsv.

#include "fixture_specs.h"

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <numbers>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using shipglows_audio::trials::Build;
using shipglows_audio::trials::Spec;

namespace {

std::string Hex(HRESULT hr) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
  return buffer;
}

// Original tone: sine with a 10 ms raised fade at both ends to avoid clicks.
// Output is interleaved float in [-0.5, 0.5]. Channel 0 uses freq_left and
// channel 1 uses freq_right; mono uses freq_left only.
std::vector<float> SynthesizeTone(const Spec& spec) {
  const auto frames = static_cast<std::size_t>(spec.seconds * spec.sample_rate);
  const std::size_t channels = spec.channels;
  const std::size_t fade = spec.sample_rate / 100;  // 10 ms
  std::vector<float> data(frames * channels);
  for (std::size_t i = 0; i < frames; ++i) {
    double envelope = 1.0;
    if (i < fade) envelope = 0.5 - 0.5 * std::cos(std::numbers::pi * i / fade);
    if (frames - 1 - i < fade)
      envelope = 0.5 - 0.5 * std::cos(std::numbers::pi * (frames - 1 - i) / fade);
    for (std::size_t c = 0; c < channels; ++c) {
      const double hz = (c % 2 == 0) ? spec.freq_left_hz : spec.freq_right_hz;
      const double t = static_cast<double>(i) / spec.sample_rate;
      data[i * channels + c] = static_cast<float>(
          shipglows_audio::trials::kToneAmplitude * envelope *
          std::sin(2.0 * std::numbers::pi * hz * t));
    }
  }
  return data;
}

void PutLe16(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void PutLe32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  PutLe16(out, v & 0xFFFF);
  PutLe16(out, v >> 16);
}

// Writes a canonical RIFF/WAVE file. format tag 1 = PCM, 3 = IEEE float.
bool WriteWav(const fs::path& path, const std::vector<float>& samples,
              std::uint16_t channels, std::uint32_t sample_rate,
              std::uint16_t bits, bool float_samples, std::string& detail) {
  const std::uint32_t bytes_per_sample = bits / 8;
  const std::uint32_t data_bytes =
      static_cast<std::uint32_t>(samples.size() * bytes_per_sample);
  std::vector<std::uint8_t> out;
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  PutLe32(out, 36 + data_bytes);
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  PutLe32(out, 16);
  PutLe16(out, float_samples ? 3 : 1);
  PutLe16(out, channels);
  PutLe32(out, sample_rate);
  PutLe32(out, sample_rate * channels * bytes_per_sample);
  PutLe16(out, static_cast<std::uint16_t>(channels * bytes_per_sample));
  PutLe16(out, bits);
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  PutLe32(out, data_bytes);
  for (const float s : samples) {
    const double x = std::clamp(static_cast<double>(s), -1.0, 1.0);
    if (float_samples) {
      std::uint32_t raw = 0;
      std::memcpy(&raw, &s, 4);
      PutLe32(out, raw);
    } else if (bits == 8) {
      out.push_back(static_cast<std::uint8_t>(std::lround(x * 127.0) + 128));
    } else if (bits == 16) {
      PutLe16(out, static_cast<std::uint16_t>(
                       static_cast<std::int16_t>(std::lround(x * 32767.0))));
    } else if (bits == 24) {
      const auto v = static_cast<std::int32_t>(std::lround(x * 8388607.0));
      out.push_back(static_cast<std::uint8_t>(v));
      out.push_back(static_cast<std::uint8_t>(v >> 8));
      out.push_back(static_cast<std::uint8_t>(v >> 16));
    } else if (bits == 32) {
      const auto v = static_cast<std::int32_t>(std::llround(x * 2147483647.0));
      PutLe32(out, static_cast<std::uint32_t>(v));
    } else {
      detail = "unsupported_bits";
      return false;
    }
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    detail = "cannot_open_output";
    return false;
  }
  file.write(reinterpret_cast<const char*>(out.data()),
             static_cast<std::streamsize>(out.size()));
  return static_cast<bool>(file);
}

// Encodes interleaved float tone data to an MP4 (.m4a) or MP3 file through
// IMFSinkWriter. The input is 16-bit PCM; the encoder is chosen by MF.
HRESULT EncodeWithSinkWriter(const fs::path& path, const GUID& output_subtype,
                             std::uint32_t sample_rate, std::uint32_t channels,
                             std::uint32_t avg_bytes_per_second,
                             const std::vector<float>& samples,
                             std::string& detail) {
  ComPtr<IMFSinkWriter> writer;
  HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer);
  if (FAILED(hr)) {
    detail = "MFCreateSinkWriterFromURL " + Hex(hr);
    return hr;
  }
  ComPtr<IMFMediaType> output;
  hr = MFCreateMediaType(&output);
  if (FAILED(hr)) return hr;
  output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  output->SetGUID(MF_MT_SUBTYPE, output_subtype);
  output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
  output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
  output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
  if (avg_bytes_per_second)
    output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, avg_bytes_per_second);
  DWORD stream = 0;
  hr = writer->AddStream(output.Get(), &stream);
  if (FAILED(hr)) {
    detail = "AddStream " + Hex(hr);
    return hr;
  }
  ComPtr<IMFMediaType> input;
  hr = MFCreateMediaType(&input);
  if (FAILED(hr)) return hr;
  input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
  input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
  input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sample_rate);
  input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
  input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
  input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sample_rate * channels * 2);
  input->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  hr = writer->SetInputMediaType(stream, input.Get(), nullptr);
  if (FAILED(hr)) {
    detail = "SetInputMediaType " + Hex(hr);
    return hr;
  }
  hr = writer->BeginWriting();
  if (FAILED(hr)) {
    detail = "BeginWriting " + Hex(hr);
    return hr;
  }
  // Feed one second of audio per sample so timestamps stay simple.
  const std::size_t frames_total = samples.size() / channels;
  const std::size_t frames_per_chunk = sample_rate;
  for (std::size_t first = 0; first < frames_total; first += frames_per_chunk) {
    const std::size_t frames = std::min(frames_per_chunk, frames_total - first);
    const DWORD bytes = static_cast<DWORD>(frames * channels * 2);
    ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(bytes, &buffer);
    if (FAILED(hr)) return hr;
    BYTE* dst = nullptr;
    hr = buffer->Lock(&dst, nullptr, nullptr);
    if (FAILED(hr)) return hr;
    for (std::size_t k = 0; k < frames * channels; ++k) {
      const double x = std::clamp(static_cast<double>(samples[first * channels + k]),
                                  -1.0, 1.0);
      const auto v = static_cast<std::int16_t>(std::lround(x * 32767.0));
      std::memcpy(dst + k * 2, &v, 2);
    }
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);
    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return hr;
    sample->AddBuffer(buffer.Get());
    const LONGLONG time = static_cast<LONGLONG>(first) * 10000000LL / sample_rate;
    const LONGLONG duration = static_cast<LONGLONG>(frames) * 10000000LL / sample_rate;
    sample->SetSampleTime(time);
    sample->SetSampleDuration(duration);
    hr = writer->WriteSample(stream, sample.Get());
    if (FAILED(hr)) {
      detail = "WriteSample " + Hex(hr);
      return hr;
    }
  }
  hr = writer->Finalize();
  if (FAILED(hr)) detail = "Finalize " + Hex(hr);
  return hr;
}

// Counts registered MFT encoders that output `subtype`; used to explain
// "not generated" results precisely.
UINT32 CountEncoders(const GUID& subtype) {
  MFT_REGISTER_TYPE_INFO output{MFMediaType_Audio, subtype};
  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  if (FAILED(MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_ALL, nullptr,
                       &output, &activates, &count)))
    return 0;
  for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
  CoTaskMemFree(activates);
  return count;
}

// Drains every pending output sample from `mft` into `bytes`.
HRESULT DrainOutput(IMFTransform* mft, const MFT_OUTPUT_STREAM_INFO& info,
                    std::vector<std::uint8_t>& bytes) {
  const bool provides = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                         MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
  for (;;) {
    ComPtr<IMFSample> own;
    if (!provides) {
      HRESULT hr = MFCreateSample(&own);
      if (FAILED(hr)) return hr;
      ComPtr<IMFMediaBuffer> buffer;
      hr = MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, 1u << 16), &buffer);
      if (FAILED(hr)) return hr;
      own->AddBuffer(buffer.Get());
    }
    MFT_OUTPUT_DATA_BUFFER out{};
    out.dwStreamID = 0;
    out.pSample = provides ? nullptr : own.Get();
    DWORD status = 0;
    const HRESULT hr = mft->ProcessOutput(0, 1, &out, &status);
    ComPtr<IMFSample> got;
    if (provides) got.Attach(out.pSample);
    else got = own;
    if (out.pEvents) out.pEvents->Release();
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return S_OK;
    if (FAILED(hr)) return hr;
    if (!got) continue;
    ComPtr<IMFMediaBuffer> contiguous;
    HRESULT copy = got->ConvertToContiguousBuffer(&contiguous);
    if (FAILED(copy)) return copy;
    BYTE* data = nullptr;
    DWORD length = 0;
    copy = contiguous->Lock(&data, nullptr, &length);
    if (FAILED(copy)) return copy;
    bytes.insert(bytes.end(), data, data + length);
    contiguous->Unlock();
  }
}

// Encodes with the registered MP3 encoder MFT directly and writes the raw
// MPEG-1 Layer III frames. MF has no MP3 file sink, so IMFSinkWriter cannot
// produce a .mp3 file (SetInputMediaType fails with MF_E_INVALIDMEDIATYPE).
HRESULT EncodeMp3Frames(const fs::path& path, std::uint32_t sample_rate,
                        std::uint32_t channels, std::uint32_t bitrate_bps,
                        const std::vector<float>& samples, std::string& detail) {
  MFT_REGISTER_TYPE_INFO output{MFMediaType_Audio, MFAudioFormat_MP3};
  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_ALL, nullptr,
                         &output, &activates, &count);
  if (FAILED(hr) || count == 0) {
    detail = "MFTEnumEx(MP3 encoder) " + Hex(hr) + " count=" + std::to_string(count);
    if (activates) CoTaskMemFree(activates);
    return FAILED(hr) ? hr : MF_E_NOT_FOUND;
  }
  ComPtr<IMFTransform> mft;
  hr = activates[0]->ActivateObject(IID_PPV_ARGS(&mft));
  for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
  CoTaskMemFree(activates);
  if (FAILED(hr)) {
    detail = "ActivateObject " + Hex(hr);
    return hr;
  }
  // This encoder requires the output type first (SetInputType before it
  // returns MF_E_TRANSFORM_TYPE_NOT_SET). Pick the MP3 type it advertises for
  // this rate, channel count and bitrate, then the matching PCM16 input.
  ComPtr<IMFMediaType> out_type;
  std::string offered;
  for (DWORD index = 0;; ++index) {
    ComPtr<IMFMediaType> candidate;
    if (FAILED(mft->GetOutputAvailableType(0, index, &candidate))) break;
    UINT32 rate = 0, chans = 0, avg = 0;
    candidate->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    candidate->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &chans);
    candidate->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &avg);
    offered += " [" + std::to_string(rate) + "Hz/" + std::to_string(chans) + "ch/" +
               std::to_string(avg * 8 / 1000) + "kbps]";
    if (rate == sample_rate && chans == channels && (!out_type || avg * 8 == bitrate_bps))
      out_type = candidate;
  }
  if (!out_type) {
    detail = "no_mp3_output_type_for_request offered:" + offered;
    return MF_E_INVALIDMEDIATYPE;
  }
  hr = mft->SetOutputType(0, out_type.Get(), 0);
  if (FAILED(hr)) {
    detail = "SetOutputType(MP3) " + Hex(hr) + " offered:" + offered;
    return hr;
  }
  ComPtr<IMFMediaType> in_type;
  for (DWORD index = 0;; ++index) {
    ComPtr<IMFMediaType> candidate;
    if (FAILED(mft->GetInputAvailableType(0, index, &candidate))) break;
    GUID subtype{};
    UINT32 rate = 0, chans = 0, bits = 0;
    candidate->GetGUID(MF_MT_SUBTYPE, &subtype);
    candidate->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    candidate->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &chans);
    candidate->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    if (subtype == MFAudioFormat_PCM && bits == 16 && rate == sample_rate &&
        chans == channels) {
      in_type = candidate;
      break;
    }
  }
  if (!in_type) {
    detail = "no_pcm16_input_type_for_request";
    return MF_E_INVALIDMEDIATYPE;
  }
  hr = mft->SetInputType(0, in_type.Get(), 0);
  if (FAILED(hr)) {
    detail = "SetInputType(PCM16) " + Hex(hr);
    return hr;
  }
  detail = "offered:" + offered;
  MFT_OUTPUT_STREAM_INFO info{};
  mft->GetOutputStreamInfo(0, &info);
  mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

  std::vector<std::uint8_t> bytes;
  const std::size_t frames_total = samples.size() / channels;
  const std::size_t chunk_frames = 4096;
  for (std::size_t first = 0; first < frames_total; first += chunk_frames) {
    const std::size_t frames = std::min(chunk_frames, frames_total - first);
    ComPtr<IMFMediaBuffer> buffer;
    MFCreateMemoryBuffer(static_cast<DWORD>(frames * channels * 2), &buffer);
    BYTE* dst = nullptr;
    buffer->Lock(&dst, nullptr, nullptr);
    for (std::size_t k = 0; k < frames * channels; ++k) {
      const double x = std::clamp(static_cast<double>(samples[first * channels + k]),
                                  -1.0, 1.0);
      const auto v = static_cast<std::int16_t>(std::lround(x * 32767.0));
      std::memcpy(dst + k * 2, &v, 2);
    }
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(frames * channels * 2));
    ComPtr<IMFSample> sample;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(static_cast<LONGLONG>(first) * 10000000LL / sample_rate);
    sample->SetSampleDuration(static_cast<LONGLONG>(frames) * 10000000LL / sample_rate);
    hr = mft->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
      hr = DrainOutput(mft.Get(), info, bytes);
      if (FAILED(hr)) break;
      hr = mft->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr)) {
      detail = "ProcessInput " + Hex(hr);
      return hr;
    }
    hr = DrainOutput(mft.Get(), info, bytes);
    if (FAILED(hr)) {
      detail = "ProcessOutput " + Hex(hr);
      return hr;
    }
  }
  mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
  mft->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
  hr = DrainOutput(mft.Get(), info, bytes);
  if (FAILED(hr)) {
    detail = "drain " + Hex(hr);
    return hr;
  }
  if (bytes.empty()) {
    detail = "encoder produced no bytes";
    return E_FAIL;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    detail = "write_failed";
    return E_FAIL;
  }
  detail = "mf_mp3_encoder_mft bytes=" + std::to_string(bytes.size());
  return S_OK;
}

struct Outcome {
  std::string status;  // "generated" or "not_generated"
  std::string detail;
};

Outcome GenerateOne(const Spec& spec, const fs::path& dir,
                    std::map<std::string, Outcome>& done) {
  const fs::path target = dir / spec.file;
  const auto base_outcome = [&]() -> const Outcome* {
    if (!spec.base) return nullptr;
    auto it = done.find(spec.base);
    return it == done.end() ? nullptr : &it->second;
  };
  switch (spec.build) {
    case Build::WavPcm:
    case Build::WavFloat:
    case Build::WavPcm6Channel: {
      std::string detail;
      const auto tone = SynthesizeTone(spec);
      const bool ok = WriteWav(target, tone, spec.channels, spec.sample_rate,
                               spec.bits, spec.float_samples, detail);
      return ok ? Outcome{"generated", "wav_direct_riff"}
                : Outcome{"not_generated", detail};
    }
    case Build::MfMp3: {
      std::string detail;
      const std::uint32_t bitrate = spec.channels == 2 ? 192000 : 128000;
      const HRESULT hr = EncodeMp3Frames(target, spec.sample_rate, spec.channels,
                                         bitrate, SynthesizeTone(spec), detail);
      if (FAILED(hr)) {
        fs::remove(target);
        return {"not_generated", detail + " hr=" + Hex(hr)};
      }
      return {"generated", detail + " bitrate=" + std::to_string(bitrate)};
    }
    case Build::MfAac:
    case Build::MfAlac: {
      const GUID subtype = spec.build == Build::MfAac ? MFAudioFormat_AAC
                                                      : MFAudioFormat_ALAC;
      const UINT32 encoders = CountEncoders(subtype);
      std::string detail;
      const UINT32 avg = spec.build == Build::MfAac ? 24000 : 0;  // 192 kbps AAC
      const HRESULT hr = EncodeWithSinkWriter(target, subtype, spec.sample_rate,
                                              spec.channels, avg,
                                              SynthesizeTone(spec), detail);
      const std::string count = "encoder_mfts=" + std::to_string(encoders);
      if (FAILED(hr)) {
        fs::remove(target);
        return {"not_generated", count + "; sink_writer " + Hex(hr) + " " + detail};
      }
      return {"generated", count + "; mf_sink_writer"};
    }
    case Build::CopyOf:
    case Build::CorruptHeader:
    case Build::Truncate:
    case Build::Empty: {
      if (spec.build == Build::Empty) {
        std::ofstream(target, std::ios::binary | std::ios::trunc);
        return {"generated", "zero_bytes"};
      }
      const Outcome* base = base_outcome();
      if (!base || base->status != "generated")
        return {"not_generated", std::string("base_not_generated:") + spec.base};
      std::ifstream in(dir / spec.base, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
      if (spec.build == Build::Truncate) {
        bytes.resize(bytes.size() * 2 / 5);
      } else if (spec.build == Build::CorruptHeader) {
        for (std::size_t i = 0; i < 16 && i < bytes.size(); ++i)
          bytes[i] = static_cast<char>(0xA5 ^ i);
      }
      std::ofstream out(target, std::ios::binary | std::ios::trunc);
      out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      return out ? Outcome{"generated", std::string("derived_from:") + spec.base}
                 : Outcome{"not_generated", "write_failed"};
    }
  }
  return {"not_generated", "unknown_build"};
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: shipglows_fixture_gen <output-dir>\n";
    return 2;
  }
  const fs::path dir = argv[1];
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    std::cerr << "cannot create " << dir.string() << '\n';
    return 2;
  }
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
  if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) return 2;

  std::map<std::string, Outcome> done;
  std::ofstream manifest(dir / "generated.tsv", std::ios::trunc);
  manifest << "file\tstatus\tdetail\n";
  for (const Spec& spec : shipglows_audio::trials::kSpecs) {
    const Outcome outcome = GenerateOne(spec, dir, done);
    done[spec.file] = outcome;
    manifest << spec.file << '\t' << outcome.status << '\t' << outcome.detail << '\n';
    std::cout << (outcome.status == "generated" ? "[generated]     " : "[not generated] ")
              << spec.file << "  " << outcome.detail << '\n';
  }
  MFShutdown();
  CoUninitialize();
  return 0;
}
