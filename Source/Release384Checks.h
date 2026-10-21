#pragma once
int runRelease384(){
 juce::ScopedJuceInitialiser_GUI gui;
 auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,48000);for(int i=0;i<48000;++i)data->buffer.setSample(0,i,i<1200?.2f:.7f);
 for(bool exit:{false,true})for(bool ping:{false,true}){
  Processor p(false);p.audio=data;p.split(1,false);p.oneShotMode=true;p.shotLoop=true;p.shotExitLoop=exit;p.shotPingPong=ping;p.shotStart=1000;p.shotEnd=48000;p.shotLoopStart=1000;p.shotLoopEnd=1200;p.shotEnvelope={0,0,1,.5f};p.prepareToPlay(48000,128);p.triggerShot(60,1);juce::AudioBuffer<float> out(2,128);juce::MidiBuffer midi;
  for(int i=0;i<12;++i){out.clear();p.renderShots(out,midi);}auto& voice=p.shots[0];if(ping&&exit){voice.pos=1100;voice.direction=-1;}
  float previous=out.getSample(0,127);midi.addEvent(juce::MidiMessage::noteOff(1,60),0);out.clear();p.renderShots(out,midi);if(std::abs(out.getSample(0,0)-previous)>.003)return 3841;
  for(int i=0;i<16;++i){out.clear();p.renderShots(out,midi);}if(!voice.active)return 3842;if(exit?(voice.pos<=1200||voice.direction!=1):(voice.pos<1000||voice.pos>=1200))return 3843;
  auto xml=p.configurationXml();Processor restored(false);restored.audio=data;if(!restored.applyConfiguration(*xml,false)||restored.shotExitLoop!=exit)return 3844;
  if(!exit){xml->removeAttribute("shotExitLoop");restored.shotExitLoop=true;if(!restored.applyConfiguration(*xml,false)||restored.shotExitLoop)return 3845;}
  for(int i=0;i<200;++i){out.clear();p.renderShots(out,midi);}if(voice.active)return 3846;
 }
 Processor p(false);p.audio=data;p.split(1,false);Editor editor(p);editor.diagnosticQuietAnalysis();if(!editor.release384UiForCheck())return 3847;
 std::cout<<"Loop release: remain/exit, forward/ping-pong, continuous handover, ADSR completion, saved state, old saves and UI passed.\n";return 0;
}
