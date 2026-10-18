#include "analysis_a0.hpp"
#include "clock_review.hpp"
#include "clock_candidate.hpp"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
using namespace shipglows::audio::a0;
using Clock=std::chrono::steady_clock;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::vector<float> fixture(size_t n){std::vector<float>x(n*2);for(size_t i=0;i<n;++i){x[2*i]=float(.5*std::sin(2*pi*1000*i/48000));x[2*i+1]=-x[2*i];}return x;}
static std::unique_ptr<Processor> process(unsigned mask,const std::vector<float>&x,size_t chunk){auto p=std::make_unique<Processor>(mask);for(size_t f=0;f<x.size()/2;f+=chunk){size_t n=std::min(chunk,x.size()/2-f);p->push(x.data()+2*f,n,96000+f);}p->finish();return p;}
static void tests(){
 {auto candidate=[](uint64_t ticks,uint64_t freq=384000,int64_t status=0,bool active=true,bool consistent=true){return clock_candidate(ticks,freq,10,900,1100,2000,100,status,active,consistent);};
  auto c=candidate(81);require(c.available&&c.output_frame==10&&c.fractional_frame==.125&&c.age_ns==1000,"clock candidate units and fraction");
  require(!candidate(0).available&&!candidate(81,384000,1).available&&!candidate(81,0).available,"startup and inaccurate clock rejected");
  require(!candidate(81,384000,0,false).available&&!candidate(81,384000,0,true,false).available,"inactive or inconsistent source rejected");
  require(!candidate(801).available&&!candidate(UINT64_MAX,1).available,"outside submitted domain and integer overflow rejected");
  require(!clock_candidate(81,384000,10,1001,1100,2000,100,0,true,true).available,"QPC outside bracket rejected");
  require(!clock_candidate(81,384000,10,900,1100,1000,100,0,true,true).available,"future query rejected");
  require(!clock_candidate(81,384000,10,900,1100,250001001,100,0,true,true).available,"age limit rejected");
  require(!clock_candidate(81,384000,UINT64_MAX,900,1100,2000,100,0,true,true).available,"QPC conversion overflow rejected");
 }
 {ClockReview c;require(!c.rate_available(),"clock startup unavailable");
  c.observe(10,384000,100,1000,2000,0);c.observe(3843850,384000,100000100,3000,4500,0);
  require(c.rate_available()&&std::abs(c.relative_rate_ppm()-1000)<1e-6,"clock units and observed rate");
  c.observe(0,384000,0,5000,6000,1);require(c.inaccurate==1&&c.accurate==2&&c.max_query_ns==1500,"S_FALSE not accurate");
  c.observe(3843860,48000,100000200,7000,8000,0);require(!c.rate_available()&&c.frequency_changes==1,"frequency change invalidates rate");
  ClockReview r;r.observe(100,384000,100,0,1,0);r.observe(99,384000,99,0,1,0);
  require(r.regressions==1&&!r.rate_available(),"clock regression invalidates rate");
  r.observe(200,384000,200,2,1,0);require(r.inaccurate==1,"invalid query bracket");
  ClockReview startup;startup.observe(0,384000,0,0,1,0);
  startup.observe(383000,384000,10000000,0,1,0);
  require(!startup.after_startup_rate_available(),"one after-startup sample unavailable");
  startup.observe(11903000,384000,310000000,0,1,0);
  require(startup.relative_rate_ppm() < -80&&startup.after_startup_rate_available()&&std::abs(startup.after_startup_relative_rate_ppm())<1e-6,"startup and subsequent interval remain distinct");
  require(startup.after_startup_max_nominal_residual_ns==0,"constant offset excluded from subsequent residual");
  startup.observe(12287000,384000,320000100,0,1,0);
  require(std::abs(startup.after_startup_max_nominal_residual_ns-10000)<1e-6,"clock timestamp residual in ns");
  ClockReview drift;
  for(uint64_t second=0;second<=601;++second)
   drift.observe(second*1000150,1000000,second*10000000,0,1,0);
  require(drift.local_completed_windows==600&&std::abs(drift.local_max_nominal_residual_ns-150000)<1e-6,"local windows retain closing sample with 150 ppm drift");
  require(std::abs(drift.after_startup_max_nominal_residual_ns-90000000)<1e-6,"global residual retains cumulative drift");
  const auto windows=drift.local_completed_windows;
  drift.observe(0,1000000,0,0,1,0);
  drift.observe(603090450,1000000,6030000000,0,1,0);
  require(drift.regressions==1&&drift.local_completed_windows==windows,"clock regression stops local calibration");
 }
 // Independent original full-bin band transform, including recurrence and EOF.
 for(unsigned mask:{11u,13u}){
  auto x=fixture(3001);auto p=process(mask,x,31);std::array<double,64> state{};
  for(size_t j=0;j<p->history_count();++j){const auto& r=p->history(j);
   for(size_t b=0;b<64;++b){double lo=20*std::pow(1000.,double(b)/64),hi=20*std::pow(1000.,double(b+1)/64),q=0;
    for(size_t k=1;k<=1024;++k){double a=(k-.5)*48000/2048,d=std::min(24000.,(k+.5)*48000/2048);q+=std::max(0.,std::min(d,hi)-std::max(a,lo))/(d-a)*double(r.spectrum[k]);}
    double target=q==0?0:std::pow(std::clamp((10*std::log10(2*q)+80)/80,0.,1.),.8);
    double tau=(target>state[b]?.020:.180);state[b]+=(1-std::exp(-double(r.valid_frames)/48000/tau))*(target-state[b]);
    require(r.bands[b]==float(state[b]),"sparse overlap equals original full-bin transform");
   }
  }
 }
 for(unsigned mask:{3u,11u,5u,13u})for(size_t n:{1u,479u,480u,481u,1023u,1024u,2048u,3001u}){
  auto x=fixture(n);auto a=process(mask,x,1);auto b=process(mask,x,479);
  require(a->history_count()==(n+479)/480,"EOF count");require(a->stats().fft_calls==((mask&8)?2*a->stats().records:0),"FFT mask");
  for(size_t j=0;j<a->history_count();++j){auto&r=a->history(j);auto&s=b->history(j);require(r.center==96000+j*480,"absolute center");require(r.valid_frames==std::min<size_t>(480,n-j*480),"partial hop");require(std::memcmp(r.levels.data(),s.levels.data(),24)==0,"levels partition");require(std::memcmp(r.visual_levels.data(),s.visual_levels.data(),24)==0,"transform partition");require(r.envelope==s.envelope&&r.spectrum==s.spectrum&&r.bands==s.bands,"optional partition");}
 }
 std::vector<float>constant(960, .5f);auto p=process(5,constant,31);auto&r=p->history(0);require(std::abs(r.levels[5]-.5)<1e-6,"RMS reference");double y=(1-std::exp(-.01/.02))*std::pow(.5,.8);require(std::abs(r.visual_levels[5]-y)<1e-6,"initial recurrence");
 std::vector<float>release(962,0);std::fill(release.begin(),release.begin()+960,.5f);auto partial=process(5,release,31);double released=y*std::exp(-(1./48000)/.180);require(partial->history_count()==2&&partial->latest()->valid_frames==1&&std::abs(partial->latest()->visual_levels[5]-released)<1e-6,"independent release partial dt");
 auto q=std::make_unique<Processor>(5);q->push(constant.data(),480,123);q->push(constant.data(),1,1,true);q->finish();require(q->history_count()==1&&q->history(0).continuity==2&&q->history(0).center==1,"gap reset");require(q->history(0).flags==1,"gap flag");
 std::vector<float>x(4096);for(size_t i=0;i<2048;++i){x[2*i]=float(.5*std::sin(2*pi*64*i/2048));x[2*i+1]=-x[2*i];}auto spectrum=process(13,x,31);auto&sr=spectrum->history(2);double sum=0,weighted=0;for(size_t k=0;k<=1024;++k)sum+=sr.spectrum[k];for(size_t n=0;n<2048;++n){int64_t at=960+int64_t(n)-1024;double v=(at<0||at>=2048)?0:x[size_t(at)*2];double w=.5-.5*std::cos(2*pi*n/2048);weighted+=v*v*w*w/768;}require(std::abs(sum-weighted)<1e-6,"Parseval anti-phase");
 for(size_t k:{0u,1u,63u,64u,65u,1024u}){std::complex<double>d=0.;for(size_t n=0;n<2048;++n){int64_t at=960+int64_t(n)-1024;double v=(at<0||at>=2048)?0:x[size_t(at)*2];double w=.5-.5*std::cos(2*pi*n/2048);d+=v*w*std::polar(1.,-2*pi*k*n/2048);}double power=(k==0||k==1024?1:2)*std::norm(d)/(2048*768);require(std::abs(power-sr.spectrum[k])<=1e-6+1e-5*std::abs(power),"direct DFT");}
 BlockQueue queue(480);BlockQueue::Header h;std::array<float,960>out{};for(unsigned i=0;i<16;++i)require(queue.try_push(constant.data(),480,i*480),"queue admission");require(!queue.try_push(constant.data(),480,7680),"whole overflow");for(unsigned i=0;i<16;++i){require(queue.try_pop(out.data(),h),"queue pop");require(h.position==i*480&&h.frames==480&&out[0]==.5,"queue content");}require(!queue.try_pop(out.data(),h)&&queue.drops()==1,"queue empty/drop");
 bool rejected=false;constant[0]=std::numeric_limits<float>::quiet_NaN();try{q=std::make_unique<Processor>(5);q->push(constant.data(),480,0);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"NaN rejected");
 for(unsigned mask:{3u,11u,5u,13u}){
  std::vector<float>impulse(4098,0);impulse[0]=2;impulse.back()=-3;
  auto a=process(mask,impulse,1);auto b=process(mask,impulse,4096);
  require(a->history(0).levels[0]==2&&a->latest()->levels[1]==3,"finite over-unity unclamped");
  for(size_t j=0;j<a->history_count();++j){require(a->history(j).levels==b->history(j).levels&&a->history(j).envelope==b->history(j).envelope&&a->history(j).spectrum==b->history(j).spectrum&&a->history(j).visual_levels==b->history(j).visual_levels,"first/last impulse partition");}
 }
 std::cout<<"A0 reference tests passed: four masks, chunk/EOF/absolute centers, initial recurrence, PCM reset, independent DFT/Parseval anti-phase, bounded whole queue, NaN rejection.\n";
}
static double percentile(std::vector<double>x,double fraction){std::sort(x.begin(),x.end());return x[std::min(x.size()-1,size_t(fraction*x.size()))];}
int main(int argc,char**argv){try{
 if(argc>1&&std::string(argv[1])=="--test"){tests();return 0;}
 const unsigned seconds=argc>1?unsigned(std::strtoul(argv[1],nullptr,10)):30;if(!seconds||seconds>3600)throw std::invalid_argument("seconds1..3600");
 std::cout<<"{\"kind\":\"synthetic_native_worker_a0\",\"sample_rate\":48000,\"seconds_per_mask\":"<<seconds<<",\"build\":\""<<
#ifdef NDEBUG
 "release"
#else
 "debug"
#endif
 <<"\",\"masks\":[";
 bool first=true;for(unsigned mask:{3u,11u,5u,13u}){
  auto p=std::make_unique<Processor>(mask);std::array<float,960>block{};std::vector<double>times;times.reserve(seconds*100);auto begin=Clock::now();for(uint64_t f=0;f<uint64_t(seconds)*48000;f+=480){for(size_t i=0;i<480;++i){double t=double(f+i)/48000;block[2*i]=float(.4*std::sin(2*pi*997*t)+.05*std::sin(2*pi*71*t));block[2*i+1]=-block[2*i];}auto start=Clock::now();p->push(block.data(),480,f);times.push_back(std::chrono::duration<double,std::micro>(Clock::now()-start).count());}p->finish();double elapsed=std::chrono::duration<double>(Clock::now()-begin).count();double total=0;for(double t:times)total+=t;if(!first)std::cout<<",";first=false;auto&s=p->stats();std::cout<<"{\"measurement_ns_including_fft\":"<<s.measurement_ns<<",\"transform_ns\":"<<s.transform_ns<<",\"fft_ns_subset_of_measurement\":"<<s.fft_ns<<",\"measurement_us_per_record\":"<<double(s.measurement_ns)/1000/s.records<<",\"transform_us_per_record\":"<<double(s.transform_ns)/1000/s.records<<",\"fft_us_per_record\":"<<double(s.fft_ns)/1000/s.records<<",\"mask\":"<<mask<<",\"records\":"<<s.records<<",\"fft_calls\":"<<s.fft_calls<<",\"processor_bytes\":"<<p->bytes()<<",\"pcm_copied_bytes\":"<<s.copied_bytes<<",\"history_overwrites\":"<<s.history_overwrites<<",\"wall_seconds_including_generator\":"<<elapsed<<",\"worker_us_mean\":"<<total/times.size()<<",\"worker_us_p95\":"<<percentile(times,.95)<<",\"worker_us_p99\":"<<percentile(times,.99)<<",\"worker_us_max\":"<<*std::max_element(times.begin(),times.end())<<"}";
 }
 std::cout<<"]}\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
