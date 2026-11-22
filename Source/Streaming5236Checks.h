#pragma once
// One small playback smoke check for the local evaluation build.
int runStreaming5236(){
 juce::ScopedJuceInitialiser_GUI init;
 auto audio=std::make_shared<AudioData>();audio->rate=44100;audio->buffer.setSize(2,44100*8);
 for(int ch=0;ch<2;++ch)for(int n=0;n<audio->buffer.getNumSamples();++n)audio->buffer.setSample(ch,n,.2f*std::sin((float)(2*juce::MathConstants<double>::pi*220*n/44100.)));
 TrackStemWorker stems;TrackStreaming stream(stems);TrackGrid grid;grid.bpm=120;grid.anchors={{0,0}};
 auto wait=[&](double beat){auto until=juce::Time::getMillisecondCounter()+4000;while(juce::Time::getMillisecondCounter()<until){auto window=stream.audioResult();if(window&&window->at(beat)&&window->at(beat+.08))return window;juce::Thread::sleep(2);}return std::shared_ptr<const TrackStreaming::Window>{};};
 stream.request({audio,grid,100,0,false,false,0,16,0},0);
 auto first=wait(0);if(!first)return 2361;
 double energy=0;int crossings=0;float prior=0;for(int n=300;n<5000;++n){double beat=n/44100.*100/60;std::array<float,5> values{};float dry=0;bool separated=false;if(!first->read(beat,0,values,dry,separated)){first=wait(beat);if(!first||!first->read(beat,0,values,dry,separated))return 2362;}energy+=dry*dry;if(prior<0&&dry>=0)++crossings;prior=dry;}
 if(energy<1||crossings<18||crossings>28)return 2363;
 stream.seek(8);auto jumped=wait(8);if(!jumped)return 2364;
 stream.request({audio,grid,100,12,false,false,0,16,0},8);auto pitched=wait(8);auto until=juce::Time::getMillisecondCounter()+4000;while(pitched&&pitched->configuration->pitch!=12&&juce::Time::getMillisecondCounter()<until){juce::Thread::sleep(2);pitched=wait(8);}if(!pitched||pitched->configuration->pitch!=12)return 2365;
 TrackPlayer player;player.publish(audio,grid,false,0,16,1,100,0,false);juce::AudioBuffer<float> block(2,256);double ppq=0;int audible=0;
 for(int n=0;n<120;++n){player.render(block,44100,true,ppq,100);ppq+=256./44100*100/60;if(block.getMagnitude(0,256)>.001f)++audible;juce::Thread::sleep(6);}
 if(audible<50)return 2366;auto session=std::atomic_load(&player.session);if(!session||!session->streaming||session->rendered)return 2367;
 std::cout<<"Streaming smoke passed: audible tempo-adjusted output, seek, pitch reconfiguration, and TrackPlayer callback path\n";return 0;
}
