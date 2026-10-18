#pragma once
#include <cstdint>
namespace shipglows::audio::a0 {
// A0 diagnostic correspondence only, not a public presentation anchor.
struct ClockCandidate {
 bool available=false;
 const char* reason="unavailable";
 uint64_t output_frame=0,qpc_ns=0,age_ns=0;
 double fractional_frame=0;
};
inline ClockCandidate clock_candidate(uint64_t ticks,uint64_t frequency,uint64_t qpc_100ns,
 uint64_t begin_ns,uint64_t end_ns,uint64_t observation_ns,uint64_t submitted_frames,
 int64_t status,bool active,bool source_consistent) noexcept {
 ClockCandidate result;
 if(!active){result.reason="inactive";return result;}
 if(!source_consistent){result.reason="source_discontinuity";return result;}
 if(status!=0||!frequency){result.reason="clock_reading_invalid";return result;}
 if(!ticks){result.reason="warming_up_zero_position";return result;}
 if(qpc_100ns>UINT64_MAX/100){result.reason="clock_overflow";return result;}
 const uint64_t qpc_ns=qpc_100ns*100;
 if(end_ns<begin_ns||qpc_ns<begin_ns||qpc_ns>end_ns){result.reason="clock_correlation_unverified";return result;}
 if(observation_ns<end_ns){result.reason="future_observation";return result;}
 if(end_ns-begin_ns>2000000||observation_ns-qpc_ns>250000000){result.reason="clock_observation_stale_or_slow";return result;}
 const uint64_t whole=ticks/frequency,remainder=ticks%frequency;
 if(whole>UINT64_MAX/48000||remainder>UINT64_MAX/48000){result.reason="frame_conversion_overflow";return result;}
 const uint64_t whole_frames=whole*48000,scaled_remainder=remainder*48000;
 const uint64_t partial_frames=scaled_remainder/frequency;
 if(whole_frames>UINT64_MAX-partial_frames){result.reason="frame_conversion_overflow";return result;}
 const uint64_t frames=whole_frames+partial_frames;
 const uint64_t fractional_numerator=scaled_remainder%frequency;
 if(frames>submitted_frames||(frames==submitted_frames&&fractional_numerator)){
  result.reason="position_outside_submitted_domain";return result;
 }
 result.available=true;result.reason="candidate_only_uncertainty_unknown";
 result.output_frame=frames;result.qpc_ns=qpc_ns;result.age_ns=observation_ns-qpc_ns;
 result.fractional_frame=double(fractional_numerator)/double(frequency);
 return result;
}
}
