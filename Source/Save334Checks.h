#pragma once
#include <unistd.h>
int runSave334Checks(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryStore::diagnosticSharing=false;auto root=LibraryStore::root();root.createDirectory();
 LibraryStore::update("settings.json",[](juce::DynamicObject& o){o.setProperty("browserAutoLoad",false);});
 auto slow=root.getChildFile("slow-cache-test.json");slow.replaceWithText("{\"value\":1}");LibraryStore::diagnosticParseDelayMs=350;
 std::atomic<bool> started{false};std::thread parsing([&]{started=true;LibraryStore::readLocal("slow-cache-test.json");});while(!started)std::this_thread::yield();juce::Thread::sleep(40);
 auto now=[](){return juce::Time::getMillisecondCounterHiRes();};auto start=now();LibraryStore::read("settings.json");double contention=now()-start;parsing.join();LibraryStore::diagnosticParseDelayMs=0;if(contention>40)return 3350;
 auto samples=root.getChildFile("Samples");samples.createDirectory();auto audio=std::make_shared<AudioData>();audio->rate=12000;audio->buffer.setSize(1,48000);audio->buffer.clear();audio->path=samples.getChildFile("test_0.wav").getFullPathName();audio->contentHash="save334";audio->buildWaveformPeaks();if(!writeRenderedAudio(juce::File(audio->path),audio->buffer,audio->rate))return 3351;
 auto tags=juce::var(new juce::DynamicObject());for(int i=0;i<5000;++i){auto file=samples.getChildFile("test_"+juce::String(i)+".wav");if(i&&!file.existsAsFile())if(::link(audio->path.toRawUTF8(),file.getFullPathName().toRawUTF8()))return 3352;tags.getDynamicObject()->setProperty(file.getFullPathName(),"drums, house, kick");}LibraryStore::update("tags.json",[&](juce::DynamicObject& o){for(auto& item:tags.getDynamicObject()->getProperties())o.setProperty(item.name,item.value);});LibraryStore::metadataChanged("tags.json");LibraryStore::addLibraryRoot(samples);
 Processor p(false);p.trackOffline=true;p.autoSave=false;p.audio=audio;p.split(64,false);auto xml=p.configurationXml()->toString();LibraryStore::update("loops.json",[&](juce::DynamicObject& o){for(int i=0;i<600;++i){o.setProperty("/legacy/"+juce::String(i)+".wav",xml);o.setProperty("sha256:legacy-"+juce::String(i),xml);}});p.split(256,false);
 Editor editor(p);editor.diagnosticQuietAnalysis();editor.navigateForCheck(samples);editor.diagnosticQuietAnalysis();
 auto wait=[&](auto predicate){double end=now()+10000;while(!predicate()&&now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return predicate();};if(!wait([&]{return editor.indexReadyForCheck()&&editor.browserRowsForCheck()==5000;}))return 3353;double settle=now()+600;while(now()<settle)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);
 auto rebuilds=editor.save334Rebuilds();auto completed=p.manualSaveCompleted.load();
 struct Heartbeat:juce::Timer{double last=0,maximum=0;void timerCallback()override{double current=juce::Time::getMillisecondCounterHiRes();if(last)maximum=juce::jmax(maximum,current-last);last=current;}} heartbeat;heartbeat.startTimer(5);heartbeat.last=now();
 // Hold the processor lock on a worker: Save must defer, never block the host UI.
 std::atomic<bool> locked{false};std::thread holder([&]{const juce::ScopedLock guard(p.lock);locked=true;juce::Thread::sleep(300);});while(!locked)std::this_thread::yield();start=now();editor.save334Click(false);double click=now()-start;
 if(click>30){holder.join();return 3354;}if(!wait([&]{return p.manualSaveCompleted>completed;})){holder.join();return 3355;}holder.join();if(!p.manualSaveOK)return 3356;
 if(!wait([&]{return editor.save334IconReady(juce::File(audio->path));}))return 3357;
 // Busy detection must not be synchronously restarted by clicking Save.
 completed=p.manualSaveCompleted;auto generation=p.detectionGeneration.load();start=now();editor.save334Click(true);double detectionClick=now()-start;if(detectionClick>30||p.detectionGeneration!=generation)return 3358;
 double end=now()+100;while(now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);if(p.manualSaveCompleted!=completed)return 3359;editor.save334ReleaseDetection();if(!wait([&]{return p.manualSaveCompleted>completed;}))return 3360;
 if(!wait([&]{return editor.save334IconReady(juce::File(audio->path));}))return 3361;
 end=now()+300;while(now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);heartbeat.stopTimer();
 std::cout<<"DEBUG click "<<click<<" detection "<<detectionClick<<" heartbeat "<<heartbeat.maximum<<" rebuilds "<<editor.save334Rebuilds()-rebuilds<<"\n";if(editor.save334Rebuilds()!=rebuilds)return 3362;if(heartbeat.maximum>100)return 3363;
 auto saved=LibraryStore::savedLoop(juce::File(audio->path),audio->contentHash);if(!saved)return 3364;int sliceCount=0;for(auto* child:saved->getChildIterator())if(child->hasTagName("slice"))++sliceCount;if(sliceCount!=256)return 3364;
 std::cout<<"Unrelated settings read during 350 ms background parse: "<<contention<<" ms\nSave click while processor lock held: "<<click<<" ms; busy-detection click: "<<detectionClick<<" ms\nMaximum UI heartbeat gap: "<<heartbeat.maximum<<" ms; catalogue rebuilds after saving: "<<editor.save334Rebuilds()-rebuilds<<"\nManual saves, pending-detection handling, saved icon, configuration recall and UI responsiveness passed\n";return 0;
}
