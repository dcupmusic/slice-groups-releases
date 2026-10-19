#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Display-only summaries. Never built or queried by the audio callback.
struct FrequencyWaveform {
 static constexpr int stride=64;
 struct Peak {
  float low=0,high=0;std::array<float,3> energy{};bool present=false;
  void merge(const Peak& other){low=juce::jmin(low,other.low);high=juce::jmax(high,other.high);for(int i=0;i<3;++i)energy[i]+=other.energy[i];present|=other.present;}
  juce::Colour colour()const{float r=std::sqrt(juce::jmax(0.f,energy[0])),g=std::sqrt(juce::jmax(0.f,energy[1])),b=std::sqrt(juce::jmax(0.f,energy[2]));float scale=juce::jmax(r,g,b);if(scale<1e-8f)return juce::Colour(0xff555555);return juce::Colour::fromFloatRGBA(.1f+.9f*r/scale,.1f+.9f*g/scale,.1f+.9f*b/scale,1);}
 };
 struct State {std::array<float,2> bass,body;State():bass{},body{} {}};
 std::vector<std::vector<Peak>> levels;State tail;int length=0,validBegin=0,validEnd=0;
 bool build(const juce::AudioBuffer<float>& audio,double rate,int channel=0,int channels=2,int begin=0,int end=-1,State initial=State{},std::function<bool()> cancel={}){
  length=audio.getNumSamples();if(length<=0||channel<0||channel>=audio.getNumChannels()||rate<=0){levels.clear();length=validBegin=validEnd=0;return true;}validBegin=juce::jlimit(0,length,begin);validEnd=end<0?length:juce::jlimit(validBegin,length,end);levels.clear();levels.emplace_back((length+stride-1)/stride);tail=initial;
  float slow=1-std::exp((float)(-2*juce::MathConstants<double>::pi*200/rate)),fast=1-std::exp((float)(-2*juce::MathConstants<double>::pi*2000/rate));channels=juce::jlimit(1,2,juce::jmin(channels,audio.getNumChannels()-channel));
  for(int n=validBegin;n<validEnd;++n){if((n&4095)==0&&cancel&&cancel())return false;auto& peak=levels[0][n/stride];peak.present=true;for(int ch=0;ch<channels;++ch){float x=audio.getSample(channel+ch,n);peak.low=juce::jmin(peak.low,x);peak.high=juce::jmax(peak.high,x);tail.bass[ch]+=slow*(x-tail.bass[ch]);tail.body[ch]+=fast*(x-tail.body[ch]);float low=tail.bass[ch],mid=tail.body[ch]-low,high=x-tail.body[ch];peak.energy[0]+=low*low;peak.energy[1]+=mid*mid;peak.energy[2]+=high*high;}}
  while(levels.back().size()>1){auto& last=levels.back();std::vector<Peak> next((last.size()+1)/2);for(size_t i=0;i<next.size();++i){next[i]=last[i*2];if(i*2+1<last.size())next[i].merge(last[i*2+1]);}levels.push_back(std::move(next));}return true;
 }
 Peak range(int begin,int end)const{Peak result;if(levels.empty())return result;begin=juce::jlimit(validBegin,validEnd,begin);end=juce::jlimit(begin,validEnd,end);if(begin==end)return result;int bin=begin/stride,limit=(end+stride-1)/stride;while(bin<limit){int level=0;while(level+1<(int)levels.size()&&bin%(1<<(level+1))==0&&bin+(1<<(level+1))<=limit)++level;result.merge(levels[level][bin>>level]);bin+=1<<level;}return result;}
};

class FrequencyWaveformWorker final:public juce::Thread {
public:
 struct Result{std::shared_ptr<const juce::AudioBuffer<float>> source;FrequencyWaveform waveform;};
private:
 struct Request{std::shared_ptr<const juce::AudioBuffer<float>> source;double rate=44100;};
 std::shared_ptr<const Request> request;std::shared_ptr<const Result> ready;std::atomic<uint64_t> generation{0};juce::WaitableEvent wake;
public:
 FrequencyWaveformWorker():Thread("Track waveform colours"){}~FrequencyWaveformWorker()override{signalThreadShouldExit();++generation;wake.signal();stopThread(-1);}
 void setSource(std::shared_ptr<const juce::AudioBuffer<float>> source,double rate){auto old=std::atomic_load(&request);if((!old&&!source)||(old&&old->source==source&&old->rate==rate))return;auto next=std::make_shared<Request>();next->source=std::move(source);next->rate=rate;std::atomic_store(&request,std::shared_ptr<const Request>(next));++generation;std::atomic_store(&ready,std::shared_ptr<const Result>{});if(!isThreadRunning())startThread();wake.signal();}
 auto result()const{return std::atomic_load(&ready);}
 void run()override{std::shared_ptr<const Request> done;while(!threadShouldExit()){auto current=std::atomic_load(&request);if(!current||!current->source){done.reset();wake.wait(50);continue;}if(current==done){wake.wait(50);continue;}auto serial=generation.load();auto next=std::make_shared<Result>();next->source=current->source;auto cancelled=[&]{return threadShouldExit()||generation.load()!=serial||std::atomic_load(&request)!=current;};if(next->waveform.build(*current->source,current->rate,0,2,0,-1,{},cancelled)&&!cancelled()&&std::atomic_load(&request)==current){std::atomic_store(&ready,std::shared_ptr<const Result>(next));done=current;}}}
};
