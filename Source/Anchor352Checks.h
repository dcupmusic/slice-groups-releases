#pragma once
int runAnchor352Checks(){
 juce::ScopedJuceInitialiser_GUI gui;
 struct Head:juce::AudioPlayHead{PositionInfo position;juce::Optional<PositionInfo> getPosition()const override{return position;}};
 for(int numerator:{3,4})for(double tempo:{90.,120.})for(int start:{0,12000}){
  Processor p(false);p.trackOffline=true;p.autoSave=false;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);for(int n=0;n<96000;++n)data->buffer.setSample(0,n,.1f+.3f*n/96000.f);data->buildWaveformPeaks();p.audio=data;p.sourceBpm=120;p.sourceBeats=4;p.trimStart=0;p.trimEnd=96000;p.startMarker=start;p.split(1,false);p.startMarker=start;p.stretchMode=p.playMode=true;p.normaliseEnabled=false;p.hasHost=true;p.hostBpm=tempo;p.hostNumerator=numerator;p.hostDenominator=4;p.stretchAnchorBar=3;p.setStretchAnchor(48000,false);p.prepareToPlay(48000,1024);
  Head head;head.position.setBpm(tempo);head.position.setIsPlaying(true);head.position.setTimeSignature(juce::AudioPlayHead::TimeSignature{numerator,4});p.setPlayHead(&head);auto rendered=StretchWorker::render(p.stretchRequest());if(!rendered)return 3541;p.stretchWorker.cachePrepared(rendered);juce::AudioBuffer<float> out(2,1024);juce::MidiBuffer notes;
  double anchor=2*numerator,begin=anchor-(48000-start)/24000.,end=anchor+2;
  auto play=[&](double beat,bool on=true){head.position.setIsPlaying(on);head.position.setPpqPosition(beat);p.processBlock(out,notes);};
  play(begin-1);if(out.getMagnitude(0,1024)>1e-7||p.voices[0].active)return 3542;
  play(anchor);if(out.getMagnitude(0,1024)<.02||!p.voices[0].active)return 3543;
  if(tempo==120&&std::abs(out.getSample(0,1023)-data->buffer.getSample(0,48000+1023))>.0001)return 3544;
  play(end);if(out.getMagnitude(0,1024)>1e-7||p.voices[0].active)return 3545;
  play(end+4);if(out.getMagnitude(0,1024)>1e-7)return 3546;
  // Blocks crossing either boundary must contain silence on the outside.
  play(begin-512*tempo/60/48000);if(out.getMagnitude(0,500)>1e-7||out.getMagnitude(600,424)<.001)return 3547;
  play(end-256*tempo/60/48000);if(out.getMagnitude(400,624)>1e-7||p.voices[0].active)return 3548;
  play(anchor,false);if(out.getMagnitude(0,1024)>1e-7)return 3549;play(anchor);if(out.getMagnitude(0,1024)<.02)return 3550;
  p.stretchAnchorEnabled=false;play(end+4);if(out.getMagnitude(0,1024)<.02)return 3551;
 }
 std::cout<<"Anchored Stretch single pass: exact anchor alignment, silence before/after, no later repeats or rotated tail, partial-block boundaries, stop/restart, 3/4 and 4/4, 90/120 BPM, normal looping with anchor off passed\n";return 0;
}
