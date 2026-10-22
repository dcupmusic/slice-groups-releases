#pragma once
// Import-time monophonic fundamental detection. Never runs in the audio callback.
struct SamplePitch {
 int note=-1;double cents=0,confidence=0;juce::String method;
 static int filename(juce::String path){auto text=juce::File(path).getFileNameWithoutExtension().toStdString();std::regex pattern(R"((?:^|[\s_\-\[\(])([A-Ga-g])([#b]?)(-?[0-8])(?:m|min|maj)?(?=$|[\s_\-\]\)]))");int result=-1;for(auto i=std::sregex_iterator(text.begin(),text.end(),pattern);i!=std::sregex_iterator();++i){auto m=*i;int pitch=juce::String("C D EF G A B").indexOfChar((juce::juce_wchar)std::toupper(m[1].str()[0]));if(pitch<0)continue;if(m[2].str()=="#")++pitch;if(m[2].str()=="b")--pitch;int n=12*(std::stoi(m[3].str())+2)+pitch;if(n>=0&&n<128)result=n;}return result;}
 static juce::String name(int note){if(note<0)return "—";static const char* names[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};return juce::String(names[note%12])+juce::String(note/12-2);}
 static int parse(juce::String text){return filename("/"+text.trim()+".wav");}
 static SamplePitch analyse(const juce::AudioBuffer<float>& input,double rate,std::function<bool()> cancel={}){
  SamplePitch out;out.method="Audio";if(rate<=0||input.getNumSamples()<128||input.getNumChannels()<1)return out;
  int channel=0;double maxEnergy=0;for(int ch=0;ch<input.getNumChannels();++ch){double energy=0;int step=juce::jmax(1,input.getNumSamples()/16000);for(int i=0;i<input.getNumSamples();i+=step)energy+=std::pow(input.getSample(ch,i),2);if(energy>maxEnergy){maxEnergy=energy;channel=ch;}}if(maxEnergy<1e-9)return out;
  constexpr double sr=12000;int total=(int)juce::jmin(240000.,input.getNumSamples()*sr/rate);if(total<300)return out;
  // Inspect at most twenty seconds, using bounded windows and a cancellation point per frame.
  int lagMax=juce::jmin(600,total/3),n=juce::jmin(2048,total-lagMax),width=n+lagMax;if(n<200)return out;
  std::vector<double> notes,quality;double peak=input.getMagnitude(channel,0,input.getNumSamples());int frames=8;
  for(int frame=0;frame<frames;++frame){if(cancel&&cancel())return {};int start=(int)((total-width)*(.06+.86*frame/(frames-1)));std::vector<double> x(width),d(lagMax+1),cm(lagMax+1);double mean=0,energy=0;auto* values=input.getReadPointer(channel);for(int i=0;i<width;++i){double pos=(start+i)*rate/sr;int a=juce::jlimit(0,input.getNumSamples()-1,(int)pos);x[i]=values[a]+(pos-a)*(values[juce::jmin(a+1,input.getNumSamples()-1)]-values[a]);mean+=x[i];}mean/=width;for(auto& v:x){v-=mean;energy+=v*v;}if(std::sqrt(energy/width)<juce::jmax(1e-5,peak*.015))continue;
   double sum=0;cm[0]=1;for(int tau=1;tau<=lagMax;++tau){double value=0;for(int i=0;i<n;++i){double delta=x[i]-x[i+tau];value+=delta*delta;}d[tau]=value;sum+=value;cm[tau]=sum>1e-20?value*tau/sum:1;}
   int tau=0;for(int i=5;i<lagMax-1;++i)if(cm[i]<.16){while(i+1<lagMax&&cm[i+1]<cm[i])++i;tau=i;break;}if(!tau)continue;
   if(tau*2+1<lagMax&&cm[tau*2]+.06<cm[tau])tau*=2;
   double position=tau;if(tau>1&&tau<lagMax){double a=cm[tau-1],b=cm[tau],c=cm[tau+1],den=a-2*b+c;if(std::abs(den)>1e-12)position+=juce::jlimit(-.5,.5,.5*(a-c)/den);}
   double note=69+12*std::log2((sr/position)/440.);if(note<0||note>127||1-cm[tau]<.84)continue;notes.push_back(note);quality.push_back(1-cm[tau]);
  }
  if(notes.size()<2)return out;auto sorted=notes;std::sort(sorted.begin(),sorted.end());double median=sorted[sorted.size()/2],sum=0,q=0;int stable=0;for(size_t i=0;i<notes.size();++i)if(std::abs(notes[i]-median)<.38){sum+=notes[i];q+=quality[i];++stable;}if(stable<2||stable<(int)notes.size()*.7)return out;double midi=sum/stable;out.note=juce::jlimit(0,127,(int)std::round(midi));out.cents=100*(midi-out.note);out.confidence=q/stable*stable/notes.size();return out;
 }
 static SamplePitch cached(const juce::File& file,const juce::AudioBuffer<float>& audio,double rate,bool force=false,std::function<bool()> cancel={}){
  if(!force){int note=filename(file.getFullPathName());if(note>=0)return {note,0,1,"Filename"};}
  auto values=LibraryStore::localSnapshot("sample-pitch-v1.json"),v=values.getProperty(file.getFullPathName(),{});auto stamp=juce::String(file.getSize())+":"+juce::String(file.getLastModificationTime().toMilliseconds());if(!force&&v.getProperty("stamp",{}).toString()==stamp)return {(int)v.getProperty("note",-1),(double)v.getProperty("cents",0),(double)v.getProperty("confidence",0),"Audio"};
  auto result=analyse(audio,rate,cancel);if(cancel&&cancel())return {};auto* object=new juce::DynamicObject();object->setProperty("stamp",stamp);object->setProperty("note",result.note);object->setProperty("cents",result.cents);object->setProperty("confidence",result.confidence);LibraryStore::update("sample-pitch-v1.json",[&](auto& layer){layer.setProperty(file.getFullPathName(),juce::var(object));});return result;
 }
};
