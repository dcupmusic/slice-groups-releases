#include "MidiExtraction.h"
#include <juce_dsp/juce_dsp.h>
#include <onnxruntime_cxx_api.h>
#include <samplerate.h>
#include <algorithm>
#include <mutex>
#include <dlfcn.h>
#include <cmath>

juce::File MidiExtraction::modelFile(){
 auto executable=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
 for(auto base:{juce::File::getSpecialLocation(juce::File::currentApplicationFile),executable.getParentDirectory().getParentDirectory()}){
  for(auto file:{base.getChildFile("Contents/Resources/BasicPitch.onnx"),base.getChildFile("Resources/BasicPitch.onnx")})if(file.existsAsFile())return file;
 }
 Dl_info location{};if(dladdr((const void*)&MidiExtraction::modelFile,&location)&&location.dli_fname){auto file=juce::File(juce::String::fromUTF8(location.dli_fname)).getParentDirectory().getParentDirectory().getChildFile("Resources/BasicPitch.onnx");if(file.existsAsFile())return file;}
 return juce::File::getCurrentWorkingDirectory().getChildFile("outputs/SliceGroups/Resources/BasicPitch.onnx");
}
namespace {
// Native adaptation of Spotify Basic Pitch's onset/frame decoding (Apache-2.0).
// See Resources/BasicPitch-LICENSE and BasicPitch-NOTICE.
std::vector<SequenceNote> decode(std::vector<float> frames,std::vector<float> onsets,int count,double bpm,double seconds,MidiExtractionSettings settings){
 constexpr int bins=88;float onsetThreshold=settings.sensitivity==0?.65f:settings.sensitivity==2?.35f:.5f,frameThreshold=settings.sensitivity==0?.4f:settings.sensitivity==2?.2f:.3f;int minimumFrames=juce::jmax(1,(int)std::round(settings.minimumMs*.001*22050/256));for(int t=0;t<count;++t)for(int p=0;p<bins;++p)if(p+21<settings.low||p+21>settings.high){frames[t*bins+p]=0;onsets[t*bins+p]=0;}std::vector<SequenceNote> notes;auto original=frames;std::vector<float> differences(frames.size());float maximum=0,onsetMaximum=0;
 for(int t=2;t<count;++t)for(int p=0;p<bins;++p){float delta=1;for(int d=1;d<=2;++d)delta=juce::jmin(delta,frames[t*bins+p]-frames[(t-d)*bins+p]);delta=juce::jmax(0.f,delta);differences[t*bins+p]=delta;maximum=juce::jmax(maximum,delta);}
 for(auto value:onsets)onsetMaximum=juce::jmax(onsetMaximum,value);
 if(maximum>0)for(size_t i=0;i<onsets.size();++i)onsets[i]=juce::jmax(onsets[i],differences[i]*onsetMaximum/maximum);
 auto consume=[&](int a,int b,int p){for(int t=a;t<b;++t)for(int q=juce::jmax(0,p-1);q<=juce::jmin(87,p+1);++q)frames[t*bins+q]=0;};
 auto time=[](int frame){return frame*256./22050-std::floor(frame/172.)*((256./22050)*(172-43844./256)+.0018);};
 auto add=[&](int a,int b,int p){if(b-a<=minimumFrames||notes.size()>=8192)return;double begin=juce::jlimit(0.,seconds,time(a)),end=juce::jlimit(begin,seconds,time(b));if(end-begin<.04)return;float amplitude=0;for(int t=a;t<b;++t)amplitude+=original[t*bins+p];amplitude/=juce::jmax(1,b-a);notes.push_back({(int)notes.size()+1,p+21,juce::jlimit(1,127,(int)std::round(amplitude*127)),1,begin*bpm/60,(end-begin)*bpm/60});};
 for(int t=count-2;t>=1;--t)for(int p=bins-1;p>=0;--p)if(onsets[t*bins+p]>=onsetThreshold&&onsets[t*bins+p]>onsets[(t-1)*bins+p]&&onsets[t*bins+p]>onsets[(t+1)*bins+p]){
  int i=t+1,quiet=0;while(i<count-1&&quiet<11){quiet=frames[i*bins+p]<frameThreshold?quiet+1:0;++i;}int end=i-quiet;if(end-t>minimumFrames){consume(t,end,p);add(t,end,p);}
 }
 // Recover sustained notes without a clear new onset, suppressing adjacent-bin duplicates.
 for(int iteration=0;iteration<8192-(int)notes.size();++iteration){auto peak=std::max_element(frames.begin(),frames.end());if(peak==frames.end()||*peak<=frameThreshold)break;int index=(int)(peak-frames.begin()),mid=index/bins,p=index%bins;*peak=0;int i=mid+1,quiet=0;
  while(i<count-1&&quiet<11){quiet=frames[i*bins+p]<frameThreshold?quiet+1:0;consume(i,i+1,p);++i;}int end=i-1-quiet;i=mid-1;quiet=0;
  while(i>0&&quiet<11){quiet=frames[i*bins+p]<frameThreshold?quiet+1:0;consume(i,i+1,p);--i;}add(i+1+quiet,end,p);
 }
 std::sort(notes.begin(),notes.end(),[](auto a,auto b){return a.beat==b.beat?a.pitch<b.pitch:a.beat<b.beat;});return notes;
}
juce::Image spectrum(const std::vector<float>& audio,std::function<bool()> cancelled){
 constexpr int size=4096;int columns=juce::jlimit(64,1600,(int)audio.size()/256);juce::Image image(juce::Image::ARGB,columns,128,true);juce::dsp::FFT fft(12);std::vector<float> window(size),buffer(size*2),values(columns*128);juce::dsp::WindowingFunction<float>::fillWindowingTables(window.data(),size,juce::dsp::WindowingFunction<float>::hann,false);float maximum=1e-9f;
 for(int x=0;x<columns;++x){if(cancelled&&cancelled())return {};std::fill(buffer.begin(),buffer.end(),0);int center=(int)((double)x/columns*audio.size());for(int n=0;n<size;++n){int source=center+n-size/2;if(juce::isPositiveAndBelow(source,(int)audio.size()))buffer[n]=audio[source]*window[n];}fft.performFrequencyOnlyForwardTransform(buffer.data());for(int pitch=0;pitch<128;++pitch){double bin=440*std::pow(2.,(pitch-69)/12.)*size/22050;if(bin>=size/2)continue;int a=juce::jlimit(0,size/2-1,(int)std::floor(bin)),b=juce::jmin(size/2-1,a+1);float v=buffer[a]+(bin-a)*(buffer[b]-buffer[a]);values[x*128+pitch]=v;maximum=juce::jmax(maximum,v);}}
 for(int x=0;x<columns;++x)for(int pitch=0;pitch<128;++pitch){float db=20*std::log10(juce::jmax(1e-9f,values[x*128+pitch]/maximum)),v=juce::jlimit(0.f,1.f,(db+65)/65);image.setPixelAt(x,127-pitch,juce::Colour::fromHSV(.58f-v*.48f,.7f,.25f+v*.75f,v));}return image;
}
}
MidiExtractionResult MidiExtraction::run(const std::vector<float>& mono,double rate,double bpm,std::function<bool()> cancelled,std::function<void(double)> progress,MidiExtractionSettings settings){
 MidiExtractionResult result;try{
  if(mono.empty()||rate<8000||!std::isfinite(rate)||!std::isfinite(bpm)||bpm<20||bpm>400||mono.size()/rate>120){result.error="Choose a loop of up to two minutes.";return result;}
  if(cancelled&&cancelled())return result;int count=(int)std::ceil(mono.size()*22050/rate);std::vector<float> audio(count+64);SRC_DATA convert{};convert.data_in=mono.data();convert.input_frames=(long)mono.size();convert.data_out=audio.data();convert.output_frames=(long)audio.size();convert.src_ratio=22050/rate;convert.end_of_input=1;if(src_simple(&convert,SRC_SINC_FASTEST,1))throw std::runtime_error("Could not resample audio.");audio.resize(convert.output_frames_gen);
  auto model=modelFile();if(!model.existsAsFile())throw std::runtime_error("The MIDI extraction model is missing. Reinstall Slice Groups.");
  // Shared across windows/instances; never loaded or called on the audio thread.
  static std::mutex modelLock;std::unique_lock<std::mutex> guard(modelLock,std::defer_lock);while(!guard.try_lock()){if(cancelled&&cancelled())return result;juce::Thread::sleep(10);}
  static Ort::Env environment(ORT_LOGGING_LEVEL_ERROR,"Slice Groups MIDI");static std::unique_ptr<Ort::Session> session;static juce::String loadedPath;
  if(!session||loadedPath!=model.getFullPathName()){Ort::SessionOptions options;options.SetIntraOpNumThreads(1);options.SetInterOpNumThreads(1);options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);session=std::make_unique<Ort::Session>(environment,model.getFullPathName().toRawUTF8(),options);loadedPath=model.getFullPathName();}
  constexpr int window=43844,overlap=7680,hop=window-overlap,trim=15;int padded=(int)audio.size()+overlap/2;std::vector<float> frames,onsets,input(window);auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);std::array<int64_t,3> shape{1,window,1};const char* inputNames[]{"serving_default_input_2:0"};const char* outputNames[]{"StatefulPartitionedCall:1","StatefulPartitionedCall:2"};
  for(int offset=0;offset<padded;offset+=hop){if(cancelled&&cancelled())return result;std::fill(input.begin(),input.end(),0);for(int n=0;n<window;++n){int at=offset+n-overlap/2;if(juce::isPositiveAndBelow(at,(int)audio.size()))input[n]=audio[at];}auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());auto output=session->Run(Ort::RunOptions{},inputNames,&tensor,1,outputNames,2);auto dimensions=output[0].GetTensorTypeAndShapeInfo().GetShape();if(dimensions.size()!=3||dimensions[2]!=88||dimensions[1]<=trim*2)throw std::runtime_error("Unexpected MIDI model output.");int nFrames=(int)dimensions[1];for(int i=0;i<2;++i){auto* values=output[i].GetTensorData<float>();auto& target=i?onsets:frames;target.insert(target.end(),values+trim*88,values+(nFrames-trim)*88);}if(progress)progress(.05+.7*juce::jmin(1.,(offset+hop)/(double)padded));}
  guard.unlock();int nFrames=juce::jmin((int)frames.size()/88,(int)std::floor(audio.size()*86./22050));frames.resize(nFrames*88);onsets.resize(nFrames*88);if(nFrames<2)throw std::runtime_error("The loop is too short to extract notes.");if(cancelled&&cancelled())return result;
  result.beats=mono.size()/rate*bpm/60;result.clip.notes=decode(std::move(frames),std::move(onsets),nFrames,bpm,mono.size()/rate,settings);result.clip.nextId=(int)result.clip.notes.size()+1;result.clip.bars=juce::jlimit(1,512,(int)std::ceil((result.beats-1e-6)/4));result.clip.key=SequenceClip::inferKey(result.clip.notes,result.clip.minor);result.clip.changed();if(progress)progress(.85);result.spectrogram=spectrum(audio,cancelled);if(progress)progress(1);
 }catch(const std::exception& e){result.error=e.what();}return result;
}
