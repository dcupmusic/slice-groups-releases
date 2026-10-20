#pragma once
int runStretch360Checks(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryPreparation::checksPaused=false;auto root=LibraryStore::root().getChildFile("Samples"),chosen=root.getChildFile("Chosen Stretch"),other=root.getChildFile("Other Library"),child=chosen.getChildFile("Slice override");child.createDirectory();other.createDirectory();
 juce::AudioBuffer<float> sound(2,48000*8);for(int n=0;n<sound.getNumSamples();++n){float value=.2f*std::sin(n*.052)+((n%12000)<300?.1f*std::sin(n*.35):0);sound.setSample(0,n,value);sound.setSample(1,n,value*.8f);}
 auto stretch=chosen.getChildFile("loop_120_C.wav"),slice=child.getChildFile("slice_120.wav"),outside=other.getChildFile("outside_120.wav"),cold=root.getChildFile("cold_120_C.wav"),cancelledFile=chosen.getChildFile("cancelled_120.wav");
 for(auto file:{stretch,slice,outside,cold})if(!writeRenderedAudio(file,sound,48000))return 3601;
 LibraryStore::setPlaybackType({stretch},3);LibraryStore::setPlaybackType({child},5);LibraryStore::setPlaybackType({outside},1);
 LibraryStore::setPlaybackType({chosen},4);
 if(LibraryStore::playbackType(stretch)!=4||LibraryStore::playbackType(slice)!=4||LibraryStore::playbackType(outside)!=1)return 3617;
 std::cout<<"Folder type replaces existing descendant file/folder overrides; sibling types retained\n";
 LibraryStore::addLibraryRoot(root);LibraryStore::setPlaybackType({chosen,cold},4);LibraryStore::setPlaybackType({child},3);
 auto now=[](){return juce::Time::getMillisecondCounterHiRes();};auto pump=[&](double ms){auto end=now()+ms;while(now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);};auto wait=[&](auto condition){auto end=now()+30000;while(!condition()&&now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return condition();};
 LibraryPreparation::request({stretch,slice,outside});LibraryPreparation::requestScope(root);pump(120);if(AnalysisStore::exists(stretch)||AnalysisStore::exists(slice)||AnalysisStore::exists(outside))return 3602;
 {LibraryPreparation::Foreground foreground;LibraryPreparation::requestStretch(chosen);pump(120);if(AnalysisStore::exists(stretch)||!LibraryPreparation::busy())return 3603;LibraryPreparation::markPlayback(true);}
pump(150);if(AnalysisStore::exists(stretch))return 3604;
 if(!wait([&]{return AnalysisStore::exists(stretch)&&LoopLoudness::exists(stretch)&&!LibraryPreparation::busy();}))return 3605;
 if(AnalysisStore::exists(slice)||AnalysisStore::exists(outside))return 3606;
 writeRenderedAudio(cancelledFile,sound,48000);LibraryPreparation::markPlayback(true);LibraryPreparation::requestStretch(cancelledFile);LibraryPreparation::cancel();pump(500);if(AnalysisStore::exists(cancelledFile)||LibraryPreparation::busy())return 3607;
 LibraryPreparation::requestStretch(cancelledFile);if(!wait([&]{return AnalysisStore::exists(cancelledFile)&&!LibraryPreparation::busy();}))return 3608;
 std::cout<<"Explicit Stretch-only scope, child Slice overrides, no whole-library/browse preparation, foreground/playback pause, cancellation and explicit restart passed\n";
 Processor p(false);p.trackOffline=true;p.autoSave=false;p.hasHost=true;p.hostBpm=132;p.pitchSemitones=2;LoopLoader loader(p);bool complete=false;loader.completed=[&](bool ok,juce::File,uint64_t){complete=ok;loader.busy=false;};loader.startThread();
 auto ready=[&]{auto result=p.stretchWorker.result();return complete&&result&&result->request==p.stretchRequest();};auto start=now();loader.request(cold);if(!wait(ready))return 3609;double coldMs=now()-start;if(!wait([&]{return !p.stretchWorker.busy;}))return 3616;
 loader.prepareUpcoming({stretch,slice},"132-pitch2");std::shared_ptr<const AudioData> data;if(!wait([&]{data=DecodedSampleCache::instance().get(stretch);return data!=nullptr;}))return 3610;
 Processor expected(false);expected.audio=p.audio;auto config=p.configurationXml(true);expected.applyConfiguration(*config,true);expected.autoSave=expected.autoCategories=false;expected.hasHost=true;expected.hostBpm=132;if(!expected.installAudio(data,true,0,false))return 3611;auto request=expected.stretchRequest();if(!wait([&]{return p.stretchWorker.cached(request)!=nullptr;}))return 3612;
 if(AnalysisStore::exists(slice))return 3613;complete=false;start=now();loader.request(stretch);if(!wait(ready))return 3614;double warmMs=now()-start;
 std::cout<<"DEBUG cold "<<coldMs<<" warm "<<warmMs<<" loader "<<loader.lastLoadMs.load()<<" identity "<<(p.audio==data)<<" cached "<<(p.stretchWorker.cached(p.stretchRequest())!=nullptr)<<"\n";if(p.audio!=data||warmMs>100)return 3615;
 std::cout<<"8-second stereo Stretch, 132 BPM / +2 semitones: cold request-to-ready "<<coldMs<<" ms; prepared request-to-ready "<<warmMs<<" ms (includes callback and first available render); decoded/render identity reused\n";
 return 0;
}
