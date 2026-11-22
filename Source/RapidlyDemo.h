#pragma once
// Local evaluation adapter for the documented Rapidly C ABI. SDK/model stay external.
// No licence keys are registered: vendor demo watermarking remains enabled.
class RapidlyDemo {
 juce::DynamicLibrary library;using Handle=void*;
 Handle (*create)(const char*,int32_t,double)=nullptr;void (*destroy)(Handle)=nullptr;void (*reset)(Handle)=nullptr;
 void (*add)(Handle,const float*,int32_t)=nullptr;int32_t (*pending)(Handle)=nullptr;bool (*get)(Handle,float*,int32_t)=nullptr;
 int32_t (*count)(Handle)=nullptr;void (*name)(Handle,int32_t,char*,int32_t)=nullptr;void (*parameter)(Handle,int32_t,float)=nullptr;
 std::array<Handle,4> processors{};
 template<class T> void symbol(T& target,const char* n){target=reinterpret_cast<T>(library.getFunction(n));if(!target)throw std::runtime_error("Rapidly demo SDK API is missing");}
public:
 RapidlyDemo(){auto sdk=juce::SystemStats::getEnvironmentVariable("SLICE_GROUPS_RAPIDLY_SDK",SLICE_GROUPS_RAPIDLY_SDK);juce::File root(sdk);auto model=root.getChildFile("models/music-stem-separation-96ms.v1.1.rapidly");if(!model.existsAsFile()||!library.open(root.getChildFile("bin/macos/libRapidlyEngine.dylib").getFullPathName()))throw std::runtime_error("Rapidly demo SDK is unavailable. This local evaluation needs the external SDK folder.");
  symbol(create,"rapidlyCreateProcessor");symbol(destroy,"rapidlyDeleteProcessor");symbol(reset,"rapidlyResetProcessorState");symbol(add,"rapidlyAddAudioInterleaved");symbol(pending,"rapidlyGetNumOfPendingSamples");symbol(get,"rapidlyGetAudioInterleaved");symbol(count,"rapidlyGetNumOfOutputBusses");symbol(name,"rapidlyGetOutputBusName");symbol(parameter,"rapidlySetParameterValue");
  try{for(int stem=0;stem<4;++stem){auto& p=processors[stem];p=create(model.getFullPathName().toRawUTF8(),2,44100);if(!p)throw std::runtime_error("Rapidly demo processor could not load");bool found=false;for(int bus=0;bus<count(p);++bus){char label[128]{};name(p,bus,label,128);juce::String n(label);bool enabled=stem==0?n=="drums":stem==1?n=="bass":stem==2?n=="vocals":n=="guitar"||n=="piano"||n=="residual";parameter(p,0x100+bus,enabled?1.f:0.f);found|=enabled;}if(!found)throw std::runtime_error("Rapidly demo stem bus not found");}}catch(...){for(auto p:processors)if(p)destroy(p);throw;}
 }
 ~RapidlyDemo(){for(auto p:processors)if(p)destroy(p);}
 void restart(){for(auto p:processors)reset(p);}
 int process(const std::vector<float>& input,int frames,juce::AudioBuffer<float>& output){int available=-1;for(auto p:processors){add(p,input.data(),frames);int n=pending(p);if(n<0||n>44100)throw std::runtime_error("Rapidly demo output queue invalid");if(available>=0&&n!=available)throw std::runtime_error("Rapidly demo stem queues differ");available=n;}if(!available)return 0;output.setSize(8,available);std::vector<float> interleaved(available*2);for(int stem=0;stem<4;++stem){if(!get(processors[stem],interleaved.data(),available))throw std::runtime_error("Rapidly demo output unavailable");for(int n=0;n<available;++n)for(int ch=0;ch<2;++ch){float value=interleaved[n*2+ch];if(!std::isfinite(value))throw std::runtime_error("Rapidly demo non-finite output");output.setSample(stem*2+ch,n,value);}}return available;}
};
