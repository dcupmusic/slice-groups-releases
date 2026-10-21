#pragma once
int runOutput385(){
 juce::ScopedJuceInitialiser_GUI gui;
 RackProcessor rack;rack.enableAllBuses();
 auto source=[](float level){auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(2,48000);for(int i=0;i<48000;++i){data->buffer.setSample(0,i,level);data->buffer.setSample(1,i,level*.5f);}return data;};
 for(int i=0;i<4;++i){auto& p=*rack.loops[i];p.audio=source(i<2?.6f:i==2?.1f:.4f);p.split(1,false);p.oneShotMode=true;p.shotLoop=true;p.loopKey=0;p.shotEnvelope={0,0,1,.1f};p.normaliseEnabled=false;rack.output[i]=i<2?0:i-1;}rack.volumeDb[3]=12;rack.prepareToPlay(48000,128);
 juce::MidiBuffer midi;for(int i=0;i<4;++i)midi.addEvent(juce::MidiMessage::noteOn(i+1,60,1.f),0);juce::AudioBuffer<float> out(18,1024);rack.processBlock(out,midi);
 const double ceiling=std::pow(10.,-1./20);for(int ch=0;ch<18;++ch)if(out.getMagnitude(ch,0,1024)>ceiling+1e-6)return 3851;
 if(std::abs(out.getSample(0,1000)-ceiling)>1e-6||std::abs(out.getSample(4,1000)-ceiling)>1e-6)return 3852;
 for(int pair:{0,1,2})for(int i=100;i<1024;++i)if(std::abs(out.getSample(pair*2+1,i)-out.getSample(pair*2,i)*.5f)>1e-6)return 3853;
 if(std::abs(out.getSample(2,1000)-.1f)>1e-6||rack.outputLimiting.load()<.2f)return 3854;
 {RackEditor editor(rack);if(!editor.limiter385UiForCheck())return 3855;auto folder=LibraryStore::root().getChildFile("screenshots");folder.createDirectory();if(!editor.activeEditorForCheck().compact385UiForCheck(folder))return 3856;editor.setSize(1000,1000);auto image=editor.createComponentSnapshot(editor.getLocalBounds());auto stream=folder.getChildFile("rack-master-limiter.png").createOutputStream();juce::PNGImageFormat png;if(stream)png.writeImageToStream(image,*stream);}
 rack.mute[0]=rack.mute[1]=rack.mute[3]=true;for(int i=0;i<300;++i)rack.processBlock(out,midi);if(rack.outputLimiting.load()>.0001f||std::abs(out.getSample(2,1000)-.1f)>1e-6)return 3857;
 // Polyphonic one-shots alone can overload a group, even with normalise off.
 for(int i=0;i<4;++i)rack.loops[i]->clearVoices();rack.mute[0]=false;for(int note=0;note<8;++note)midi.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);rack.processBlock(out,midi);if(out.getMagnitude(0,1024)>ceiling+1e-6||rack.outputLimiting.load()<.5f)return 3858;
 std::cout<<"Final limiter: summed groups, post-fader gain, polyphonic one-shots, independent auxiliary outputs, stereo linking, oversized host blocks, LED release and six compact UI layouts passed.\n";return 0;
}
