#pragma once
// Original EMU-II-inspired implementation, not TAL code or an exact hardware model.
struct VintageSettings {
 bool enabled=false,filter=true;double rate=27000,colour=0,hiss=0;
 bool operator==(const VintageSettings& b)const{return enabled==b.enabled&&filter==b.filter&&rate==b.rate&&colour==b.colour&&hiss==b.hiss;}
 void write(juce::XmlElement& x)const{x.setAttribute("emuEnabled",enabled);x.setAttribute("emuFilter",filter);x.setAttribute("emuRate",rate);x.setAttribute("emuColour",colour);x.setAttribute("emuHiss",hiss);}
 void read(const juce::XmlElement& x){enabled=x.getBoolAttribute("emuEnabled");filter=x.getBoolAttribute("emuFilter",true);rate=juce::jlimit(4000.,48000.,x.getDoubleAttribute("emuRate",27000));colour=juce::jlimit(-100.,100.,x.getDoubleAttribute("emuColour"));hiss=juce::jlimit(0.,100.,x.getDoubleAttribute("emuHiss"));}
};
class VintageResampler final:public juce::Thread {
public:
 struct Entry{std::shared_ptr<const AudioData> source;juce::AudioBuffer<float> pcm;};
 struct Result:std::enable_shared_from_this<Result>{VintageSettings settings;std::vector<Entry> entries;const juce::AudioBuffer<float>* find(const AudioData* source)const{for(auto& e:entries)if(e.source.get()==source)return &e.pcm;return nullptr;}};
 struct Request{VintageSettings settings;std::vector<std::shared_ptr<const AudioData>> sources;};
 std::atomic<bool> busy{false},limited{false};
private:
 std::shared_ptr<const Request> wanted;std::shared_ptr<const Result> ready;
 std::atomic<const Result*> published{nullptr},hazard{nullptr};std::vector<std::shared_ptr<const Result>> retired;juce::WaitableEvent wake;
 void publish(std::shared_ptr<const Result> next){if(ready)retired.push_back(ready);ready=std::move(next);published.store(ready.get());retired.erase(std::remove_if(retired.begin(),retired.end(),[&](auto& r){return r.use_count()==1&&r.get()!=hazard.load();}),retired.end());}
public:
 VintageResampler():Thread("Vintage sample conversion"){}~VintageResampler()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 static juce::AudioBuffer<float> convert(const AudioData& a,const VintageSettings& settings,std::function<bool()> cancel={}){
  int length=a.buffer.getNumSamples(),channels=juce::jmin(2,a.buffer.getNumChannels());if(length<=0||channels<=0||a.rate<=0)return {};
  double sr=juce::jmin(a.rate,settings.rate);int count=juce::jmax(2,(int)std::ceil(length/a.rate*sr)+1);juce::AudioBuffer<float> low(channels,count),out(channels,length);
  const double mu=255,logMu=std::log1p(mu),decodeMu=mu*std::exp2(settings.colour/100),decodeLog=std::log1p(decodeMu);
  double coefficient=-std::expm1(-2*juce::MathConstants<double>::pi*sr*.42/a.rate);
  juce::Random noise(0x454d5532);
  for(int ch=0;ch<channels;++ch){double input1=0,input2=0,predictor=0,reconstruction1=0,reconstruction2=0;int scanned=0;auto* raw=a.buffer.getReadPointer(ch);
   for(int n=0;n<count;++n){if((n&2047)==0&&cancel&&cancel())return {};double point=juce::jmin((double)length-1,n*a.rate/sr);int i=(int)point,j=juce::jmin(i+1,length-1);double x=0;
    if(settings.filter){while(scanned<=i){input1+=coefficient*(raw[scanned]-input1);input2+=coefficient*(input1-input2);++scanned;}x=input2;}else x=raw[i]+(point-i)*(raw[j]-raw[i]);
    // Eight-bit signed mu-law differential coding with decoder feedback. The
    // feedback bounds integration drift; Colour changes the decoder curve.
    double delta=juce::jlimit(-1.,1.,x-predictor);double encoded=std::copysign(std::log1p(mu*std::abs(delta))/logMu,delta);encoded=std::round(encoded*127)/127;
    double decoded=std::copysign(std::expm1(std::abs(encoded)*decodeLog)/decodeMu,encoded);predictor=juce::jlimit(-1.2,1.2,predictor+decoded);
    low.setSample(ch,n,(float)predictor);
   }
   auto* encoded=low.getReadPointer(ch);for(int n=0;n<length;++n){if((n&4095)==0&&cancel&&cancel())return {};double point=n*sr/a.rate;int i=juce::jmin((int)point,count-2);double value=encoded[i]+(point-i)*(encoded[i+1]-encoded[i]);if(settings.filter){reconstruction1+=coefficient*(value-reconstruction1);reconstruction2+=coefficient*(reconstruction1-reconstruction2);value=reconstruction2;}value+=(noise.nextFloat()*2-1)*settings.hiss/100*.002;out.setSample(ch,n,(float)value);}
  }
  return out;
 }
 void request(VintageSettings settings,std::vector<std::shared_ptr<const AudioData>> sources){if(!settings.enabled)sources.clear();auto old=std::atomic_load(&wanted);if(old&&old->settings==settings&&old->sources==sources)return;auto next=std::make_shared<Request>();next->settings=settings;next->sources=std::move(sources);std::atomic_store(&wanted,std::shared_ptr<const Request>(next));busy=settings.enabled&&!next->sources.empty();limited=false;if(!isThreadRunning()&&busy)startThread(juce::Thread::Priority::low);wake.signal();}
 std::shared_ptr<const Result> audioResult(){const Result* r;do{r=published.load();hazard.store(r);}while(r!=published.load());auto next=r?r->shared_from_this():std::shared_ptr<const Result>{};hazard.store(nullptr);return next;}
 void run()override{std::shared_ptr<const Request> done;while(!threadShouldExit()){auto next=std::atomic_load(&wanted);if(!next||next==done){wake.wait(30);continue;}auto result=std::make_shared<Result>();result->settings=next->settings;size_t bytes=0;auto cancel=[&]{return threadShouldExit()||next!=std::atomic_load(&wanted);};for(auto& source:next->sources){size_t size=(size_t)source->buffer.getNumSamples()*juce::jmin(2,source->buffer.getNumChannels())*sizeof(float);if(bytes+size>256*1024*1024){limited=true;continue;}auto pcm=convert(*source,next->settings,cancel);if(cancel())break;if(pcm.getNumSamples()){result->entries.push_back({source,std::move(pcm)});bytes+=size;}}if(!cancel()){publish(result);done=next;busy=false;}}publish({});}
};
