#pragma once

// Continuous, bounded look-ahead. All Rubber Band calls and buffer ownership
// changes happen on this worker; the audio callback only reads immutable pages.
class TrackStreaming final : public juce::Thread {
public:
 struct Configuration {
  std::shared_ptr<const AudioData> audio;
  TrackGrid grid;
  double tempo=120,pitch=0;
  bool stems=false,loop=false;
  double begin=0,end=0,repeat=0;
  bool same(const Configuration& b)const {
   if(audio!=b.audio||tempo!=b.tempo||pitch!=b.pitch||stems!=b.stems||loop!=b.loop||begin!=b.begin||end!=b.end||repeat!=b.repeat||grid.bpm!=b.grid.bpm||grid.anchors.size()!=b.grid.anchors.size())return false;
   for(size_t i=0;i<grid.anchors.size();++i)if(grid.anchors[i].sample!=b.grid.anchors[i].sample||grid.anchors[i].beat!=b.grid.anchors[i].beat)return false;
   return true;
  }
 };
 struct Page {double beat=0,step=0;bool stems=false;juce::AudioBuffer<float> pcm;};
 struct Window:std::enable_shared_from_this<Window>{
  std::shared_ptr<const Configuration> configuration;
  std::vector<std::shared_ptr<const Page>> pages;
  const Page* at(double beat)const {
   for(auto i=pages.rbegin();i!=pages.rend();++i){auto& p=**i;double n=(beat-p.beat)/p.step;if(n>=-1e-5&&n<p.pcm.getNumSamples()-1)return &p;}return nullptr;
  }
  bool read(double beat,int channel,std::array<float,5>& values,float& dry,bool& separated)const {
   auto* page=at(beat);if(!page)return false;
   double point=juce::jmax(0.,(beat-page->beat)/page->step);int n=(int)point;float t=(float)(point-n);
   auto sample=[&](int ch){auto* p=page->pcm.getReadPointer(ch);return p[n]+t*(p[n+1]-p[n]);};
   dry=sample(channel);separated=page->stems;
   for(int s=0;s<4;++s)values[s]=sample(2+s*2+channel);
   return true;
  }
 };
 std::atomic<double> wantedBeat{0};
 std::atomic<uint64_t> navigation{0},misses{0};
 std::atomic<bool> failed{false};
private:
 TrackStemWorker& separation;
 std::shared_ptr<const Configuration> wanted;
 std::shared_ptr<const Window> published;
 std::atomic<const Window*> audioPublished{nullptr},audioHazard{nullptr};
 std::vector<std::shared_ptr<const Window>> retired;
 juce::WaitableEvent wake;
 void publish(std::shared_ptr<const Window> next){if(published)retired.push_back(published);published=std::move(next);audioPublished.store(published.get());retired.erase(std::remove_if(retired.begin(),retired.end(),[&](auto& w){return w.use_count()==1&&w.get()!=audioHazard.load();}),retired.end());}
public:
 explicit TrackStreaming(TrackStemWorker& worker):Thread("Track continuous stretch"),separation(worker){}
 ~TrackStreaming()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 void request(Configuration next,double beat){auto old=std::atomic_load(&wanted);if(old&&old->same(next))return;wantedBeat=beat;failed=false;std::atomic_store(&wanted,std::make_shared<const Configuration>(std::move(next)));if(!isThreadRunning())startThread(juce::Thread::Priority::high);wake.signal();}
 void clear(){std::atomic_store(&wanted,std::shared_ptr<const Configuration>{});wake.signal();}
 void seek(double beat){wantedBeat=beat;++navigation;wake.signal();}
 std::shared_ptr<const Window> audioResult(){const Window* next;do{next=audioPublished.load();audioHazard.store(next);}while(next!=audioPublished.load());auto result=next?next->shared_from_this():std::shared_ptr<const Window>{};audioHazard.store(nullptr);return result;}
 void run()override {
  using RB=RubberBand::RubberBandStretcher;
  std::shared_ptr<const Configuration> active;
  std::unique_ptr<RB> engine;
  std::vector<std::shared_ptr<const Page>> pages;
  juce::AudioBuffer<float> input(10,512),output(10,4096);
  double source=0,outputBeat=0,generationStart=0,lastWanted=0;
  size_t discard=0;uint64_t serial=0;bool withStems=false,final=false,finished=false,preparingWrap=false;double preparedWrap=0;
  auto sourceBeat=[](const Configuration& c,double beat){if(c.repeat>0){double first=c.grid.beatAt(0,c.audio->rate);double phase=std::fmod(beat-first,c.repeat);return first+(phase<0?phase+c.repeat:phase);}return beat;};
  while(!threadShouldExit()) {
   auto next=std::atomic_load(&wanted);
   if(!next){active.reset();engine.reset();pages.clear();publish({});wake.wait(20);continue;}
   try {
    double beat=wantedBeat.load();auto nav=navigation.load();auto live=separation.result();
    bool native=live&&live->source->buffer.get()==&next->audio->buffer;
    double frame=next->grid.sampleAt(sourceBeat(*next,beat),next->audio->rate)/next->audio->rate*44100;
    bool stemsReady=next->stems&&native&&live->covers((int64_t)juce::jmax(0.,frame),(int64_t)juce::jmax(0.,frame)+8192);
    bool covered=false;for(auto& p:pages)if(beat>=p->beat&&beat<p->beat+(p->pcm.getNumSamples()-1)*p->step)covered=true;
    bool reset=active!=next||nav!=serial||(!covered&&(beat<generationStart-.001||beat>outputBeat+.02))||(!withStems&&stemsReady)||(!covered&&std::abs(beat-lastWanted)>next->tempo/60*.25);
    // Prime the loop start before the endpoint, retaining the tail pages.
    bool primeWrap=false;
    double firstBeat=next->grid.beatAt(0,next->audio->rate);
    double cycleEnd=next->repeat>0?firstBeat+(std::floor((generationStart-firstBeat+1e-6)/next->repeat)+1)*next->repeat:next->end;
    bool repeatInside=next->repeat>0&&(!next->loop||cycleEnd<next->end-1e-6);
    double wrapAt=repeatInside?cycleEnd:next->end;
    if(finished&&(repeatInside||(next->loop&&next->end>next->begin))&&!preparingWrap&&(wrapAt-beat)*60/next->tempo<.35&&beat>generationStart+.01){primeWrap=true;preparingWrap=true;preparedWrap=repeatInside?cycleEnd:next->begin;}
    if(preparingWrap&&std::abs(beat-preparedWrap)<next->tempo/60*.1)preparingWrap=false;
    if(reset||primeWrap){
     if(active!=next)pages.clear();
     if(reset)preparingWrap=false;double startBeat=primeWrap?preparedWrap:beat;
     active=next;serial=nav;withStems=stemsReady;generationStart=outputBeat=startBeat;source=next->grid.sampleAt(sourceBeat(*next,startBeat),next->audio->rate);final=finished=false;
     double local=(next->grid.beatAt(source+512,next->audio->rate)-next->grid.beatAt(source,next->audio->rate))*next->audio->rate*60/next->tempo/512;
     engine=std::make_unique<RB>((size_t)next->audio->rate,10,RB::OptionProcessRealTime|RB::OptionEngineFaster|RB::OptionThreadingNever|RB::OptionWindowShort|RB::OptionPitchHighConsistency,juce::jlimit(.125,8.,local),std::pow(2.,next->pitch/12));
     engine->setMaxProcessSize(512);discard=engine->getStartDelay();input.clear();size_t pad=engine->getPreferredStartPad();while(pad){size_t count=std::min(pad,(size_t)512);engine->process(input.getArrayOfReadPointers(),count,false);pad-=count;}
    }
    lastWanted=beat;
    // A quarter second reserve keeps streaming CPU bounded and leaves the GPU
    // worker free to finish the full original-track separation.
    double ahead=(outputBeat-(preparingWrap?preparedWrap:beat))*60/next->tempo;
    if(finished||ahead>.3){wake.wait(3);continue;}
    if(engine->available()>0){
     int count=juce::jmin(4096,engine->available());engine->retrieve(output.getArrayOfWritePointers(),count);
     int skip=(int)std::min(discard,(size_t)count);discard-=skip;int usable=count-skip;
     if(usable){
      auto page=std::make_shared<Page>();page->beat=outputBeat;page->step=next->tempo/60/next->audio->rate;page->stems=withStems;page->pcm.setSize(10,usable+1);
      for(int ch=0;ch<10;++ch){page->pcm.copyFrom(ch,0,output,ch,skip,usable);page->pcm.setSample(ch,usable,output.getSample(ch,count-1));}
      // Link the last interpolation sample to the next page, without mutating a
      // page that an audio callback may currently be reading.
      if(!pages.empty()){auto prior=pages.back();if(std::abs(prior->beat+prior->step*(prior->pcm.getNumSamples()-1)-page->beat)<page->step*.1){auto joined=std::make_shared<Page>(*prior);for(int ch=0;ch<10;++ch)joined->pcm.setSample(ch,joined->pcm.getNumSamples()-1,page->pcm.getSample(ch,0));pages.back()=joined;}}
      pages.push_back(page);outputBeat+=usable*page->step;
      pages.erase(std::remove_if(pages.begin(),pages.end(),[&](auto& p){return (p->beat+(p->pcm.getNumSamples()-1)*p->step<beat-next->tempo/60*.08&&!(preparingWrap&&p->beat<preparedWrap+next->tempo/60*.5))||(p->beat>beat+next->tempo/60*2&&!preparingWrap);}),pages.end());
      auto window=std::make_shared<Window>();window->configuration=next;window->pages=pages;publish(window);
     }
     continue;
    }
    if(final){finished=true;wake.wait(2);continue;}
    input.clear();int count=512;double boundary=next->audio->buffer.getNumSamples();
    if(next->repeat>0){double first=next->grid.beatAt(0,next->audio->rate);boundary=juce::jmin(boundary,next->grid.sampleAt(first+next->repeat,next->audio->rate));}
    if(next->loop&&next->end>next->begin)boundary=juce::jmin(boundary,next->grid.sampleAt(sourceBeat(*next,next->end-1e-8),next->audio->rate)+1);
    count=juce::jmin(count,(int)std::ceil(boundary-source));
    if(count<=0){engine->process(input.getArrayOfReadPointers(),0,true);final=true;continue;}
    if(withStems){double from=source/next->audio->rate*44100,until=juce::jmin(std::ceil(next->audio->buffer.getNumSamples()/next->audio->rate*44100.),(source+count)/next->audio->rate*44100+2);
     if(!native||!live->covers((int64_t)juce::jmax(0.,from),(int64_t)std::ceil(until))){separation.wantedFrame=juce::jmax(0.,from);wake.wait(2);continue;}}
    for(int n=0;n<count;++n){double point=source+n;int i=(int)std::floor(point);if(i<0||i>=next->audio->buffer.getNumSamples())continue;int j=juce::jmin(i+1,next->audio->buffer.getNumSamples()-1);float fraction=(float)(point-i);
     for(int ch=0;ch<2;++ch){auto* raw=next->audio->buffer.getReadPointer(juce::jmin(ch,next->audio->buffer.getNumChannels()-1));input.setSample(ch,n,raw[i]+fraction*(raw[j]-raw[i]));if(withStems){std::array<float,4> values{};live->read(point/next->audio->rate*44100,ch,values);for(int stem=0;stem<4;++stem)input.setSample(2+stem*2+ch,n,values[stem]);}}
    }
    double ratio=(next->grid.beatAt(source+count,next->audio->rate)-next->grid.beatAt(source,next->audio->rate))*next->audio->rate*60/next->tempo/count;
    engine->setTimeRatio(juce::jlimit(.125,8.,ratio));engine->process(input.getArrayOfReadPointers(),(size_t)count,false);source+=count;
   }catch(...){failed=true;engine.reset();active.reset();pages.clear();publish({});wake.wait(50);}
  }
  publish({});
 }
};
