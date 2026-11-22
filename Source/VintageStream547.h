#pragma once
// Per-group streaming character stage for modes that already render at host tempo.
// No allocation, filesystem access, or full-track conversion on the audio thread.
struct VintageStream547 {
 std::atomic<bool> enabled{false},filter{true};std::atomic<double> rate{27000},colour{0},hiss{0};
 struct Channel{double in1=0,in2=0,out1=0,out2=0,predictor=0,previous=0,held=0,phase=1;uint32_t random=0x454d5532;};std::array<Channel,32> channels;double mix=0;
 void configure(const VintageSettings& s,bool active){rate=s.rate;colour=s.colour;hiss=s.hiss;filter=s.filter;enabled=s.enabled&&active;}
 void reset(){channels={};mix=0;}
 void process(juce::AudioBuffer<float>& audio,double sampleRate){bool active=enabled.load();if(!active&&mix<=0)return;double sr=juce::jlimit(1000.,sampleRate,rate.load()),step=sr/sampleRate,coefficient=-std::expm1(-2*juce::MathConstants<double>::pi*sr*.42/sampleRate),mu=255,logMu=std::log1p(mu),decodeMu=mu*std::exp2(colour.load()/100),decodeLog=std::log1p(decodeMu),noise=hiss.load()/100*.002,smooth=1./juce::jmax(1.,sampleRate*.005);bool filtered=filter.load();
  for(int n=0;n<audio.getNumSamples();++n){mix=juce::jlimit(0.,1.,mix+(active?smooth:-smooth));for(int ch=0;ch<juce::jmin(audio.getNumChannels(),(int)channels.size());++ch){auto& s=channels[ch];double dry=audio.getSample(ch,n),input=dry;if(filtered){s.in1+=coefficient*(input-s.in1);s.in2+=coefficient*(s.in1-s.in2);input=s.in2;}s.phase+=step;if(s.phase>=1){s.phase-=std::floor(s.phase);double delta=juce::jlimit(-1.,1.,input-s.predictor);double encoded=std::round(std::copysign(std::log1p(mu*std::abs(delta))/logMu,delta)*127)/127;double decoded=std::copysign(std::expm1(std::abs(encoded)*decodeLog)/decodeMu,encoded);s.predictor=juce::jlimit(-1.2,1.2,s.predictor+decoded);s.previous=s.held;s.held=s.predictor;}double value=s.previous+(s.held-s.previous)*s.phase;if(filtered){s.out1+=coefficient*(value-s.out1);s.out2+=coefficient*(s.out1-s.out2);value=s.out2;}s.random^=s.random<<13;s.random^=s.random>>17;s.random^=s.random<<5;value+=((double)s.random/2147483648.-1)*noise;audio.setSample(ch,n,(float)(dry+(value-dry)*mix));}}
 }
};
