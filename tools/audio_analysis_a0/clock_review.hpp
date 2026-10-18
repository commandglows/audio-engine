#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
namespace shipglows::audio::a0 {
// Private diagnostic state, single writer, no allocation. No uncertainty claim.
struct ClockReview {
 uint64_t accurate=0,inaccurate=0,regressions=0,frequency_changes=0,max_query_ns=0;
 uint64_t first_ticks=0,first_qpc=0,last_ticks=0,last_qpc=0,frequency=0;
 uint64_t after_startup_first_ticks=0,after_startup_first_qpc=0,after_startup_count=0;
 double after_startup_max_nominal_residual_ns=0;
 uint64_t local_first_ticks=0,local_first_qpc=0,local_completed_windows=0;
 double local_max_nominal_residual_ns=0;
 void observe(uint64_t ticks,uint64_t freq,uint64_t qpc,uint64_t begin,uint64_t end,int64_t status) noexcept {
  if(end>=begin)max_query_ns=std::max(max_query_ns,end-begin);
  if(status!=0||!freq||end<begin){++inaccurate;return;}
  if(accurate){
   if(freq!=frequency)++frequency_changes;
   if(ticks<last_ticks||qpc<last_qpc)++regressions;
  }else{first_ticks=ticks;first_qpc=qpc;frequency=freq;}
  if(qpc>=first_qpc&&qpc-first_qpc>=10000000){
   if(!after_startup_count){after_startup_first_ticks=ticks;after_startup_first_qpc=qpc;local_first_ticks=ticks;local_first_qpc=qpc;}
   ++after_startup_count;
   if(!regressions&&!frequency_changes&&ticks>=after_startup_first_ticks&&qpc>=after_startup_first_qpc){
    const long double device_ns=static_cast<long double>(ticks-after_startup_first_ticks)*1000000000/frequency;
    const long double qpc_ns=static_cast<long double>(qpc-after_startup_first_qpc)*100;
    after_startup_max_nominal_residual_ns=std::max(after_startup_max_nominal_residual_ns,double(std::abs(device_ns-qpc_ns)));
    // Observed local variation, not an absolute correlation error bound.
    // Include the closing observation before refreshing the one-second origin.
    if(ticks>=local_first_ticks&&qpc>=local_first_qpc){
     const long double local_device_ns=static_cast<long double>(ticks-local_first_ticks)*1000000000/frequency;
     const long double local_qpc_ns=static_cast<long double>(qpc-local_first_qpc)*100;
     local_max_nominal_residual_ns=std::max(local_max_nominal_residual_ns,double(std::abs(local_device_ns-local_qpc_ns)));
     if(qpc-local_first_qpc>=10000000){++local_completed_windows;local_first_ticks=ticks;local_first_qpc=qpc;}
    }
   }
  }
  last_ticks=ticks;last_qpc=qpc;++accurate;
 }
 bool rate_available() const noexcept {
  return accurate>=2&&!regressions&&!frequency_changes&&last_qpc>first_qpc&&last_ticks>=first_ticks&&frequency;
 }
 double relative_rate_ppm() const noexcept {
  if(!rate_available())return 0; // Caller must expose unavailable explicitly.
  return double((static_cast<long double>(last_ticks-first_ticks)*10000000/
    (static_cast<long double>(frequency)*(last_qpc-first_qpc))-1)*1000000);
 }
 bool after_startup_rate_available() const noexcept {
  return rate_available()&&after_startup_count>=2&&last_qpc>after_startup_first_qpc&&last_ticks>=after_startup_first_ticks;
 }
 double after_startup_relative_rate_ppm() const noexcept {
  if(!after_startup_rate_available())return 0;
  return double((static_cast<long double>(last_ticks-after_startup_first_ticks)*10000000/
    (static_cast<long double>(frequency)*(last_qpc-after_startup_first_qpc))-1)*1000000);
 }
};
}
