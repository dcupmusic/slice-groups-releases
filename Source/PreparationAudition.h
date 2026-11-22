#pragma once
// Short reference audition, separate from the group's loaded instrument/settings.
class PreparationAudition {
 std::shared_ptr<const AudioData> owner;std::vector<std::shared_ptr<const AudioData>> retired;
 std::atomic<const AudioData*> published{nullptr},hazard{nullptr};std::atomic<uint64_t> generation{0};std::atomic<double> frequency{0};
 uint64_t applied=0;double position=0,phase=0,age=0;const AudioData* active=nullptr;
public:
 void sample(std::shared_ptr<const AudioData> data){if(owner)retired.push_back(owner);owner=std::move(data);published.store(owner.get());frequency=0;++generation;retired.erase(std::remove_if(retired.begin(),retired.end(),[&](auto& p){return p.use_count()==1&&p.get()!=hazard.load();}),retired.end());}
 void piano(double note){frequency=440*std::exp2((note-69)/12);published=nullptr;++generation;}
 void stop(){frequency=0;published=nullptr;++generation;}
 void render(juce::AudioBuffer<float>& out,double rate){auto serial=generation.load();const AudioData* source;do{source=published.load();hazard.store(source);}while(source!=published.load());if(serial!=applied){applied=serial;active=source;position=phase=age=0;}double hz=frequency.load();for(int n=0;n<out.getNumSamples();++n){double envelope=juce::jmin(1.,age/.003)*std::exp(-age*3.5);if(hz>0&&age<1.8){double value=.14*envelope*(std::sin(phase)+.3*std::exp(-age*4)*std::sin(2*phase)+.12*std::exp(-age*7)*std::sin(3*phase));for(int ch=0;ch<juce::jmin(2,out.getNumChannels());++ch)out.addSample(ch,n,(float)value);phase=std::fmod(phase+2*juce::MathConstants<double>::pi*hz/rate,2*juce::MathConstants<double>::pi);}else if(source&&source==active&&position<source->buffer.getNumSamples()&&age<5){int i=(int)position,j=juce::jmin(i+1,source->buffer.getNumSamples()-1);float gain=(float)(.7*juce::jmin(1.,age/.003)*juce::jlimit(0.,1.,(source->buffer.getNumSamples()-position)/(source->rate*.005)));for(int ch=0;ch<juce::jmin(2,out.getNumChannels());++ch){auto* p=source->buffer.getReadPointer(juce::jmin(ch,source->buffer.getNumChannels()-1));out.addSample(ch,n,gain*(p[i]+(float)(position-i)*(p[j]-p[i])));}position+=source->rate/rate;}age+=1/rate;}hazard=nullptr;}
};
