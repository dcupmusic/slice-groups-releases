#pragma once
int runVintage5237(){
 juce::ScopedJuceInitialiser_GUI init;auto audio=std::make_shared<AudioData>();audio->rate=44100;audio->buffer.setSize(2,44100);
 for(int ch=0;ch<2;++ch)for(int n=0;n<44100;++n)audio->buffer.setSample(ch,n,(float)(.25*std::sin(n*.071)+.1*std::sin(n*.89)));
 VintageSettings settings;settings.enabled=true;settings.rate=12000;settings.colour=35;auto wet=VintageResampler::convert(*audio,settings);if(wet.getNumSamples()!=44100||wet.getNumChannels()!=2)return 2371;double difference=0;for(int n=0;n<44100;++n){float v=wet.getSample(0,n);if(!std::isfinite(v)||std::abs(v)>1.3f)return 2372;difference+=std::abs(v-audio->buffer.getSample(0,n));}if(difference<10)return 2373;
 Processor p(false);p.autoSave=false;p.audio=audio;p.split(1,false);p.oneShotMode=true;p.shotRootNote=60;p.vintageSettings=settings;p.serviceVintage();for(int i=0;i<500&&p.vintageWorker.busy;++i)juce::Thread::sleep(2);auto prepared=p.vintageWorker.audioResult();if(!prepared||!prepared->find(audio.get()))return 2374;
 auto xml=p.configurationXml(true);Processor recall(false);recall.autoSave=false;recall.audio=audio;if(!recall.applyConfiguration(*xml,true)||!(recall.vintageSettings==settings))return 2375;
 p.prepareToPlay(44100,512);p.shotNoteOn(60,1);juce::MidiBuffer midi;juce::AudioBuffer<float> out(2,512);out.clear();p.renderShots(out,midi);if(out.getMagnitude(0,512)<.001f)return 2376;
 std::cout<<"Vintage smoke passed: finite coloured audio, background preparation, One Shot playback and configuration recall\n";return 0;
}
