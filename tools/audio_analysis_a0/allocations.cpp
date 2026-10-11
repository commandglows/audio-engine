// Dedicated C++ new/new[] probe; deliberately excludes malloc, Flutter and WASAPI.
#include "analysis_a0.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#ifdef A0_BRIDGE_ALLOCATIONS
#include "bridge_record.hpp"
#include <flutter/standard_method_codec.h>
#endif

namespace {
thread_local bool tracking=false;
thread_local size_t calls=0, bytes=0;
thread_local size_t live_bytes=0,peak_bytes=0;
struct AllocationSlot { void* pointer=nullptr;size_t requested=0; };
thread_local std::array<AllocationSlot,4096> slots{};
void* allocate(size_t n) {
  void* p=std::malloc(n?n:1);
  if(!p)throw std::bad_alloc();
  if(tracking){
   auto slot=std::find_if(slots.begin(),slots.end(),[](const auto& s){return !s.pointer;});
   if(slot==slots.end())std::abort(); // Never silently lose live tracking.
   *slot={p,n};++calls;bytes+=n;live_bytes+=n;peak_bytes=std::max(peak_bytes,live_bytes);
  }
  return p;
}
void release(void* p) noexcept {
 if(!p)return;
 if(tracking){
  auto slot=std::find_if(slots.begin(),slots.end(),[p](const auto& s){return s.pointer==p;});
  if(slot!=slots.end()){live_bytes-=slot->requested;*slot={};}
 }
 std::free(p);
}
struct Scope {
  Scope(){slots.fill({});calls=bytes=live_bytes=peak_bytes=0;tracking=true;}
  ~Scope(){tracking=false;}
};
}
void* operator new(size_t n){return allocate(n);}
void* operator new[](size_t n){return allocate(n);}
void operator delete(void* p) noexcept {release(p);}
void operator delete[](void* p) noexcept {release(p);}
void operator delete(void* p,size_t) noexcept {release(p);}
void operator delete[](void* p,size_t) noexcept {release(p);}

int main(){
 using namespace shipglows::audio::a0;
 // Positive control: prove the replacement is active before measuring.
 {Scope s;void* p=::operator new(123);void* q=::operator new(77);
  if(live_bytes!=200||peak_bytes!=200)return 2;
  ::operator delete(p);if(live_bytes!=77)return 2;::operator delete(q);
  if(calls!=2||bytes!=200||live_bytes||peak_bytes!=200)return 2;}
 void* previous;
 {Scope s;previous=::operator new(100);}
 {Scope s;void* current=::operator new(50);::operator delete(previous);
  if(live_bytes!=50||peak_bytes!=50)return 2;::operator delete(current);if(live_bytes)return 2;}
#ifdef A0_BRIDGE_ALLOCATIONS
 // Warm the codec singleton outside measured regions; production reuses it.
 const auto& codec=flutter::StandardMethodCodec::GetInstance();
 std::cout<<"{\"kind\":\"a0_bridge_cpp_new_probe\",\"batches\":[";
 bool first=true;
 for(unsigned mask:{3u,11u,5u,13u})for(size_t count:{0u,1u,8u,16u}){
  Record r{};r.sequence=1;r.valid_frames=480;r.levels.fill(.25f);r.spectrum.fill(.0001f);
  flutter::EncodableValue response;
  size_t map_calls,map_bytes,encode_calls,encode_bytes,wire_bytes;
  size_t map_peak,map_retained,encode_peak;
  {Scope s;flutter::EncodableList records;records.reserve(count);
   for(size_t i=0;i<count;++i){r.sequence=i+1;records.emplace_back(EncodeRecord(r,mask));}
   flutter::EncodableMap root;root[flutter::EncodableValue("records")]=std::move(records);
   response=std::move(root);map_calls=calls;map_bytes=bytes;
   map_peak=peak_bytes;map_retained=live_bytes;
  }
  {Scope s;auto encoded=codec.EncodeSuccessEnvelope(&response);
   wire_bytes=encoded->size();encode_calls=calls;encode_bytes=bytes;
   encode_peak=peak_bytes;
  }
  // Independent legacy construction verifies every serialized field and byte.
  flutter::EncodableList legacy_records;
  using V=flutter::EncodableValue;
  for(size_t i=0;i<count;++i){
   r.sequence=i+1;flutter::EncodableMap m;
   m[V("sequence")]=V(int64_t(r.sequence));m[V("center_frame")]=V(int64_t(r.center));
   m[V("continuity_id")]=V(int64_t(r.continuity));m[V("origin")]=V(int64_t(r.origin));
   m[V("valid_frames")]=V(int(r.valid_frames));
   m[V("physical_levels")]=V(std::vector<float>(r.levels.begin(),r.levels.end()));
   m[V("visual_levels")]=V(std::vector<float>(r.visual_levels.begin(),r.visual_levels.end()));
   if(mask&2){m[V("physical_envelope")]=V(std::vector<float>(r.envelope.begin(),r.envelope.end()));m[V("visual_envelope")]=V(std::vector<float>(r.visual_envelope.begin(),r.visual_envelope.end()));}
   if(mask&8){m[V("physical_spectrum")]=V(std::vector<float>(r.spectrum.begin(),r.spectrum.end()));m[V("bands")]=V(std::vector<float>(r.bands.begin(),r.bands.end()));}
   legacy_records.emplace_back(std::move(m));
  }
  flutter::EncodableMap legacy_root;legacy_root[V("records")]=V(std::move(legacy_records));
  V legacy(std::move(legacy_root));
  if(*codec.EncodeSuccessEnvelope(&legacy)!=*codec.EncodeSuccessEnvelope(&response))return 7;
  if(count&&!map_calls)return 5;
  if(wire_bytes>128*1024)return 6;
  if(!first)std::cout<<",";first=false;
  std::cout<<"{\"mask\":"<<mask<<",\"records\":"<<count<<",\"map_calls\":"<<map_calls
   <<",\"map_requested_bytes\":"<<map_bytes<<",\"one_encode_calls\":"<<encode_calls
   <<",\"map_peak_live_payload_bytes\":"<<map_peak<<",\"map_retained_payload_bytes\":"<<map_retained
   <<",\"one_encode_peak_live_payload_bytes\":"<<encode_peak
   <<",\"map_plus_one_encode_peak_payload_bytes\":"<<std::max(map_peak,map_retained+encode_peak)
   <<",\"one_encode_requested_bytes\":"<<encode_bytes<<",\"wire_bytes_records_only\":"<<wire_bytes<<"}";
 }
 std::cout<<"]}\n";return 0;
#else
 std::cout<<"{\"kind\":\"a0_cpp_new_probe\",\"seconds_per_mask\":30,\"masks\":[";
 bool first=true;
 for(unsigned mask:{3u,11u,5u,13u}){
  size_t setup_calls,setup_bytes;
  std::unique_ptr<Processor> processor;
  std::unique_ptr<BlockQueue> queue;
  {Scope s;processor=std::make_unique<Processor>(mask);queue=std::make_unique<BlockQueue>(480);
   setup_calls=calls;setup_bytes=bytes;}
  std::array<float,960> input{},output{};
  for(size_t i=0;i<480;++i){input[2*i]=float(.4*std::sin(2*pi*997*i/48000));input[2*i+1]=-input[2*i];}
  size_t steady_calls,steady_bytes;
  {Scope s;BlockQueue::Header header;
   for(uint64_t f=0;f<30*48000;f+=480){
    if(!queue->try_push(input.data(),480,f)||!queue->try_pop(output.data(),header))return 3;
    processor->push(output.data(),header.frames,header.position);
   }
   processor->finish();steady_calls=calls;steady_bytes=bytes;
  }
  if(steady_calls||steady_bytes||processor->stats().records!=3000)return 4;
  if(!first)std::cout<<",";first=false;
  std::cout<<"{\"mask\":"<<mask<<",\"setup_calls\":"<<setup_calls<<",\"setup_requested_bytes\":"<<setup_bytes
   <<",\"steady_calls\":"<<steady_calls<<",\"steady_requested_bytes\":"<<steady_bytes<<",\"records\":"<<processor->stats().records<<"}";
 }
 std::cout<<"]}\n";
#endif
}
