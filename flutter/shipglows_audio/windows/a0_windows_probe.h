#pragma once
// Private disposable calibration host; never instantiated by production playback.
#include "analysis_a0.hpp"
#include "shipglows/audio/dsp_chain.hpp"
#include "windows_wasapi_playback.h"
#include <flutter/encodable_value.h>
#include <flutter/standard_method_codec.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace shipglows_audio {
class A0WindowsProbe {
  using Processor = shipglows::audio::a0::Processor;
  using Queue = shipglows::audio::a0::BlockQueue;
  using V = flutter::EncodableValue;
  using M = flutter::EncodableMap;
 public:
  A0WindowsProbe(unsigned mask, bool enabled, bool silent_output)
      : mask_(mask), enabled_(enabled), silent_output_(silent_output) {
    // One-second deterministic, low-level fixture; generated off the callback.
    source_.resize(48000*2);
    for (size_t i=0;i<48000;++i) {
      const double t=double(i)/48000;
      const float x=float(.025*std::sin(2*shipglows::audio::a0::pi*997*t));
      source_[i*2]=x; source_[i*2+1]=-x;
    }
    if (enabled_) processor_=std::make_unique<Processor>(mask);
    GetProcessTimes(GetCurrentProcess(), &creation_, &exit_, &kernel_start_, &user_start_);
    started_=std::chrono::steady_clock::now();
    output_=std::make_unique<WindowsWasapiPlayback>(&Render,this,
      WasapiA0Observer{&Admit,&Submitted,this});
  }
  ~A0WindowsProbe() { Stop(); output_.reset(); }
  WasapiPlaybackResult Start(const std::wstring& endpoint) { return output_->Start(endpoint); }
  void Stop() {
    if (output_) {
      const auto stopped=output_->Stop(); // Fences callback before queue teardown.
      stop_failed_.store(!static_cast<bool>(stopped),std::memory_order_release);
    }
    stopping_.store(true);
    if(worker_.joinable()) worker_.join();
  }
  V Poll(uint64_t after) {
    M root, metrics;
    flutter::EncodableList records;
    std::array<shipglows::audio::a0::Record,16> record_snapshot{};
    size_t record_count=0;
    shipglows::audio::a0::Stats analysis_stats{};
    uint64_t latest_sequence=0,worker_copy_bytes=0,worker_us=0,queue_pop_copy_ns=0;
    uint64_t worker_mutex_wait_ns=0,worker_mutex_wait_max_ns=0;
    uint64_t worker_processor_push_ns=0,worker_processor_push_max_ns=0;
    bool resync=false;
    const auto poll_lock_start=std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point poll_lock_acquired;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      poll_lock_acquired=std::chrono::steady_clock::now();
      poll_lock_wait_ns_+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(poll_lock_acquired-poll_lock_start).count());
      if(processor_) {
        const auto count=processor_->history_count();
        if(count && after && after+1<processor_->history(0).sequence)resync=true;
        size_t first=0;
        while(first<count && processor_->history(first).sequence<=after)++first;
        if(count-first>16){resync=true;first=count>8?count-8:0;}
        for(size_t i=first;i<count && record_count<record_snapshot.size();++i){
          record_snapshot[record_count++]=processor_->history(i);
        }
        analysis_stats=processor_->stats();
        latest_sequence=analysis_stats.records;
      }
      worker_copy_bytes=worker_copy_bytes_;worker_us=worker_us_;queue_pop_copy_ns=queue_pop_copy_ns_;
      worker_mutex_wait_ns=worker_mutex_wait_ns_;worker_mutex_wait_max_ns=worker_mutex_wait_max_ns_;
      worker_processor_push_ns=worker_processor_push_ns_;worker_processor_push_max_ns=worker_processor_push_max_ns_;
    }
    poll_snapshot_lock_ns_+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-poll_lock_acquired).count());
    for(size_t i=0;i<record_count;++i){
      const auto& r=record_snapshot[i];M record;
      record[V("sequence")]=V(int64_t(r.sequence));
      record[V("center_frame")]=V(int64_t(r.center));
      record[V("continuity_id")]=V(int64_t(r.continuity));
      record[V("origin")]=V(int64_t(r.origin));
      record[V("valid_frames")]=V(int(r.valid_frames));
      record[V("physical_levels")]=Floats(r.levels);
      record[V("visual_levels")]=Floats(r.visual_levels);
      if(mask_&2){record[V("physical_envelope")]=Floats(r.envelope);record[V("visual_envelope")]=Floats(r.visual_envelope);}
      if(mask_&8){record[V("physical_spectrum")]=Floats(r.spectrum);record[V("bands")]=Floats(r.bands);}
      bridge_float_copy_bytes_+=uint64_t(12+((mask_&2)?512:0)+((mask_&8)?1089:0))*4;
      records.emplace_back(std::move(record));
    }
    root[V("latest_sequence")]=V(int64_t(latest_sequence));
    if(processor_){
      metrics[V("fft_calls")]=V(int64_t(analysis_stats.fft_calls));
      metrics[V("measurement_ns_including_fft")]=V(int64_t(analysis_stats.measurement_ns));
      metrics[V("transform_ns")]=V(int64_t(analysis_stats.transform_ns));
      metrics[V("fft_ns_subset_of_measurement")]=V(int64_t(analysis_stats.fft_ns));
      metrics[V("pcm_resets")]=V(int64_t(analysis_stats.pcm_resets));
      metrics[V("history_overwrites")]=V(int64_t(analysis_stats.history_overwrites));
      metrics[V("analysis_frames")]=V(int64_t(analysis_stats.frames));
      metrics[V("worker_copy_bytes")]=V(int64_t(analysis_stats.copied_bytes+worker_copy_bytes));
    }
    metrics[V("worker_us")]=V(int64_t(worker_us));
    metrics[V("queue_pop_copy_ns")]=V(int64_t(queue_pop_copy_ns));
    metrics[V("worker_mutex_wait_ns")]=V(int64_t(worker_mutex_wait_ns));
    metrics[V("worker_mutex_wait_max_ns")]=V(int64_t(worker_mutex_wait_max_ns));
    metrics[V("worker_processor_push_ns")]=V(int64_t(worker_processor_push_ns));
    metrics[V("worker_processor_push_max_ns")]=V(int64_t(worker_processor_push_max_ns));
    metrics[V("poll_snapshot_lock_ns")]=V(int64_t(poll_snapshot_lock_ns_));
    metrics[V("poll_lock_wait_ns")]=V(int64_t(poll_lock_wait_ns_));
    const auto m=output_->Metrics();
    metrics[V("callbacks")]=V(int64_t(m.callbacks));
    metrics[V("underruns")]=V(int64_t(m.underruns));
    metrics[V("render_p99_us_upper_bound")]=V(int64_t(m.p99_callback_microseconds));
    metrics[V("render_max_us")]=V(int64_t(m.max_callback_microseconds));
    metrics[V("submission_max_us")]=V(int64_t(m.max_submission_microseconds));
    metrics[V("submission_p99_us_upper_bound")]=V(int64_t(m.p99_submission_microseconds));
    metrics[V("worker_failed")]=V(worker_failed_.load());
    metrics[V("stop_failed")]=V(stop_failed_.load(std::memory_order_acquire));
    metrics[V("silent_output")]=V(silent_output_);
    metrics[V("final_pcm_tail_unknown")]=V(stopping_.load() && pending_gap_);
    metrics[V("endpoint_period_100ns")]=V(int64_t(m.endpoint_period_100ns));
    metrics[V("endpoint_buffer_frames")]=V(int(m.buffer_frames));
    metrics[V("queue_drops")]=V(int64_t(queue_?queue_->drops():0));
    metrics[V("queue_high_water_blocks")]=V(int64_t(queue_?queue_->high_water():0));
    metrics[V("last_drop_position_frames")]=V(int64_t(last_drop_position_.load(std::memory_order_relaxed)));
    metrics[V("last_drop_elapsed_ns")]=V(int64_t(last_drop_elapsed_ns_.load(std::memory_order_relaxed)));
    metrics[V("tap_copy_bytes")]=V(int64_t(tap_copy_bytes_.load()));
    metrics[V("tap_copy_ns")]=V(int64_t(tap_copy_ns_.load()));
    metrics[V("bridge_float_copy_bytes")]=V(int64_t(bridge_float_copy_bytes_));
    metrics[V("submission_failures")]=V(int64_t(submission_failures_.load()));
    metrics[V("native_context_bytes")]=V(int64_t((processor_?processor_->bytes():0)+
      (queue_?queue_->bytes():0)+(scratch_.capacity()+zeros_.capacity()+source_.capacity())*sizeof(float)+sizeof(*this)+size_t(m.buffer_frames)*8));
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))) {
      metrics[V("process_working_set_bytes")]=V(int64_t(memory.WorkingSetSize));
      metrics[V("process_peak_working_set_bytes")]=V(int64_t(memory.PeakWorkingSetSize));
      metrics[V("process_private_bytes")]=V(int64_t(memory.PrivateUsage));
    }
    FILETIME c,e,k,u;
    if(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u)) {
      const auto cpu=Ticks(k)+Ticks(u)-Ticks(kernel_start_)-Ticks(user_start_);
      const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count();
      metrics[V("process_cpu_seconds")]=V(double(cpu)/1e7);
      metrics[V("process_cpu_one_core_percent")]=V(wall>0?double(cpu)/1e7/wall*100:0);
    }
    root[V("records")]=V(std::move(records));root[V("resync")]=V(resync);
    root[V("submitted_frames")]=V(int64_t(submitted_.load()));
    root[V("observation_ns")]=V(int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    root[V("sample_rate")]=V(48000);
    root[V("timing_uncertainty_known")]=V(false);
    metrics[V("encoded_response_bytes")]=V(int64_t(0));
    metrics[V("max_encoded_response_bytes")]=V(int64_t(max_encoded_response_bytes_));
    metrics[V("encoded_response_bytes_total")]=V(int64_t(encoded_response_bytes_total_));
    metrics[V("serialization_probe_ns")]=V(int64_t(serialization_probe_ns_));
    root[V("metrics")]=V(std::move(metrics));
    V response(std::move(root));
    // Deliberate extra bounded encoding to measure real codec size. Its cost is
    // reported, included in RTT, and is not production transport overhead.
    const auto encode_begin=std::chrono::steady_clock::now();
    auto encoded=flutter::StandardMethodCodec::GetInstance().EncodeSuccessEnvelope(&response);
    serialization_probe_ns_+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-encode_begin).count());
    if(encoded->size()>128*1024)throw std::runtime_error("A0 response exceeds128KiB");
    max_encoded_response_bytes_=std::max<uint64_t>(max_encoded_response_bytes_,encoded->size());
    encoded_response_bytes_total_+=encoded->size();
    auto& response_metrics=std::get<M>(std::get<M>(response).at(V("metrics")));
    response_metrics[V("encoded_response_bytes")]=V(int64_t(encoded->size()));
    response_metrics[V("serialization_probe_ns")]=V(int64_t(serialization_probe_ns_));
    response_metrics[V("max_encoded_response_bytes")]=V(int64_t(max_encoded_response_bytes_));
    response_metrics[V("encoded_response_bytes_total")]=V(int64_t(encoded_response_bytes_total_));
    return response;
  }
 private:
  template<size_t N> static V Floats(const std::array<float,N>& input) {
    return V(std::vector<float>(input.begin(),input.end()));
  }
  static uint64_t Ticks(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
  static bool Admit(uint32_t frames,void* context) {
    auto* self=static_cast<A0WindowsProbe*>(context);
    if(!frames || frames>16384)return !self->enabled_;
    if(!self->enabled_)return true;
    self->queue_=std::make_unique<Queue>(frames);
    self->scratch_.resize(size_t(frames)*2);self->zeros_.resize(size_t(frames)*2);
    if(self->processor_->bytes()+self->queue_->bytes()+
       (self->scratch_.capacity()+self->zeros_.capacity()+self->source_.capacity())*sizeof(float)+sizeof(*self)>8*1024*1024)
      return false;
    self->worker_=std::thread([self]{self->Work();});return true;
  }
  static bool Render(float* pcm,uint32_t frames,uint32_t channels,void* context) noexcept {
    auto* self=static_cast<A0WindowsProbe*>(context);if(channels!=2)return false;
    for(uint32_t i=0;i<frames;++i){auto f=self->source_position_++%48000;
      pcm[i*2]=self->source_[f*2];pcm[i*2+1]=self->source_[f*2+1];}
    self->dsp_.process(pcm,frames);
    if(self->silent_output_)std::fill_n(pcm,size_t(frames)*channels,0.0f);
    return true;
  }
  static void Submitted(const float* pcm,uint32_t frames,bool silence,bool success,void* context) noexcept {
    auto* self=static_cast<A0WindowsProbe*>(context);
    const auto position=self->submitted_.load(std::memory_order_relaxed);
    self->submitted_.store(position+frames,std::memory_order_relaxed);
    if(!success){self->submission_failures_.fetch_add(1);self->pending_gap_=true;return;}
    if(!self->queue_)return;
    const auto begin=std::chrono::steady_clock::now();
    const bool submitted_silence=silence||self->silent_output_;
    const bool admitted=self->queue_->try_push(submitted_silence?self->zeros_.data():pcm,frames,position,self->pending_gap_,submitted_silence);
    self->tap_copy_ns_.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count()),std::memory_order_relaxed);
    self->pending_gap_=!admitted;
    if(admitted)self->tap_copy_bytes_.fetch_add(uint64_t(frames)*8,std::memory_order_relaxed);
    else {
      self->last_drop_position_.store(position,std::memory_order_relaxed);
      self->last_drop_elapsed_ns_.store(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-self->started_).count()),std::memory_order_relaxed);
    }
  }
  void Work() {
    Queue::Header header;
    for(;;){
      const auto pop_begin=std::chrono::steady_clock::now();
      if(queue_->try_pop(scratch_.data(),header)) {
        const auto pop_ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-pop_begin).count());
        const auto wait_begin=std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        const auto push_begin=std::chrono::steady_clock::now();
        const auto wait_ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(push_begin-wait_begin).count());
        worker_mutex_wait_ns_+=wait_ns;worker_mutex_wait_max_ns_=std::max(worker_mutex_wait_max_ns_,wait_ns);
        try { processor_->push(scratch_.data(),header.frames,header.position,header.discontinuity,header.submitted_silence); }
        catch(...) { worker_failed_.store(true);return; }
        const auto push_ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-push_begin).count());
        worker_processor_push_ns_+=push_ns;worker_processor_push_max_ns_=std::max(worker_processor_push_max_ns_,push_ns);
        worker_copy_bytes_+=uint64_t(header.frames)*8;
        queue_pop_copy_ns_+=pop_ns;
        worker_us_+=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-wait_begin).count());
      } else { if(stopping_.load())break;std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // A dropped final block is missing PCM, never clean EOF padding.
    if(!pending_gap_)processor_->finish();
  }
  unsigned mask_;bool enabled_,silent_output_;std::unique_ptr<Processor> processor_;std::unique_ptr<Queue> queue_;
  std::unique_ptr<WindowsWasapiPlayback> output_;std::vector<float> source_,scratch_,zeros_;
  shipglows::audio::DspChain dsp_;std::thread worker_;std::mutex mutex_;
  std::atomic<bool> stopping_{false},worker_failed_{false},stop_failed_{false};bool pending_gap_=false;
  std::atomic<uint64_t> submitted_{0},tap_copy_bytes_{0},submission_failures_{0};
  std::atomic<uint64_t> tap_copy_ns_{0};
  uint64_t source_position_=0,worker_us_=0,worker_copy_bytes_=0;
  uint64_t bridge_float_copy_bytes_=0,serialization_probe_ns_=0;
  uint64_t max_encoded_response_bytes_=0,encoded_response_bytes_total_=0;
  uint64_t queue_pop_copy_ns_=0;
  uint64_t worker_mutex_wait_ns_=0,worker_mutex_wait_max_ns_=0,poll_lock_wait_ns_=0;
  uint64_t worker_processor_push_ns_=0,worker_processor_push_max_ns_=0,poll_snapshot_lock_ns_=0;
  std::atomic<uint64_t> last_drop_position_{0},last_drop_elapsed_ns_{0};
  FILETIME creation_{},exit_{},kernel_start_{},user_start_{};
  std::chrono::steady_clock::time_point started_;
};
}
