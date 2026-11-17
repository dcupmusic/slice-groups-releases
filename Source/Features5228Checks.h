#pragma once
int runFeatures5228(){juce::ScopedJuceInitialiser_GUI gui;LibraryPreparation::checksPaused=true;try{
 auto require=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
 Processor p(false);p.autoSave=false;p.trackOffline=true;p.trackMode=true;p.stretchMode=false;p.trackStemsEnabled=false;p.trackGridReady=true;p.trackGrid.bpm=120;p.trackLoop=true;p.trackLoopStart=0;p.trackLoopEnd=4;
 auto audio=std::make_shared<AudioData>();audio->rate=22050;audio->path=LibraryStore::root().getChildFile("Track extraction.wav").getFullPathName();audio->buffer.setSize(2,44100);for(int n=0;n<44100;++n){float v=.3f*std::sin(juce::MathConstants<double>::twoPi*220*n/22050.);for(int ch=0;ch<2;++ch)audio->buffer.setSample(ch,n,v);}p.audio=audio;
 Editor e(p,false);e.diagnosticQuietAnalysis();e.stopTimer();e.browserAutoLoad=false;e.setSize(1100,1000);e.refresh();e.resized();require(e.trackPanel.isVisible()&&!e.toolsTab.isVisible()&&e.extractMidi.isVisible(),"Track controls show Extract MIDI and hide MIDI Tools");
 e.extractMidi.onClick();require(p.sequenceView&&e.sequencePanel.isVisible(),"Extract opens sequencer");
 auto check=[&]{e.refresh();require(p.trackMode&&p.audio==audio&&p.sequenceView,"Track and sequencer state retained");require(e.sequencePanel.isVisible()&&!e.trackPanel.isVisible(),"refresh keeps sequencer above Track");require(!e.toolsTab.isVisible(),"MIDI Tools absent in Track sequencer");};
 for(int i=0;i<6;++i){e.lastControlRefresh=0;e.refreshTimerControls(juce::Time::getMillisecondCounterHiRes());check();}
 auto until=juce::Time::getMillisecondCounterHiRes()+30000;while(e.sequencePanel.extracting&&juce::Time::getMillisecondCounterHiRes()<until){CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);check();}require(!e.sequencePanel.extracting&&p.extractionActive&&!p.sequence.notes.empty(),"real asynchronous MIDI extraction completes with notes");check();
 for(int width:{720,1100,1700}){e.setSize(width,1000);e.resized();check();require(e.waveTab.isVisible()&&e.sequenceTab.isVisible(),"view tabs remain available");e.waveTab.onClick();e.refresh();require(!p.sequenceView&&e.trackPanel.isVisible()&&!e.sequencePanel.isVisible()&&!e.toolsTab.isVisible(),"return to Track");e.sequenceTab.onClick();check();}
 e.waveTab.onClick();p.trackMode=false;p.stretchMode=true;e.refresh();e.resized();require(e.toolsTab.isVisible(),"MIDI Tools retained outside Track");
 std::cout<<"5228 passed: asynchronous Track extraction, repeated control refresh, resize and Track/Sequencer switching; Track MIDI Tools hidden.\n";return 0;
 }catch(const std::exception& error){std::cerr<<"5228 failure: "<<error.what()<<"\n";return 1;}}
