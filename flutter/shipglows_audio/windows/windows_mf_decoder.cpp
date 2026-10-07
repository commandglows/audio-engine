#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_mf_decoder.h"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <string_view>

namespace shipglows_audio {
namespace {
using Microsoft::WRL::ComPtr;

MfDecodeResult Error(MfDecodeError code, const char* detail) {
  return {code, detail};
}

bool AttributeUInt32(IMFMediaType* type, REFGUID key, UINT32& value) {
  return type && SUCCEEDED(type->GetUINT32(key, &value));
}

std::wstring LowerExtension(const std::filesystem::path& path) {
  auto extension = path.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return extension;
}

bool IsAacLcIndication(UINT32 value) {
  // MPEG-4 Audio Object Type AAC-LC level indications documented by MF.
  // Values for Main, HE-AAC, and other object types are deliberately rejected.
  switch (value) {
    case 0x29: case 0x2a: case 0x2b: case 0x2c:
    case 0x2d: case 0x2e: case 0x2f: return true;
    default: return false;
  }
}
}  // namespace

struct WindowsMfDecoder::Impl final {
  ComPtr<IMFSourceReader> reader;
  double duration = 0.0;
  bool started_mf = false;
  bool eof = false;
  std::vector<float> pending;
  std::size_t pending_offset = 0;

  ~Impl() {
    reader.Reset();
    if (started_mf) MFShutdown();
  }
};

WindowsMfDecoder::WindowsMfDecoder() : impl_(std::make_unique<Impl>()) {}
WindowsMfDecoder::~WindowsMfDecoder() = default;
WindowsMfDecoder::WindowsMfDecoder(WindowsMfDecoder&&) noexcept = default;
WindowsMfDecoder& WindowsMfDecoder::operator=(WindowsMfDecoder&&) noexcept = default;

MfDecodeResult WindowsMfDecoder::Open(const std::filesystem::path& path) {
  if (!impl_) impl_ = std::make_unique<Impl>();
  impl_->reader.Reset();
  impl_->duration = 0.0;
  impl_->eof = false;
  impl_->pending.clear();
  impl_->pending_offset = 0;
  if (path.empty()) return Error(MfDecodeError::invalid_argument, "empty_path");
  const auto extension = LowerExtension(path);
  if (extension != L".mp3" && extension != L".m4a" && extension != L".wav")
    return Error(MfDecodeError::unsupported_extension, "extension_not_admitted");
  std::error_code filesystem_error;
  if (!std::filesystem::is_regular_file(path, filesystem_error))
    return Error(MfDecodeError::file_not_found, "local_file_unavailable");

  if (!impl_->started_mf) {
    const HRESULT startup = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(startup)) return Error(MfDecodeError::media_foundation_unavailable,
                                      "mf_startup_failed");
    impl_->started_mf = true;
  }
  const HRESULT opened = MFCreateSourceReaderFromURL(path.c_str(), nullptr,
                                                       &impl_->reader);
  if (FAILED(opened)) return Error(MfDecodeError::source_open_failed,
                                   "source_reader_open_failed");

  // Inspect the original presentation rather than inferring track shape from
  // the source reader's FIRST_AUDIO_STREAM selector (which can hide video or
  // additional audio streams).
  ComPtr<IMFMediaSource> media_source;
  ComPtr<IMFPresentationDescriptor> presentation;
  DWORD stream_count = 0;
  HRESULT result = impl_->reader->GetServiceForStream(
      static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), GUID_NULL,
      IID_PPV_ARGS(&media_source));
  if (FAILED(result) || FAILED(media_source->CreatePresentationDescriptor(&presentation)) ||
      FAILED(presentation->GetStreamDescriptorCount(&stream_count)))
    return Error(MfDecodeError::unsupported_stream, "presentation_inspection_failed");
  if (stream_count != 1)
    return Error(MfDecodeError::unsupported_stream, "single_stream_profile_required");
  BOOL selected = FALSE;
  ComPtr<IMFStreamDescriptor> stream_descriptor;
  ComPtr<IMFMediaTypeHandler> type_handler;
  ComPtr<IMFMediaType> presentation_type;
  GUID presentation_major{};
  if (FAILED(presentation->GetStreamDescriptorByIndex(0, &selected,
                                                       &stream_descriptor)) ||
      FAILED(stream_descriptor->GetMediaTypeHandler(&type_handler)) ||
      FAILED(type_handler->GetCurrentMediaType(&presentation_type)) ||
      FAILED(presentation_type->GetGUID(MF_MT_MAJOR_TYPE, &presentation_major)) ||
      presentation_major != MFMediaType_Audio)
    return Error(MfDecodeError::unsupported_stream, "audio_only_stream_required");

  ComPtr<IMFMediaType> source_type;
  result = impl_->reader->GetNativeMediaType(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &source_type);
  if (FAILED(result)) return Error(MfDecodeError::unsupported_stream,
                                   "audio_stream_missing");
  GUID subtype{};
  UINT32 channels = 0, sample_rate = 0, bits = 0;
  if (FAILED(source_type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
      !AttributeUInt32(source_type.Get(), MF_MT_AUDIO_NUM_CHANNELS, channels) ||
      !AttributeUInt32(source_type.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND,
                       sample_rate))
    return Error(MfDecodeError::unsupported_format, "source_format_incomplete");
  if (channels < 1 || channels > 2 || sample_rate < 8000 || sample_rate > 192000)
    return Error(MfDecodeError::unsupported_format, "channel_or_rate_out_of_range");

  if (extension == L".mp3") {
    if (subtype != MFAudioFormat_MP3)
      return Error(MfDecodeError::unsupported_profile, "mp3_layer_iii_required");
  } else if (extension == L".m4a") {
    // Media Foundation reports the parsed presentation MIME type. Do not
    // infer an M4A container solely from a filename or AAC elementary subtype.
    PROPVARIANT mime;
    PropVariantInit(&mime);
    const HRESULT mime_result = impl_->reader->GetPresentationAttribute(
        static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_MIME_TYPE,
        &mime);
    const bool is_mpeg4_audio = SUCCEEDED(mime_result) && mime.vt == VT_LPWSTR &&
        mime.pwszVal && _wcsicmp(mime.pwszVal, L"audio/mp4") == 0;
    PropVariantClear(&mime);
    if (!is_mpeg4_audio)
      return Error(MfDecodeError::unsupported_profile, "m4a_container_unconfirmed");
    if (subtype != MFAudioFormat_AAC)
      return Error(MfDecodeError::unsupported_profile, "m4a_aac_required_alac_deferred");
    UINT32 profile = 0;
    if (!AttributeUInt32(source_type.Get(), MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION,
                         profile) || !IsAacLcIndication(profile))
      return Error(MfDecodeError::unsupported_profile, "aac_lc_profile_unconfirmed");
  } else {
    if (subtype != MFAudioFormat_PCM && subtype != MFAudioFormat_Float)
      return Error(MfDecodeError::unsupported_profile, "wav_pcm_required");
    if (!AttributeUInt32(source_type.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, bits))
      return Error(MfDecodeError::unsupported_format, "wav_bit_depth_missing");
    if (subtype == MFAudioFormat_PCM && bits != 16 && bits != 24 && bits != 32)
      return Error(MfDecodeError::unsupported_format, "wav_pcm_bit_depth_not_admitted");
    if (subtype == MFAudioFormat_Float && bits != 32)
      return Error(MfDecodeError::unsupported_format, "wav_float32_required");
  }

  ComPtr<IMFMediaType> output_type;
  if (FAILED(MFCreateMediaType(&output_type)) ||
      FAILED(output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
      FAILED(output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float)) ||
      FAILED(output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2)) ||
      FAILED(output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000)) ||
      FAILED(output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32)) ||
      FAILED(output_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8)) ||
      FAILED(output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 384000)))
    return Error(MfDecodeError::unsupported_format, "output_type_creation_failed");
  result = impl_->reader->SetCurrentMediaType(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr,
      output_type.Get());
  if (FAILED(result)) return Error(MfDecodeError::unsupported_format,
                                   "mf_conversion_to_stereo_48k_unavailable");
  PROPVARIANT duration;
  PropVariantInit(&duration);
  if (SUCCEEDED(impl_->reader->GetPresentationAttribute(
          static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION,
          &duration)) &&
      duration.vt == VT_UI8 && duration.uhVal.QuadPart != 0) {
    impl_->duration = static_cast<double>(duration.uhVal.QuadPart) / 10000000.0;
  }
  PropVariantClear(&duration);
  return {};
}

MfDecodeResult WindowsMfDecoder::ReadFrames(std::size_t max_frames,
                                             std::vector<float>& stereo) {
  if (!impl_ || !impl_->reader) return Error(MfDecodeError::invalid_state, "decoder_not_open");
  if (max_frames == 0 || max_frames > std::numeric_limits<DWORD>::max() / 8)
    return Error(MfDecodeError::invalid_argument, "invalid_frame_count");
  stereo.clear();
  if (impl_->eof) return {};
  stereo.reserve(max_frames * 2);
  while (stereo.size() < max_frames * 2) {
    if (impl_->pending_offset < impl_->pending.size()) {
      const auto remaining = impl_->pending.size() - impl_->pending_offset;
      const auto take = std::min(remaining, max_frames * 2 - stereo.size());
      stereo.insert(stereo.end(), impl_->pending.begin() + impl_->pending_offset,
                    impl_->pending.begin() + impl_->pending_offset + take);
      impl_->pending_offset += take;
      if (impl_->pending_offset == impl_->pending.size()) {
        impl_->pending.clear();
        impl_->pending_offset = 0;
      }
      continue;
    }
    DWORD stream = 0, flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
    const HRESULT result = impl_->reader->ReadSample(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, &stream,
        &flags, &timestamp, &sample);
    if (FAILED(result)) return Error(MfDecodeError::decode_failed, "mf_read_sample_failed");
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { impl_->eof = true; break; }
    if (!sample) continue;
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
      return Error(MfDecodeError::decode_failed, "sample_buffer_unavailable");
    BYTE* bytes = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&bytes, nullptr, &length)))
      return Error(MfDecodeError::decode_failed, "sample_buffer_lock_failed");
    const auto available = static_cast<std::size_t>(length) / sizeof(float);
    if (length != 0 && length % 8 != 0) {
      buffer->Unlock();
      return Error(MfDecodeError::decode_failed, "converted_sample_not_stereo_float");
    }
    const auto take = available & ~std::size_t{1};
    if (take) {
      const auto* samples = reinterpret_cast<const float*>(bytes);
      const auto immediate = std::min(take, max_frames * 2 - stereo.size());
      stereo.insert(stereo.end(), samples, samples + immediate);
      if (immediate < take) {
        impl_->pending.assign(samples + immediate, samples + take);
        impl_->pending_offset = 0;
      }
    }
    buffer->Unlock();
    if (stereo.size() >= max_frames * 2) break;
  }
  return {};
}

MfDecodeResult WindowsMfDecoder::Seek(double seconds) {
  if (!impl_ || !impl_->reader) return Error(MfDecodeError::invalid_state, "decoder_not_open");
  if (!std::isfinite(seconds) || seconds < 0.0 ||
      (impl_->duration > 0.0 && seconds > impl_->duration))
    return Error(MfDecodeError::invalid_argument, "seek_out_of_range");
  PROPVARIANT position;
  PropVariantInit(&position);
  position.vt = VT_I8;
  position.hVal.QuadPart = static_cast<LONGLONG>(seconds * 10000000.0);
  const HRESULT result = impl_->reader->SetCurrentPosition(GUID_NULL, position);
  PropVariantClear(&position);
  if (FAILED(result)) return Error(MfDecodeError::seek_failed, "mf_seek_failed");
  impl_->eof = false;
  impl_->pending.clear();
  impl_->pending_offset = 0;
  return {};
}

double WindowsMfDecoder::duration_seconds() const noexcept {
  return impl_ ? impl_->duration : 0.0;
}
bool WindowsMfDecoder::is_open() const noexcept {
  return impl_ && impl_->reader;
}

}  // namespace shipglows_audio
