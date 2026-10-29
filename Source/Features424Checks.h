#pragma once
int runFeatures424(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryPreparation::checksPaused=true;
 auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 auto wait=[](auto predicate){auto until=juce::Time::getMillisecondCounterHiRes()+15000;while(!predicate()&&juce::Time::getMillisecondCounterHiRes()<until)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.003,true);return predicate();};
 try{
  auto root=LibraryStore::root().getChildFile("Browser fixture");root.getChildFile("Nested").createDirectory();LibraryStore::addLibraryRoot(root);
  RackProcessor rack;std::array<std::shared_ptr<const AudioData>,8> sources;
  for(int i=0;i<8;++i){auto data=std::make_shared<AudioData>();data->rate=48000;data->path=root.getChildFile("group "+juce::String(i)+".wav").getFullPathName();data->buffer.setSize(1,96000);data->peakAmplitude=.25f;for(int n=0;n<96000;++n)data->buffer.setSample(0,n,.25f*std::sin(n*2*juce::MathConstants<double>::pi*(110+i*30)/48000));auto& p=*rack.loops[i];p.autoSave=false;require(p.installAudio(data,false,0,false),"load group fixture");p.setPlaybackMode(i%3==0?0:i%3==1?1:3);p.normaliseEnabled=false;sources[i]=data;}
  auto opened=juce::Time::getMillisecondCounterHiRes();RackEditor editor(rack);editor.setSize(1400,1232);editor.setVisible(true);std::cout<<"Open eight prepared group editors: "<<juce::Time::getMillisecondCounterHiRes()-opened<<" ms\n";
  require(wait([&]{return editor.activeEditorForCheck().ready424Browser();}),"initial browser index");
  editor.activeEditorForCheck().browser424Fixture(root,50000);
  auto token=editor.activeEditorForCheck().index424Token(),builds=editor.activeEditorForCheck().builds424Count();auto catalog=editor.activeEditorForCheck().catalog424Pointer();double maximum=0;
  for(int pass=0;pass<5;++pass)for(int i=0;i<8;++i){auto began=juce::Time::getMillisecondCounterHiRes();editor.tabForCheck(i);double elapsed=juce::Time::getMillisecondCounterHiRes()-began;maximum=juce::jmax(maximum,elapsed);require(editor.activeEditorForCheck().group424Immediate(root,50000,token,builds,catalog),"first frame: list, selection, catalogue, waveform or controls changed");CFRunLoopRunInMode(kCFRunLoopDefaultMode,.003,true);require(editor.activeEditorForCheck().group424Immediate(root,50000,token,builds,catalog),"delayed empty browser or rescan after switching");}
  std::cout<<"Group switch max "<<maximum<<" ms\n";require(maximum<40,"group switch latency");std::cout<<"40 switches, 50,000 files, first and warm visits: max "<<maximum<<" ms; zero index requests/catalogue rebuilds/empty frames\n";
  int origin=rack.selected;editor.activeEditorForCheck().pending424Search();editor.tabForCheck(2);require(editor.activeEditorForCheck().browserRowsForCheck()==50000,"other group retains its unfiltered view");editor.tabForCheck(origin);require(wait([&]{return editor.activeEditorForCheck().search424Complete();}),"pending search resumes in its original group");require(editor.activeEditorForCheck().index424Token()==token,"search during switch rescans");
  auto before=editor.activeEditorForCheck().catalog424Pointer();editor.activeEditorForCheck().pending424Sort();editor.tabForCheck(4);editor.tabForCheck(origin);require(wait([&]{return editor.activeEditorForCheck().sort424Complete(before);}),"pending sort resumes in its original group");
  editor.activeEditorForCheck().request424Metadata();editor.tabForCheck(1);require(wait([&]{return editor.activeEditorForCheck().metadata424Complete();}),"shared metadata updates across group contexts");
  for(int i=0;i<8;++i)require(rack.loops[i]->getAudio()==sources[i],"browser rebinding must not load audio");
  auto image=editor.createComponentSnapshot(editor.getLocalBounds());require(!image.isNull(),"native first paint");auto file=LibraryStore::root().getChildFile("group424.png");juce::FileOutputStream out(file);juce::PNGImageFormat().writeImageToStream(image,out);
  auto actual=root.getSiblingFile("Actual files");actual.createDirectory();juce::AudioBuffer<float> pcm(1,4800);pcm.clear();for(int i=0;i<5;++i)require(writeRenderedAudio(actual.getChildFile("real "+juce::String(i)+".wav"),pcm,48000),"actual browser fixture");
  LibraryStore::addLibraryRoot(actual);origin=rack.selected;editor.activeEditorForCheck().navigateForCheck(actual);editor.tabForCheck(6);require(editor.activeEditorForCheck().browserRowsForCheck()==50000,"pending navigation cannot replace another group's view");editor.tabForCheck(origin);require(wait([&]{return editor.activeEditorForCheck().indexReadyForCheck()&&editor.activeEditorForCheck().browserRowsForCheck()==5;}),"pending file index resumes in original group");
  require(checkExtractionPlayback420(),"audio continuity under UI lock");
  std::cout<<"In-flight search/sort/metadata, group audio preservation and native painting passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 4241;}
}
