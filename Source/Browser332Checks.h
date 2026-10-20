#pragma once
#include <unistd.h>
int runBrowser332Checks(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryStore::diagnosticSharing=false;
 auto base=LibraryStore::root().getChildFile("Samples"),a=base.getChildFile("A"),b=base.getChildFile("B");a.createDirectory();b.createDirectory();
 auto audio=std::make_shared<AudioData>();audio->rate=12000;audio->buffer.setSize(1,48000);audio->buffer.clear();audio->buildWaveformPeaks();audio->path=a.getChildFile("sample_0.wav").getFullPathName();if(!writeRenderedAudio(juce::File(audio->path),audio->buffer,audio->rate))return 3320;
 for(int i=1;i<5000;++i){auto file=(i<2500?a:b).getChildFile("sample_"+juce::String(i)+".wav");if(!file.existsAsFile()&&::link(audio->path.toRawUTF8(),file.getFullPathName().toRawUTF8())!=0)return 3321;}
 auto taggedA=juce::File(audio->path),taggedB=b.getChildFile("sample_2500.wav");LibraryStore::addTag({taggedA,taggedB},"filter-test");LibraryStore::setFavourite(taggedA,"A");LibraryStore::setFavourite(taggedB,"B");LibraryStore::setFavourite(b.getChildFile("sample_2501.wav"),"Extra");LibraryStore::addLibraryRoot(base);
 Processor p(false);p.trackOffline=true;p.audio=audio;p.split(4,false);p.boundarySnap=1;
 // Shift is down before the boundary is pressed, so it must not select a range.
 for(int snap:{1,2}){p.split(4,false);p.boundarySnap=snap;Waveform wave(p);wave.setSize(1000,200);wave.state.audio=audio;wave.state.slices=p.slices;wave.state.trimEnd=48000;int original=p.slices[1].start;float x=(float)wave.sampleToX(original);
  auto event=[&](float at){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{at,100},juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier,1,0,0,0,0,&wave,&wave,juce::Time::getCurrentTime(),{x,100},juce::Time::getCurrentTime(),1,true);};wave.mouseDown(event(x));wave.mouseDrag(event(x+20));wave.mouseUp(event(x+20));if(std::abs((p.slices[1].start-original)-96)>2)return 3322;
 }
 Editor editor(p);editor.diagnosticQuietAnalysis();if(!editor.snap332ForCheck())return 3323;editor.navigateForCheck(base);editor.diagnosticQuietAnalysis();
 auto now=[](){return juce::Time::getMillisecondCounterHiRes();};auto wait=[&](auto predicate){double end=now()+15000;while(!predicate()&&now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);return predicate();};
 if(!wait([&]{return editor.indexReadyForCheck();}))return 3324;
 editor.browser332Action(1,{});if(!wait([&]{return editor.browserRowsForCheck()==2;}))return 3325;
 editor.browser332Action(3,{});if(!wait([&]{return editor.browserRowsForCheck()==5000;}))return 3326; // Selecting a directory includes all files in its descendants.
 for(auto folder:{a,b}){double start=now();editor.browser332Action(0,folder);double click=now()-start;if(!wait([&]{return editor.browserRowsForCheck()==2500&&juce::File(editor.browserFile332ForCheck(0)).getParentDirectory()==folder;}))return 3327;std::cout<<"Folder click "<<click<<" ms; results "<<now()-start<<" ms\n";if(click>50)return 3328;}
 double start=now();editor.browser332Action(1,{});double tagClick=now()-start;if(!wait([&]{return editor.browserRowsForCheck()==2;}))return 3329;std::cout<<"Tag filter click "<<tagClick<<" ms; results "<<now()-start<<" ms\n";if(tagClick>50)return 3330;
 start=now();editor.browser332Action(2,{});double favouriteClick=now()-start;if(!wait([&]{return editor.browserRowsForCheck()==3;}))return 3331;std::cout<<"Favourite filter click "<<favouriteClick<<" ms\n";if(favouriteClick>50)return 3332;
 start=now();editor.browser332Action(4,a);double typeClick=now()-start;if(!wait([&]{return LibraryStore::playbackType(taggedA)==4;}))return 3333;std::cout<<"Type assignment click "<<typeClick<<" ms; saved "<<now()-start<<" ms\n";if(typeClick>50)return 3334;
 auto generation=p.loadGeneration.load();editor.revealForCheck();if(!wait([&]{return editor.revealedForCheck();})||p.loadGeneration.load()!=generation)return 3339;
 std::cout<<"Show in Library unfolds the root-to-sample branch, selects the loaded file without loading again\n";
 struct Heartbeat:juce::Timer{double last=juce::Time::getMillisecondCounterHiRes(),gap=0;void timerCallback()override{auto now=juce::Time::getMillisecondCounterHiRes();gap=juce::jmax(gap,now-last);last=now;}} heartbeat;heartbeat.startTimer(2);
 for(int action=0;action<3;++action){double clickStart=now();editor.metadata363Action(action,taggedA);double elapsed=now()-clickStart;if(elapsed>50||!wait([&]{return editor.metadata363Ready();}))return 3340;std::cout<<"Metadata action "<<action<<" click "<<elapsed<<" ms\n";}
 auto trashTarget=a.getChildFile("sample_1.wav");editor.trash340ForCheck(trashTarget);if(!wait([&]{return !trashTarget.existsAsFile()&&!p.trashWorker->historyPending()&&!editor.hasFile363(trashTarget);} ))return 3341;
 editor.undoForCheck(false);bool blank=false;if(!wait([&]{blank|=editor.browserRowsForCheck()==0;return trashTarget.existsAsFile()&&!p.trashWorker->historyPending()&&editor.hasFile363(trashTarget);} )||blank)return 3342;
 heartbeat.stopTimer();std::cout<<"Metadata and Trash Undo max heartbeat gap "<<heartbeat.gap<<" ms; no blank list during restore\n";if(heartbeat.gap>100)return 3343;
 Processor stretch(false);stretch.trackOffline=true;stretch.autoCategories=true;stretch.preferredSliceMethod=1;stretch.setStretchMode(true);
 start=now();auto decoded=stretch.readAudio(taggedA);double readMs=now()-start;if(!decoded)return 3336;start=now();if(!stretch.installAudio(decoded,true,0,false)||!stretch.stretchMode)return 3337;double installMs=now()-start;
 if(std::any_of(stretch.slices.begin(),stretch.slices.end(),[&](const Slice& slice){return stretch.categoryOf(slice)>=0;}))return 3338;
 std::cout<<"Stretch cold read/analysis "<<readMs<<" ms; install "<<installMs<<" ms (slice detection/categories skipped)\n";
 if(LibraryStore::typeName(0)!="Slice"||!LibraryStore::matchesType(0,3))return 3335;
 std::cout<<"Shift-before-click fine drag, snap toggles, folder/tag/favourite filtering, asynchronous type assignment and legacy Loop compatibility passed\n";return 0;
}
