#pragma once
#include <rubberband/RubberBandStretcher.h>

class StretchWorker final:public juce::Thread{
public:
 struct Request{std::shared_ptr<const AudioData> audio;int begin=0,end=0,start=0;double tempo=120,host=120,pitch=0;std::shared_ptr<const WarpArrangement> arrangement;int origin=0;std::shared_ptr<const std::map<size_t,size_t>> keyMap;double maxSeconds=600;bool varispeed=false;bool operator==(const Request& x)const{return varispeed==x.varispeed&&audio==x.audio&&begin==x.begin&&end==x.end&&start==x.start&&std::abs(tempo-x.tempo)<.001&&std::abs(host-x.host)<.001&&std::abs(pitch-x.pitch)<.001&&(!arrangement&&!x.arrangement||(arrangement&&x.arrangement&&origin==x.origin&&(*arrangement==*x.arrangement)))&&(!keyMap&&!x.keyMap||(keyMap&&x.keyMap&&*keyMap==*x.keyMap));}};
 struct Result{Request request;juce::AudioBuffer<float> buffer;std::vector<std::pair<size_t,size_t>> playbackMap;bool playbackMapPrepared=false;void prepareMap(){playbackMap.clear();if(request.keyMap)for(auto pair:*request.keyMap)playbackMap.push_back(pair);playbackMapPrepared=true;}};
private:
 const bool finePreview;const juce::Thread::Priority priority;juce::CriticalSection mutex;juce::WaitableEvent wake;Request wanted;uint64_t serial=0;std::shared_ptr<const Result> ready;std::vector<std::shared_ptr<const Result>> cache;
public:
 std::atomic<bool> busy{false};
 explicit StretchWorker(bool start=true,juce::Thread::Priority workerPriority=juce::Thread::Priority::normal,bool highQualityPreview=false):Thread("Slice Groups melodic stretch"),finePreview(highQualityPreview),priority(workerPriority){if(start)startThread(priority);}
 ~StretchWorker()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 void request(Request request){const juce::ScopedLock g(mutex);if(request==wanted)return;wanted=std::move(request);++serial;for(auto it=cache.begin();it!=cache.end();++it)if((*it)->request==wanted){ready=*it;auto hit=*it;cache.erase(it);cache.push_back(hit);busy=false;wake.signal();return;}busy=true;if(!isThreadRunning())startThread(priority);wake.signal();}
 std::shared_ptr<const Result> result(){const juce::ScopedTryLock g(mutex);return g.isLocked()?ready:nullptr;}
 static std::shared_ptr<Result> render(const Request& request,std::function<bool()> cancel={},bool preview=false){
  if(!request.audio||request.end<=request.begin)return {};int frames=request.end-request.begin,channels=juce::jmin(2,request.audio->buffer.getNumChannels());double ratio=request.tempo/request.host;
  if(request.arrangement){
   if(request.host<=0||request.tempo<=0||frames*ratio>request.audio->rate*request.maxSeconds)return {};
   auto result=std::make_shared<Result>();result->request=request;result->prepareMap();int expected=juce::jmax(1,(int)std::round(frames*ratio));result->buffer.setSize(channels,expected);result->buffer.clear();double spb=request.audio->rate*60/request.host,firstBeat=(request.begin-request.origin)/request.audio->rate*request.tempo/60;
   for(const auto& chunk:*request.arrangement){if(cancel&&cancel())return {};if(!chunk.valid(request.audio->buffer.getNumSamples()))return {};if(chunk.end()<=firstBeat||chunk.begin()>=firstBeat+frames/request.audio->rate*request.tempo/60)continue;
    Request child{request.audio,chunk.sourceBegin(),chunk.sourceEnd(),chunk.sourceBegin(),(chunk.end()-chunk.begin())*spb/(chunk.sourceEnd()-chunk.sourceBegin())*request.host,request.host,request.pitch};
    if(chunk.points.size()>2){auto map=std::make_shared<std::map<size_t,size_t>>();bool nonlinear=false;double ratio=(chunk.end()-chunk.begin())*spb/(chunk.sourceEnd()-chunk.sourceBegin());for(size_t n=1;n+1<chunk.points.size();++n){size_t source=chunk.points[n].source-chunk.sourceBegin(),target=(size_t)std::round((chunk.points[n].beat-chunk.begin())*spb);(*map)[source]=target;nonlinear|=std::abs(target-source*ratio)>.51;}if(nonlinear)child.keyMap=map;}
    auto rendered=render(child,cancel,preview);if(!rendered)return {};int position=(int)std::round((chunk.begin()-firstBeat)*spb),count=rendered->buffer.getNumSamples(),fade=juce::jmax(1,juce::jmin(count/2,(int)(request.audio->rate*.001)));
    for(int n=juce::jmax(0,-position);n<juce::jmin(count,expected-position);++n){float gain=juce::jmin(1.f,juce::jmin((float)n/fade,(float)(count-1-n)/fade));for(int ch=0;ch<channels;++ch)result->buffer.addSample(ch,position+n,rendered->buffer.getSample(ch,n)*gain);}
   }
   int rotation=juce::jlimit(0,expected-1,(int)std::round((request.start-request.begin)*ratio));if(rotation){juce::AudioBuffer<float> rotated(channels,expected);for(int ch=0;ch<channels;++ch)for(int n=0;n<expected;++n)rotated.setSample(ch,n,result->buffer.getSample(ch,(n+rotation)%expected));result->buffer=std::move(rotated);}return result;
  }
  if(ratio<.03125||ratio>32||frames*ratio>request.audio->rate*request.maxSeconds)return {};
  juce::AudioBuffer<float> input(channels,frames);input.clear();int rotation=juce::jlimit(0,frames-1,request.start-request.begin);
  for(int ch=0;ch<channels;++ch)for(int n=0;n<frames;++n){int source=request.begin+(rotation+n)%frames;if(source<request.audio->buffer.getNumSamples())input.setSample(ch,n,request.audio->buffer.getSample(ch,source));}
  if(cancel&&cancel())return {};if(std::abs(ratio-1)<.000001&&std::abs(request.pitch)<.000001&&!request.keyMap){auto result=std::make_shared<Result>();result->request=request;result->prepareMap();result->buffer=std::move(input);return result;}
  if(request.varispeed){auto result=std::make_shared<Result>();result->request=request;result->prepareMap();int count=juce::jmax(1,(int)std::round(frames*ratio));result->buffer.setSize(channels,count);std::vector<std::pair<double,double>> map{{0,0}};if(request.keyMap)for(auto point:*request.keyMap)map.push_back({(double)point.second,(double)point.first});map.push_back({(double)count,(double)frames});size_t interval=1;for(int n=0;n<count;++n){if((n%4096)==0&&cancel&&cancel())return {};while(interval+1<map.size()&&n>=map[interval].first)++interval;auto a=map[interval-1],b=map[interval];double source=a.second+(n-a.first)/juce::jmax(1.,b.first-a.first)*(b.second-a.second);source=juce::jlimit(0.,(double)frames-1,source);int i=(int)source;for(int ch=0;ch<channels;++ch){float x=input.getSample(ch,i),y=input.getSample(ch,juce::jmin(i+1,frames-1));result->buffer.setSample(ch,n,x+(float)(source-i)*(y-x));}}return result;}
  if(request.keyMap&&std::abs(request.pitch)>.000001){auto pitchRequest=request;pitchRequest.keyMap.reset();pitchRequest.host=pitchRequest.tempo;auto pitched=render(pitchRequest,cancel,preview);if(!pitched)return {};input=pitched->buffer;}
  // R2 respects offline keyframes more tightly; keep R3 for unconstrained melodic stretching.
  using RB=RubberBand::RubberBandStretcher;RB engine((size_t)request.audio->rate,channels,RB::OptionProcessOffline|((preview||request.keyMap)?RB::OptionEngineFaster:RB::OptionEngineFiner)|RB::OptionThreadingNever|RB::OptionPitchHighQuality,ratio,std::pow(2.,(request.keyMap?0:request.pitch)/12));
  engine.setExpectedInputDuration(frames);engine.setMaxProcessSize(4096);const float* read[2]{};float* write[2]{};
  for(int a=0;a<frames;a+=4096){if(cancel&&cancel())return {};int count=juce::jmin(4096,frames-a);for(int ch=0;ch<channels;++ch)read[ch]=input.getReadPointer(ch,a);engine.study(read,count,a+count==frames);}
  // Apply the map after study: studying may rebuild the offline stretch profile.
  if(request.keyMap)engine.setKeyFrameMap(*request.keyMap);
  auto result=std::make_shared<Result>();result->request=request;result->prepareMap();int expected=(int)std::round(frames*ratio);result->buffer.setSize(channels,expected+8192);result->buffer.clear();int received=0;
  auto drain=[&]{while(engine.available()>0){int count=engine.available();if(received+count>result->buffer.getNumSamples())result->buffer.setSize(channels,received+count,true,true);for(int ch=0;ch<channels;++ch)write[ch]=result->buffer.getWritePointer(ch,received);received+=(int)engine.retrieve(write,count);}};
  for(int a=0;a<frames;a+=4096){if(cancel&&cancel())return {};int count=juce::jmin(4096,frames-a);for(int ch=0;ch<channels;++ch)read[ch]=input.getReadPointer(ch,a);engine.process(read,count,a+count==frames);drain();}
  drain();if(received<expected)result->buffer.clear(received,expected-received);result->buffer.setSize(channels,expected,true,true);return result;
 }
 void cachePrepared(std::shared_ptr<const Result> result){if(!result)return;const juce::ScopedLock g(mutex);for(auto& item:cache)if(item->request==result->request)return;cache.push_back(std::move(result));size_t bytes=0;for(auto& item:cache)bytes+=(size_t)item->buffer.getNumSamples()*item->buffer.getNumChannels()*sizeof(float);while(cache.size()>1&&(bytes>64*1024*1024||cache.size()>8)){bytes-=(size_t)cache.front()->buffer.getNumSamples()*cache.front()->buffer.getNumChannels()*sizeof(float);cache.erase(cache.begin());}}
 std::shared_ptr<const Result> cached(const Request& request){const juce::ScopedLock g(mutex);for(auto& item:cache)if(item->request==request)return item;return {};}
 void run()override{uint64_t done=0;while(!threadShouldExit()){wake.wait(100);Request request;uint64_t token;{const juce::ScopedLock g(mutex);if(serial==done)continue;request=wanted;token=serial;if(!busy){done=token;continue;}}
  auto cancelled=[&]{const juce::ScopedLock g(mutex);return threadShouldExit()||serial!=token;};
  auto preview=render(request,cancelled,!finePreview);{const juce::ScopedLock g(mutex);if(token==serial&&preview)ready=preview;}
  auto rendered=cancelled()?nullptr:(finePreview||request.keyMap)?preview:render(request,cancelled);{const juce::ScopedLock g(mutex);done=token;if(token==serial){if(rendered){ready=rendered;cache.push_back(rendered);size_t bytes=0;for(auto& item:cache)bytes+=(size_t)item->buffer.getNumSamples()*item->buffer.getNumChannels()*sizeof(float);while(cache.size()>1&&(bytes>64*1024*1024||cache.size()>8)){bytes-=(size_t)cache.front()->buffer.getNumSamples()*cache.front()->buffer.getNumChannels()*sizeof(float);cache.erase(cache.begin());}}busy=false;}}}}
};
