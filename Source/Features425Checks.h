#pragma once
int runFeatures425(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryPreparation::checksPaused=true;
 auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 try{
  auto root=LibraryStore::root();root.createDirectory();auto folder=root.getChildFile("Loops");folder.createDirectory();
  juce::AudioBuffer<float> audio(1,96000);audio.clear();for(int hit=0;hit<8;++hit)for(int n=0;n<4000;++n)audio.setSample(0,hit*12000+n,.8f*std::exp(-n/800.f)*std::sin(n*.13f));
  auto a=folder.getChildFile("A.wav"),b=folder.getChildFile("B.wav"),c=folder.getChildFile("C.wav"),d=folder.getChildFile("D.wav");int identity=0;for(auto file:{a,b,c,d}){audio.setSample(0,95000,.01f*++identity);require(writeRenderedAudio(file,audio,48000),"write fixture");}
  Processor p(false);p.autoSave=false;require(p.sliceMethod==1&&p.preferredSliceMethod==1&&p.load(a)&&p.sliceMethod==1,"new loops default transient");p.split(16);p.detectStrength=67;p.loopKey=2;p.pitchSemitones=-3;p.quantizeEnabled=true;p.swingEnabled=true;p.swingLinked=false;p.swingAmount=56;p.slices[0].gainDb=-7;require(p.saveLoop(),"saved grid fixture");
  require(p.load(b)&&p.sliceMethod==1,"saved grid must not change next unsaved default");require(p.load(a)&&p.sliceMethod==0&&p.detectStrength==67,"saved grid preserved on recall");
  auto bpm=Processor::bulkBpm({a,b},135.25);require(bpm.first==2&&bpm.second==0,"bulk BPM");Processor recalled(false);recalled.autoSave=false;require(recalled.load(a)&&recalled.sliceMethod==0&&recalled.slices[0].gainDb==-7&&std::abs(recalled.sourceBpm-135.25)<1e-7,"BPM keeps saved grid and slice edits");require(recalled.load(b)&&recalled.sliceMethod==1,"BPM initializes unsaved loops as transient");
  LibraryStore::setPlaybackType({c},4);LibraryStore::setPlaybackType({d},1);auto result=Processor::bulkSliceMethod({folder,a},1);require(result.changed==2&&result.skipped==2&&!result.failed,"recursive bulk conversion with dedup and unrelated modes skipped");require(recalled.load(a)&&recalled.sliceMethod==1&&recalled.detectStrength==67&&recalled.loopKey==2&&recalled.pitchSemitones==-3&&recalled.quantizeEnabled&&recalled.swingEnabled&&recalled.swingAmount==56&&std::abs(recalled.sourceBpm-135.25)<1e-7,"bulk transient keeps timing/pitch/key/swing/strength");require(Processor::bulkSliceMethod({a,b},0).changed==2&&recalled.load(a)&&recalled.sliceMethod==0,"bulk grid and saved recall");
  // Put all four fixtures back into Slice mode for the real asynchronous UI path.
  LibraryStore::setPlaybackType({c,d},3);require(p.load(a),"loaded bulk UI loop");
  {Editor editor(p);editor.setSize(1400,1120);editor.addToDesktop(0);editor.setVisible(true);editor.toFront(true);require(editor.hotkey425ForCheck(folder),"repeated Next after strength edit");require(p.load(a),"bulk UI source");require(editor.bulk425ForCheck({a,b}),"bulk UI applies loaded configuration without reload or selection loss");editor.removeFromDesktop();}
  struct Head:juce::AudioPlayHead{double beat=0;bool running=true;juce::Optional<PositionInfo> getPosition()const override{PositionInfo info;info.setPpqPosition(beat);info.setBpm(120);info.setIsPlaying(running);return info;}}head;
  RackProcessor rack;rack.prepareToPlay(48000,128);rack.setPlayHead(&head);require(rack.producesMidi(),"rack advertises output");
  for(int i=0;i<2;++i){auto& engine=*rack.loops[i];engine.sequence.bars=1;engine.sequence.notes={{1,60+i*7,100,1,0,.5}};engine.sequence.changed();engine.sequenceEnabled=true;engine.sequenceMidiOut=true;}
  rack.midiChannel[1]=5;juce::AudioBuffer<float> block(2,512);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,99,(juce::uint8)90),3);rack.processBlock(block,midi);
  int ons=0;for(auto event:midi){auto m=event.getMessage();if(m.isNoteOn()){require(event.samplePosition==0&&((m.getNoteNumber()==60&&m.getChannel()==1)||(m.getNoteNumber()==67&&m.getChannel()==5)),"MIDI note/channel/frame or input echo");++ons;}}require(ons==2,"two independent group outputs");
  head.beat=512.*120/60/48000;rack.mute[1]=true;midi.clear();rack.processBlock(block,midi);bool mutedOff=false;for(auto e:midi)if(e.getMessage().isNoteOff()&&e.getMessage().getChannel()==5)mutedOff=true;require(mutedOff,"mute flushes external held notes");
  head.beat+=512.*120/60/48000;rack.loops[0]->sequenceMidiOut=false;midi.clear();rack.processBlock(block,midi);bool off=false;for(auto e:midi)if(e.getMessage().isNoteOff()&&e.getMessage().getNoteNumber()==60)off=true;require(off,"MIDI OUT toggle flush");
  head.beat+=512.*120/60/48000;rack.loops[0]->sequenceMidiOut=true;midi.clear();rack.processBlock(block,midi);ons=0;for(auto e:midi)if(e.getMessage().isNoteOn())++ons;require(ons==1,"enable chases sustained note");
  head.running=false;midi.clear();rack.processBlock(block,midi);off=false;for(auto e:midi)if(e.getMessage().isNoteOff()&&e.getMessage().getNoteNumber()==60)off=true;require(off,"transport stop note-off");
  auto state=rack.stateXml();RackProcessor restored;require(restored.restore(*state,true)&&restored.loops[0]->sequenceMidiOut.load(),"MIDI routing saved with Set");rack.setPlayHead(nullptr);
  require(checkExtractionPlayback420(),"audio continuity");std::cout<<"Transient defaults, BPM preservation, recursive bulk mode/recall, live selection, real navigation focus; MIDI routing, no echo, segmented buffers, mute/toggle/stop note-offs and Set recall passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 4251;}
}
