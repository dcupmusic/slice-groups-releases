#pragma once
#include "MusicalKey.h"
#include <juce_dsp/juce_dsp.h>
// Background-only chroma/template detector. No audio files are written.
struct LoopKeyDetection {
 struct Result {MusicalKey key{-1,-1};double confidence=0;};
 static Result chroma(const std::array<double,12>& notes){
  double sum=0;for(auto n:notes)sum+=n;if(sum<1e-8)return {};
  static const double profiles[2][12]={{6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88},{6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17}};
  std::array<std::array<double,12>,2> scores{};double best=-2,second=-2;int root=0,quality=1;
  for(int q=0;q<2;++q)for(int r=0;r<12;++r){double a=0,b=0,ab=0,mean=sum/12,pm=0;for(double p:profiles[q])pm+=p/12;for(int i=0;i<12;++i){double x=notes[(i+r)%12]-mean,y=profiles[q][i]-pm;a+=x*x;b+=y*y;ab+=x*y;}double score=a*b>0?ab/std::sqrt(a*b):0;scores[q][r]=score;if(score>best){second=best;best=score;root=r;quality=q;}else second=juce::jmax(second,score);}
  // Keep the selected tonic; favour minor when its major/minor evidence is close.
  if(std::abs(scores[0][root]-scores[1][root])<.10)quality=1;
  return {{root,quality},juce::jlimit(0.,1.,best-second)};
 }
 static Result file(juce::File file){juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));if(!reader||reader->sampleRate<1000||reader->lengthInSamples<256)return {};
  constexpr int order=13,size=1<<order;juce::dsp::FFT fft(order);std::vector<float> work(size*2);juce::AudioBuffer<float> audio(juce::jmin(2,(int)reader->numChannels),size);std::array<double,12> notes{};
  int64_t hop=juce::jmax<int64_t>(size/2,reader->lengthInSamples/1200);double tonal=0;
  for(int64_t start=0;start<reader->lengthInSamples;start+=hop){audio.clear();if(!reader->read(&audio,0,size,start,true,true))continue;std::fill(work.begin(),work.end(),0);for(int i=0;i<size;++i){float v=0;for(int c=0;c<audio.getNumChannels();++c)v+=audio.getSample(c,i)/audio.getNumChannels();work[i]=v*(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(size-1)));}fft.performFrequencyOnlyForwardTransform(work.data());std::array<double,12> frame{};double total=0;
   for(int k=2;k<size/2-1;++k){double hz=k*reader->sampleRate/size;if(hz<40||hz>5000||work[k]<work[k-1]||work[k]<=work[k+1])continue;double denom=work[k-1]-2*work[k]+work[k+1];double offset=std::abs(denom)>1e-12?.5*(work[k-1]-work[k+1])/denom:0;double midi=69+12*std::log2((k+juce::jlimit(-.5,.5,offset))*reader->sampleRate/size/440.);int note=(int)std::round(midi);if(std::abs(midi-note)>.35)continue;double weight=std::sqrt(juce::jmax(0.f,work[k]));frame[(note%12+12)%12]+=weight;total+=weight;}
   if(total>.001){for(int i=0;i<12;++i)notes[i]+=frame[i]/total;tonal+=total;}
  }return tonal>.01?chroma(notes):Result{};
 }
};
