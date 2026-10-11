#pragma once
// Private A0 record serializer shared by the Windows probe and allocation probe.
#include "analysis_a0.hpp"
#include <flutter/encodable_value.h>
namespace shipglows::audio::a0 {
inline flutter::EncodableValue EncodeRecord(const Record& r,unsigned mask) {
 using V=flutter::EncodableValue;
 flutter::EncodableMap record;
 record[V("sequence")]=V(int64_t(r.sequence));
 record[V("center_frame")]=V(int64_t(r.center));
 record[V("continuity_id")]=V(int64_t(r.continuity));
 record[V("origin")]=V(int64_t(r.origin));
 record[V("valid_frames")]=V(int(r.valid_frames));
 record[V("physical_levels")]=std::vector<float>(r.levels.begin(),r.levels.end());
 record[V("visual_levels")]=std::vector<float>(r.visual_levels.begin(),r.visual_levels.end());
 if(mask&2){
  record[V("physical_envelope")]=std::vector<float>(r.envelope.begin(),r.envelope.end());
  record[V("visual_envelope")]=std::vector<float>(r.visual_envelope.begin(),r.visual_envelope.end());
 }
 if(mask&8){
  record[V("physical_spectrum")]=std::vector<float>(r.spectrum.begin(),r.spectrum.end());
  record[V("bands")]=std::vector<float>(r.bands.begin(),r.bands.end());
 }
 // This SDK's forwarding constructor initializes its variant from an lvalue;
 // inherited variant assignment moves the containers without that deep copy.
 V result;result=std::move(record);return result;
}
}
