#pragma once
int runType335Checks(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryStore::diagnosticSharing=false;LibraryStore::update("settings.json",[](juce::DynamicObject& o){o.setProperty("browserAutoLoad",false);});
 auto root=LibraryStore::root().getChildFile("Samples");for(int i=0;i<10;++i)root.getChildFile("A"+juce::String(i)).createDirectory();LibraryStore::addLibraryRoot(root);
 auto audio=std::make_shared<AudioData>();audio->rate=12000;audio->buffer.setSize(1,24000);audio->buffer.clear();audio->buildWaveformPeaks();audio->path=root.getChildFile("A0/sample_0.wav").getFullPathName();if(!writeRenderedAudio(juce::File(audio->path),audio->buffer,audio->rate))return 3370;
 Processor p(false);p.trackOffline=true;p.autoSave=false;p.audio=audio;p.split(4,false);Editor editor(p);editor.diagnosticQuietAnalysis();editor.type335Seed(root,50000);
 auto now=[](){return juce::Time::getMillisecondCounterHiRes();};auto wait=[&](auto predicate){auto deadline=now()+15000;while(!predicate()&&now()<deadline)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return predicate();};
 auto settle=now()+600;while(now()<settle)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);if(editor.browserRowsForCheck()!=0)return 3371;auto rebuilds=editor.save334Rebuilds();
 struct Heartbeat:juce::Timer{double last=0,maximum=0;void timerCallback()override{auto current=juce::Time::getMillisecondCounterHiRes();if(last)maximum=juce::jmax(maximum,current-last);last=current;}} heartbeat;heartbeat.startTimer(5);heartbeat.last=now();
 for(int type:{4,3}){
  auto start=now();editor.type335Change(root,type);double click=now()-start;
  if(!wait([&]{return editor.type335Ready(type==4?50000:0)&&p.playbackMode()==(type==4?1:0);})){heartbeat.stopTimer();return 3372;}
  std::cout<<"Directory type "<<type<<": click "<<click<<" ms; completed view/mode "<<now()-start<<" ms; rows "<<editor.browserRowsForCheck()<<"\n";if(click>30)return 3373;
 }
 auto end=now()+200;while(now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);heartbeat.stopTimer();std::cout<<"Maximum UI heartbeat gap across persistence, type calculation, sorting, folder pruning and row publication: "<<heartbeat.maximum<<" ms; full catalogue rebuilds "<<editor.save334Rebuilds()-rebuilds<<"\n";
 if(heartbeat.maximum>60||editor.save334Rebuilds()!=rebuilds)return 3374;
 std::cout<<"50,000-entry directory type update with active Stretch filter, type sort, loaded sample mode change and UI responsiveness passed\n";return 0;
}
