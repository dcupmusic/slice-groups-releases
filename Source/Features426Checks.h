#pragma once
int runFeatures426(){juce::ScopedJuceInitialiser_GUI gui;LibraryPreparation::checksPaused=true;try{
 auto root=LibraryStore::root().getChildFile("Shortcut files");root.createDirectory();juce::AudioBuffer<float> audio(1,48000);audio.clear();for(int k=0;k<4;++k)for(int n=0;n<3000;++n)audio.setSample(0,k*12000+n,.4f*std::exp(-n/500.f)*std::sin(n*.1f));int id=0;for(auto name:{"A.wav","B.wav","C.wav","D.wav"}){audio.setSample(0,47000,.01f*++id);if(!writeRenderedAudio(root.getChildFile(name),audio,48000))throw std::runtime_error("fixture");}
 Processor p(false);p.autoSave=false;Editor editor(p);editor.setSize(1400,1120);editor.addToDesktop(0);editor.setVisible(true);editor.toFront(true);if(!editor.hotkey426ForCheck(root))throw std::runtime_error("Previous/Trash sequence after transient adjustment");editor.removeFromDesktop();return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 4261;}}
