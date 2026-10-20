#pragma once
int runSharing370(juce::String mode,juce::File samples){
 juce::ScopedJuceInitialiser_GUI gui;samples.createDirectory();LibraryStore::addLibraryRoot(samples);auto file=samples.getChildFile("nested/test_C.wav");
 auto wait=[](auto predicate){auto until=juce::Time::getMillisecondCounterHiRes()+12000;while(!predicate()&&juce::Time::getMillisecondCounterHiRes()<until){CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);juce::Thread::sleep(10);}return predicate();};auto& sharing=OwnedLibrarySharing::instance();
 if(mode=="owner"){
  file.getParentDirectory().createDirectory();juce::AudioBuffer<float> buffer(1,12000);buffer.clear();if(!writeRenderedAudio(file,buffer,12000))return 3701;
  LibraryStore::addTag({file},"Shared, Test");LibraryStore::setFileKey(file,0);LibraryStore::setPlaybackType({samples},4);LibraryStore::setFavourite(file,"private favourite");
  Processor p(false);p.trackOffline=true;p.load(file);p.normaliseEnabled=p.normaliseLufs=true;p.targetLufs=-23;auto config=p.configurationXml(false);if(config->hasAttribute("normalise")||config->hasAttribute("normaliseLufs")||config->hasAttribute("targetLufs"))return 3702;if(!LibraryStore::saveLoop(file,config->toString()))return 3703;
  auto loopOnly=samples.getChildFile("nested/loop_only_D.wav");writeRenderedAudio(loopOnly,buffer,12000);config->setAttribute("loopKey",7);if(!LibraryStore::saveLoop(loopOnly,config->toString()))return 3730;
  if(!sharing.claim(samples))return 3704;
  if(!wait([&]{auto owner=juce::JSON::parse(samples.getChildFile(".slicegroups/sharing-v2/owner.json")).getProperty("owner",{}).toString();auto doc=juce::JSON::parse(samples.getChildFile(".slicegroups/sharing-v2/"+owner+".json"));auto fields=doc.getProperty("fields",{});return fields.isObject()&&fields.getProperty("tags.json",{}).isObject()&&fields.getProperty("loops.json",{}).isObject();}))return 3705;
  std::cout<<"Owner published portable tags, keys, types and timing; no normalise state in saved loop\n";
 }else if(mode=="update"){
  if(!sharing.claim(samples))return 3722;LibraryStore::removeTags({file},"Shared");sharing.refresh();
  if(!wait([&]{auto owner=juce::JSON::parse(samples.getChildFile(".slicegroups/sharing-v2/owner.json")).getProperty("owner",{}).toString();auto doc=juce::JSON::parse(samples.getChildFile(".slicegroups/sharing-v2/"+owner+".json"));auto value=doc.getProperty("fields",{}).getProperty("tags.json",{});return value.isObject()&&value.getDynamicObject()->getProperty("nested/test_C.wav").toString()=="Test";}))return 3723;
  std::cout<<"Owner tag removal published without stale values\n";
 }else{
  if(!wait([&]{return !sharing.editable(file)&&LibraryStore::tagsFor(LibraryStore::read("tags.json"),file).contains(mode=="reader-deleted"?"Test":"Shared");}))return 3706;
  if(mode=="reader-deleted"&&LibraryStore::tagsFor(LibraryStore::read("tags.json"),file).contains("Shared"))return 3724;
  if(LibraryStore::playbackType(file)!=4||(int)LibraryStore::read("keys.json").getDynamicObject()->getProperty(file.getFullPathName())!=0)return 3707;
  auto imported=LibraryStore::read("keys.json");if((int)imported.getDynamicObject()->getProperty(samples.getChildFile("nested/loop_only_D.wav").getFullPathName())!=7)return 3731;
  auto config=LibraryStore::savedLoop(file);if(!config||config->getStringAttribute("file")!=file.getFullPathName())return 3708;
  if(LibraryStore::read("favourites.json").getDynamicObject()->hasProperty(file.getFullPathName()))return 3709;
  if(LibraryStore::addTag({file},"Forbidden")||LibraryStore::saveLoop(file,config->toString()))return 3710;
  if(!LibraryStore::setFavourite(file,"private on reader"))return 3711;
  std::cout<<"Collaborator at different path reads tags/key/type/timing; writes rejected; favourites remain private\n";
  if(mode=="reader-watch"){std::cout<<"Reader ready for live update\n"<<std::flush;if(!wait([&]{auto tags=LibraryStore::tagsFor(LibraryStore::read("tags.json"),file);return tags.contains("Test")&&!tags.contains("Shared");}))return 3732;std::cout<<"Live collaborator refresh received tag removal without restart\n";}
 }
 return 0;
}
int runFeatures370(){
 juce::ScopedJuceInitialiser_GUI gui;auto root=LibraryStore::root().getChildFile("Samples"),a=root.getChildFile("A"),b=root.getChildFile("B");a.createDirectory();b.createDirectory();
 auto data=std::make_shared<AudioData>();data->rate=12000;data->buffer.setSize(1,24000);for(int i=0;i<24000;++i)data->buffer.setSample(0,i,.2f*std::sin(i*.2));data->buildWaveformPeaks();data->path=a.getChildFile("one_C.wav").getFullPathName();auto file=juce::File(data->path);writeRenderedAudio(file,data->buffer,data->rate);auto second=b.getChildFile("two.wav");writeRenderedAudio(second,data->buffer,data->rate);auto third=b.getChildFile("three.wav");writeRenderedAudio(third,data->buffer,data->rate);LibraryStore::addLibraryRoot(root);LibraryStore::addTag({file},"first");LibraryStore::addTag({second},"second");LibraryStore::addTag({third},"first,second");
 Processor p(false);p.trackOffline=true;p.audio=data;p.sourceBpm=120;p.split(4,false);
 {Waveform wave(p);wave.setSize(1000,200);wave.state.audio=data;wave.state.slices=p.slices;float x=(float)wave.sampleToX(9000);auto event=juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,100},juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier,1,0,0,0,0,&wave,&wave,juce::Time::getCurrentTime(),{x,100},juce::Time::getCurrentTime(),1,false);wave.mouseDown(event);wave.mouseUp(event);if(p.slices[1].start!=9000)return 3728;p.split(4,false);}
 Editor editor(p);editor.diagnosticQuietAnalysis();editor.navigateForCheck(root);
 auto wait=[](auto predicate){auto until=juce::Time::getMillisecondCounterHiRes()+10000;while(!predicate()&&juce::Time::getMillisecondCounterHiRes()<until)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return predicate();};if(!wait([&]{return editor.indexReadyForCheck()&&editor.browserRowsForCheck()==3;}))return 3712;
 if(!editor.features370Ui()||!wait([&]{return editor.columns370Persisted();}))return 3713;
 editor.tags370ForCheck("first","second",false);if(!wait([&]{return editor.browserRowsForCheck()==1;}))return 3714;
 editor.tags370ForCheck("first","second",true);if(!wait([&]{return editor.browserRowsForCheck()==3;}))return 3715;
 editor.folders370ForCheck(a,b);if(!wait([&]{return editor.browserRowsForCheck()==3;}))return 3716;editor.folders370Deselect(b);if(!wait([&]{return editor.browserRowsForCheck()==1;}))return 3729;
 auto config=p.configurationXml(false);config->setAttribute("normalise",true);config->setAttribute("normaliseLufs",true);p.normaliseEnabled=p.normaliseLufs=false;if(!p.applyConfiguration(*config,false)||p.normaliseEnabled||p.normaliseLufs)return 3717;
 p.setPlaybackMode(3);p.shotBendRange=12;p.outputRate=12000;p.shotEnvelope={0,0,1,0};p.shotLoop=true;p.shotLoopStart=0;p.shotLoopEnd=24000;p.rootOctave=3;p.loopKey=0;
 juce::AudioBuffer<float> out(2,2400);out.clear();juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,60,(juce::uint8)127),0);midi.addEvent(juce::MidiMessage::pitchWheel(1,16383),100);p.renderShots(out,midi);auto& voice=p.shots[0];double expected=std::pow(2.,(60-60+12)/12.);if(std::abs(voice.ratio-expected)>.001)return 3718;
 p.shotBendRange=7;auto shot=p.configurationXml(false);if(shot->getIntAttribute("shotBendRange")!=7)return 3719;
 editor.navigateForCheck(a);if(!wait([&]{return editor.indexReadyForCheck()&&editor.browserRowsForCheck()==1;}))return 3725;editor.revealForCheck();bool blank=false;if(!wait([&]{blank|=editor.browserRowsForCheck()==0;return editor.revealedForCheck();})||blank)return 3726;
 if(!editor.render370ForCheck(3000,9000)||!wait([&]{return editor.render370Ready();}))return 3727;
 std::shared_ptr<AudioData> trimmed;auto copy=a.getChildFile("render-copy.wav");writeRenderedAudio(copy,data->buffer,data->rate);auto result=DestructiveRender::trim(copy,*data,3000,9000,&trimmed);if(result.failed()||!trimmed||trimmed->buffer.getNumSamples()!=6000||std::abs(trimmed->buffer.getSample(0,0)-data->buffer.getSample(0,3000))>1e-6)return 3720;
 juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));if(!reader||reader->lengthInSamples!=6000)return 3721;
 auto image=editor.createComponentSnapshot(editor.getLocalBounds());juce::PNGImageFormat png;auto stream=LibraryStore::root().getChildFile("features370.png").createOutputStream();if(stream)png.writeImageToStream(image,*stream);
 std::cout<<"Column reorder, peak UI, AND/OR tags, multi-folder scope, normalise persistence, pitch bend, and atomic source trim passed\n";return 0;
}

int runSharing370Args(const char* mode,const char* folder){return runSharing370(mode,juce::File(folder));}
