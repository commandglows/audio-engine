#pragma once
// Disposable A0 worker-side prototype. This is NOT a public/wire contract.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace shipglows::audio::a0 {
constexpr double pi=3.14159265358979323846;
struct Record {
 uint64_t sequence=0, center=0, continuity=1, origin=0;
 uint32_t valid_frames=0, flags=0;
 std::array<float,6> levels{}, visual_levels{};
 std::array<float,256> envelope{}, visual_envelope{};
 std::array<float,1025> spectrum{};
 std::array<float,64> bands{};
};
struct Stats { uint64_t frames=0, records=0, fft_calls=0, pcm_resets=0, history_overwrites=0, copied_bytes=0, measurement_ns=0, transform_ns=0, fft_ns=0; };
struct Config { double sensitivity_db=0,gamma=.8,attack_ms=20,release_ms=180,floor_db=-80,ceiling_db=0; };
// Preallocated SPSC WHOLE block queue: one callback producer, one analysis worker.
// Audio submission outcome must be established by the adapter before publication.
class BlockQueue {
public:
 static_assert(std::atomic<uint64_t>::is_always_lock_free,"A0 callback queue requires lock-free uint64 atomics");
 struct Header { uint64_t position=0; uint32_t frames=0; bool discontinuity=false, submitted_silence=false; };
 explicit BlockQueue(uint32_t max_frames):max_(max_frames) {
  if(!max_ || max_>16384) throw std::invalid_argument("endpoint block exceeds A0 admission");
  samples_.resize(size_t(16)*max_*2);
 }
  bool try_push(const float* pcm, uint32_t frames,uint64_t position,bool gap=false,bool silence=false) noexcept {
  if(!pcm || !frames || frames>max_ || position>UINT64_MAX-frames){++drops_;return false;}
  const auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
  if(w-r>=16){++drops_;return false;}
  const auto slot=w%16;
  std::memcpy(samples_.data()+slot*max_*2,pcm,size_t(frames)*2*sizeof(float));
  headers_[slot]={position,frames,gap,silence};
  write_.store(w+1,std::memory_order_release);
  const uint64_t occupancy=w+1-r;
  auto high=high_water_.load(std::memory_order_relaxed);
  while(occupancy>high&&!high_water_.compare_exchange_weak(high,occupancy,
      std::memory_order_relaxed,std::memory_order_relaxed)){}
  return true;
 }
 // Worker copies into caller-owned preallocated storage. Never call from callback.
 bool try_pop(float* destination,Header& h) noexcept {
  const auto r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
  if(r==w)return false;
  h=headers_[r%16];
  std::memcpy(destination,samples_.data()+(r%16)*max_*2,size_t(h.frames)*2*sizeof(float));
  read_.store(r+1,std::memory_order_release);return true;
 }
 uint64_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }
 uint64_t high_water() const noexcept { return high_water_.load(std::memory_order_relaxed); }
 size_t bytes()const noexcept{return samples_.size()*sizeof(float)+sizeof(*this);}
private:
 uint32_t max_; std::vector<float> samples_; std::array<Header,16> headers_{};
 alignas(64) std::atomic<uint64_t> write_{0},read_{0},drops_{0},high_water_{0};
};
class Processor {
public:
 explicit Processor(unsigned mask,Config config={}):mask_(mask),cfg_(config) {
  if(mask!=3&&mask!=11&&mask!=5&&mask!=13)throw std::invalid_argument("unsupported A0 mask");
  if(!std::isfinite(cfg_.sensitivity_db)||cfg_.sensitivity_db < -24||cfg_.sensitivity_db>24||
    !std::isfinite(cfg_.gamma)||cfg_.gamma<.25||cfg_.gamma>2||!std::isfinite(cfg_.attack_ms)||cfg_.attack_ms<1||cfg_.attack_ms>200||
    !std::isfinite(cfg_.release_ms)||cfg_.release_ms<10||cfg_.release_ms>2000||!std::isfinite(cfg_.floor_db)||cfg_.floor_db < -120||cfg_.floor_db > -20||
    !std::isfinite(cfg_.ceiling_db)||cfg_.ceiling_db < -6||cfg_.ceiling_db>12||cfg_.floor_db>=cfg_.ceiling_db)throw std::invalid_argument("invalid transform config");
  for(size_t n=0;n<2048;++n)hann_[n]=.5-.5*std::cos(2*pi*n/2048);
 }
 // Worker-only; arbitrary partition sizes, finite bounded float32 stereo PCM.
 void push(const float* pcm,size_t frames,uint64_t position,bool discontinuity=false,bool submitted_silence=false) {
  if(ended_)throw std::logic_error("push after EOF");
  if(!pcm||frames>UINT64_MAX-position||frames>SIZE_MAX/2||frames>uint64_t(INT64_MAX)-2048)throw std::invalid_argument("invalid PCM span");
  if(started_&&!discontinuity&&position==origin_+length_&&frames>uint64_t(INT64_MAX)-2048-length_)throw std::invalid_argument("A0 local indexing budget exhausted");
  for(size_t i=0;i<frames*2;++i)if(!std::isfinite(pcm[i])||std::abs(pcm[i])>8)throw std::invalid_argument("invalid PCM sample");
  if(!started_){origin_=position;started_=true;}
  else if(discontinuity||position!=origin_+length_)reset(position);
  for(size_t f=0;f<frames;++f){
   pcm_[length_%4096]={pcm[2*f],pcm[2*f+1]}; silence_[length_%4096]=submitted_silence;
   ++length_;++stats_.frames;stats_.copied_bytes+=8;
   while(length_>=next_+lookahead())emit(false);
  }
 }
 void finish(){if(ended_)return;while(next_<length_)emit(true);ended_=true;}
 // Unknown final submission/drop: discard incomplete centered/hop work, never pad.
 void end_gap()noexcept {ended_=true;}
 size_t history_count()const noexcept{return count_;}
 const Record& history(size_t oldest_index)const {if(oldest_index>=count_)throw std::out_of_range("history");return history_[(head_+128-count_+oldest_index)%128];}
 const Record* latest()const noexcept{return count_?&history_[(head_+127)%128]:nullptr;}
 const Stats& stats()const noexcept{return stats_;}
 unsigned mask()const noexcept{return mask_;}
 size_t bytes()const noexcept{return sizeof(*this);}
private:
 uint64_t lookahead()const noexcept{return ((mask_&2)||(mask_&8))?1024:480;}
 void reset(uint64_t position){origin_=position;length_=next_=0;++continuity_;++stats_.pcm_resets;state_.fill(0);band_state_.fill(0);head_=count_=0;reset_pending_=true;}
 float sample(int64_t at,size_t channel)const {if(at<0||uint64_t(at)>=length_)return 0;return pcm_[uint64_t(at)%4096][channel];}
 double amplitude(double x)const{return std::pow(std::clamp(x*std::pow(10.,cfg_.sensitivity_db/20),0.,1.),cfg_.gamma);}
 float smooth(double target,double& state,uint32_t m)const {const double tau=(target>state?cfg_.attack_ms:cfg_.release_ms)/1000;state+=(1-std::exp(-double(m)/48000/tau))*(target-state);return float(state);}
 void fft(size_t channel){
  const auto begin=std::chrono::steady_clock::now();
  for(size_t n=0;n<2048;++n)fft_[n]=double(sample(int64_t(next_)+int64_t(n)-1024,channel))*hann_[n];
  for(size_t i=1,j=0;i<2048;++i){size_t bit=1024;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(fft_[i],fft_[j]);}
  for(size_t len=2;len<=2048;len*=2){const auto root=std::polar(1.,-2*pi/double(len));for(size_t i=0;i<2048;i+=len){std::complex<double>w=1.;for(size_t j=0;j<len/2;++j){auto u=fft_[i+j],v=fft_[i+j+len/2]*w;fft_[i+j]=u+v;fft_[i+j+len/2]=u-v;w*=root;}}}
  ++stats_.fft_calls;
  stats_.fft_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count());
 }
 void emit(bool eof){
  (void)eof;
  Record& r=history_[head_];r=Record{};r.sequence=++sequence_;r.center=origin_+next_;r.continuity=continuity_;r.origin=origin_;r.valid_frames=uint32_t(std::min<uint64_t>(480,length_-next_));r.flags=reset_pending_?1:0;reset_pending_=false;
  const auto measurement_begin=std::chrono::steady_clock::now();
  double sums[2]={0,0};for(uint32_t f=0;f<r.valid_frames;++f){if(silence_[(next_+f)%4096])r.flags|=2;for(size_t c=0;c<2;++c){double x=sample(int64_t(next_+f),c);r.levels[c]=std::max(r.levels[c],float(std::abs(x)));sums[c]+=x*x;}}
  r.levels[2]=std::max(r.levels[0],r.levels[1]);r.levels[3]=float(std::sqrt(sums[0]/r.valid_frames));r.levels[4]=float(std::sqrt(sums[1]/r.valid_frames));r.levels[5]=float(std::sqrt((sums[0]+sums[1])/(2*r.valid_frames)));
  if(mask_&2)for(size_t bin=0;bin<64;++bin)for(size_t c=0;c<2;++c){float lo=8,hi=-8;for(size_t n=0;n<32;++n){float x=sample(int64_t(next_)-1024+int64_t(bin*32+n),c);lo=std::min(lo,x);hi=std::max(hi,x);}size_t i=bin*4+c*2;r.envelope[i]=lo;r.envelope[i+1]=hi;}
  if(mask_&8){std::array<double,1025>powers{};for(size_t c=0;c<2;++c){fft(c);for(size_t k=0;k<=1024;++k)powers[k]+=(k==0||k==1024?1.:2.)*std::norm(fft_[k])/(2048*768*2);}for(size_t k=0;k<=1024;++k)r.spectrum[k]=float(powers[k]);
  }
  const auto transform_begin=std::chrono::steady_clock::now();
  stats_.measurement_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(transform_begin-measurement_begin).count());
  for(size_t i=0;i<6;++i)r.visual_levels[i]=smooth(amplitude(r.levels[i]),state_[i],r.valid_frames);
  if(mask_&2)for(size_t j=0;j<256;++j)r.visual_envelope[j]=float(std::copysign(amplitude(std::abs(r.envelope[j])),r.envelope[j]));
  if(mask_&8)for(size_t b=0;b<64;++b){double lo=20*std::pow(1000.,double(b)/64),hi=20*std::pow(1000.,double(b+1)/64),q=0;for(size_t k=1;k<=1024;++k){double a=(k-.5)*48000/2048,d=std::min(24000.,(k+.5)*48000/2048);q+=std::max(0.,std::min(d,hi)-std::max(a,lo))/(d-a)*double(r.spectrum[k]);}double target=q==0?0:std::pow(std::clamp((10*std::log10(2*q)+cfg_.sensitivity_db-cfg_.floor_db)/(cfg_.ceiling_db-cfg_.floor_db),0.,1.),cfg_.gamma);r.bands[b]=smooth(target,band_state_[b],r.valid_frames);}
  stats_.transform_ns+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-transform_begin).count());
  head_=(head_+1)%128;if(count_<128)++count_;else ++stats_.history_overwrites;next_+=480;++stats_.records;
 }
 unsigned mask_;Config cfg_;Stats stats_{};bool started_=false,ended_=false,reset_pending_=false;
 uint64_t origin_=0,length_=0,next_=0,sequence_=0,continuity_=1;
 std::array<std::array<float,2>,4096> pcm_{};std::array<bool,4096> silence_{};
 std::array<Record,128> history_{};size_t head_=0,count_=0;
 std::array<double,6>state_{};std::array<double,64>band_state_{};
 std::array<double,2048>hann_{};std::array<std::complex<double>,2048>fft_{};
};
} // namespace shipglows::audio::a0
