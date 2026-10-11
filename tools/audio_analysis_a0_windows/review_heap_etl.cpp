// Offline, read-only ETW review. No tracing, registry changes or elevation.
#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
#include <tuple>
#include <string>

static const GUID heapGuid = {0x222962ab,0x6180,0x4b88,{0xa8,0x25,0x34,0x6b,0x75,0xf2,0xa2,0x4a}};
struct Field { std::wstring name; unsigned type, length; uint64_t value; };
struct Group {
 uint64_t count=0; unsigned payloadLength=0, schemaStatus=0;
 std::vector<Field> fields;
 std::vector<unsigned> sample;
};
static std::map<std::pair<unsigned,unsigned>,Group> groups;
struct Ledger {
 std::map<std::pair<uint64_t,uint64_t>,uint64_t> live;
 uint64_t bytes=0, peak=0, requested=0, largest=0, unknownFree=0, duplicates=0, rejected=0;
 uint64_t reallocRequested=0, reallocOldMissing=0, reallocMirroredAlloc=0;
 void remove(uint64_t heap,uint64_t address) {
  auto it=live.find({heap,address});
  if(it==live.end()){++unknownFree;return;}
  bytes-=it->second;live.erase(it);
 }
 void add(uint64_t heap,uint64_t address,uint64_t size) {
  auto it=live.find({heap,address});
  if(it!=live.end()){++duplicates;bytes-=it->second;live.erase(it);}
  live[{heap,address}]=size;bytes+=size;requested+=size;
  if(bytes>peak)peak=bytes;if(size>largest)largest=size;
 }
 void reallocate(uint64_t heap,uint64_t oldAddress,uint64_t newAddress,uint64_t size) {
  reallocRequested+=size;
  auto old=live.find({heap,oldAddress});
  if(old!=live.end()){bytes-=old->second;live.erase(old);}else ++reallocOldMissing;
  auto newer=live.find({heap,newAddress});
  if(newer!=live.end() && newer->second==size){++reallocMirroredAlloc;return;}
  // Some heaps also emit Alloc/Free around Realloc. Never count that new block twice.
  add(heap,newAddress,size);
 }
};
static std::map<unsigned,Ledger> ledgers;
static void account(EVENT_RECORD* event,const Group& group,unsigned type) {
 if(type!=33 && type!=34 && type!=36)return;
 auto& ledger=ledgers[event->EventHeader.ProcessId];
 const std::vector<std::wstring> expected=type==33 ? std::vector<std::wstring>{L"HeapHandle",L"AllocSize",L"AllocAddress",L"SourceId"} :
  type==34 ? std::vector<std::wstring>{L"HeapHandle",L"NewAllocAddress",L"OldAllocAddress",L"NewAllocSize",L"OldAllocSize",L"SourceId"} :
  std::vector<std::wstring>{L"HeapHandle",L"FreeAddress",L"SourceId"};
 if(group.schemaStatus!=0 || group.fields.size()!=expected.size() || event->UserDataLength!=group.payloadLength){++ledger.rejected;return;}
 uint64_t values[6]{};unsigned offset=0;
 for(size_t i=0;i<expected.size();++i){
  const auto& field=group.fields[i];
  if(field.name!=expected[i] || field.length!=(i==expected.size()-1?4:8) || offset+field.length>event->UserDataLength){++ledger.rejected;return;}
  memcpy(&values[i],static_cast<const unsigned char*>(event->UserData)+offset,field.length);offset+=field.length;
 }
 if(type==33)ledger.add(values[0],values[2],values[1]);
 if(type==36)ledger.remove(values[0],values[1]);
 if(type==34)ledger.reallocate(values[0],values[2],values[1],values[3]);
}
static void WINAPI eventRecord(EVENT_RECORD* event) {
 if (!IsEqualGUID(event->EventHeader.ProviderId, heapGuid)) return;
 // Classic heap events encode their event type in Opcode, not Id.
 const unsigned type = event->EventHeader.EventDescriptor.Opcode;
 auto& group = groups[{event->EventHeader.ProcessId,type}];
 if (group.count++ != 0) {account(event,group,type);return;}
 group.payloadLength=event->UserDataLength;
 const auto* bytes=static_cast<const unsigned char*>(event->UserData);
 for(unsigned i=0;i<event->UserDataLength && i<80;++i) group.sample.push_back(bytes[i]);
 ULONG size=0;
 ULONG status=TdhGetEventInformation(event,0,nullptr,nullptr,&size);
 if(status!=ERROR_INSUFFICIENT_BUFFER) {group.schemaStatus=status;return;}
 std::vector<unsigned char> storage(size);
 auto* info=reinterpret_cast<TRACE_EVENT_INFO*>(storage.data());
 status=TdhGetEventInformation(event,0,nullptr,info,&size);
 group.schemaStatus=status;
 if(status!=ERROR_SUCCESS) return;
 for(ULONG i=0;i<info->TopLevelPropertyCount;++i) {
  const auto& prop=info->EventPropertyInfoArray[i];
  if(prop.Flags & PropertyStruct) continue;
  auto* name=reinterpret_cast<wchar_t*>(storage.data()+prop.NameOffset);
  PROPERTY_DATA_DESCRIPTOR descriptor{};
  descriptor.PropertyName=reinterpret_cast<ULONGLONG>(name);
  descriptor.ArrayIndex=ULONG_MAX;
  ULONG length=0;
  if(TdhGetPropertySize(event,0,nullptr,1,&descriptor,&length)!=ERROR_SUCCESS) continue;
  uint64_t value=0;
  if(length>8 || TdhGetProperty(event,0,nullptr,1,&descriptor,length,reinterpret_cast<PBYTE>(&value))!=ERROR_SUCCESS) continue;
  group.fields.push_back({name,prop.nonStructType.InType,length,value});
 }
 account(event,group,type);
}
int wmain(int argc,wchar_t** argv) {
 if(argc==2 && std::wstring(argv[1])==L"--self-test") {
  Ledger l;l.add(1,10,100);l.add(1,20,200);l.remove(1,10);
  l.reallocate(1,10,20,200);l.remove(1,20);l.remove(1,30);
  if(l.bytes!=0 || l.peak!=300 || l.requested!=300 || l.reallocRequested!=200 || l.reallocMirroredAlloc!=1 || l.reallocOldMissing!=1 || l.unknownFree!=1 || l.duplicates!=0)return 5;
  l.add(1,40,50);l.reallocate(1,40,40,80);
  if(l.bytes!=80 || l.live.size()!=1 || l.duplicates!=0)return 6;
  std::cout<<"ETW ledger positive controls passed\n";return 0;
 }
 if(argc!=2) {std::cerr<<"Usage: audio_analysis_a0_etl trace.etl\n";return 2;}
 EVENT_TRACE_LOGFILEW log{};
 log.LogFileName=argv[1];
 log.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;
 log.EventRecordCallback=eventRecord;
 TRACEHANDLE handle=OpenTraceW(&log);
 if(handle==INVALID_PROCESSTRACE_HANDLE) {std::cerr<<"OpenTrace error "<<GetLastError();return 3;}
 ULONG status=ProcessTrace(&handle,1,nullptr,nullptr);
 CloseTrace(handle);
 if(status!=ERROR_SUCCESS) {std::cerr<<"ProcessTrace error "<<status;return 4;}
 std::cout<<"{\"groups\":[";
 bool first=true;
 for(const auto& entry:groups) {
  if(!first) std::cout<<',';first=false;
  const auto& g=entry.second;
  std::cout<<"{\"pid\":"<<entry.first.first<<",\"type\":"<<entry.first.second<<",\"count\":"<<g.count
   <<",\"payload_length\":"<<g.payloadLength<<",\"schema_status\":"<<g.schemaStatus<<",\"fields\":[";
  bool firstField=true;
  for(const auto& f:g.fields) {
   if(!firstField)std::cout<<',';firstField=false;
   std::string name;for(wchar_t ch:f.name)name.push_back(ch>=32 && ch<127 && ch!=L'"' && ch!=L'\\'?static_cast<char>(ch):'?');
   std::cout<<"{\"name\":\""<<name<<"\",\"type\":"<<f.type<<",\"length\":"<<f.length<<",\"value\":"<<f.value<<'}';
  }
  std::cout<<"],\"sample_bytes\":[";
  for(size_t i=0;i<g.sample.size();++i){if(i)std::cout<<',';std::cout<<g.sample[i];}
  std::cout<<"]}";
 }
 std::cout<<"],\"observed_allocation_ledgers\":[";
 first=true;
 for(const auto& entry:ledgers){
  if(!first)std::cout<<',';first=false;const auto& l=entry.second;
  std::cout<<"{\"pid\":"<<entry.first<<",\"observed_new_block_requested_bytes\":"<<l.requested<<",\"realloc_event_requested_bytes\":"<<l.reallocRequested<<",\"largest_request_bytes\":"<<l.largest
   <<",\"observed_live_peak_bytes\":"<<l.peak<<",\"observed_live_final_bytes\":"<<l.bytes<<",\"observed_live_final_count\":"<<l.live.size()
   <<",\"unmatched_frees\":"<<l.unknownFree<<",\"realloc_old_address_missing\":"<<l.reallocOldMissing<<",\"realloc_new_address_already_observed\":"<<l.reallocMirroredAlloc
   <<",\"duplicate_live_addresses\":"<<l.duplicates<<",\"rejected_layouts\":"<<l.rejected<<'}';
 }
 std::cout<<"],\"limits\":\"Observed-live tracking excludes allocations already live before capture and non-heap allocation paths; it is not whole-process peak memory or Dart managed-object accounting.\"}\n";
 return 0;
}
