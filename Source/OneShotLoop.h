#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
namespace OneShotLoop {
inline double wrap(double value,double period){return value-std::floor(value/period)*period;}
inline void normalise(double& pos,int& direction,double begin,double end,bool pingPong){
    if(!pingPong){direction=1;if(pos>=end||pos<begin)pos=begin+wrap(pos-begin,juce::jmax(1.,end-begin));return;}
    double span=end-begin-1;
    if(span<=0){pos=begin;direction=1;return;}
    double phase=wrap(direction>0?pos-begin:2*span-(pos-begin),2*span);
    direction=phase<span?1:-1;pos=begin+(phase<=span?phase:2*span-phase);
}
// Match the waveform phase of a new, stationary loop window to the outgoing one.
// The travel cursor chooses the neighbourhood; it never adds to playback speed.
// Bounded work, stack storage only, and shared channel offsets preserve stereo phase.
inline double alignTravel(const juce::AudioBuffer<float>& buffer,double reference,double wanted,double length,double lower,double upper){
    wanted=juce::jlimit(lower,upper,wanted);
    if(length<4||upper<=lower)return wanted;
    constexpr int points=16;const int channels=juce::jmin(2,buffer.getNumChannels());
    const int size=buffer.getNumSamples();
    auto sample=[&](int ch,double pos){pos=juce::jlimit(0.,(double)size-1,pos);int i=(int)pos;const float* data=buffer.getReadPointer(ch);return data[i]+(pos-i)*(data[juce::jmin(size-1,i+1)]-data[i]);};
    double ref[2][points]{},refEnergy=0;
    for(int ch=0;ch<channels;++ch){double mean=0;for(int i=0;i<points;++i)mean+=(ref[ch][i]=sample(ch,reference+(i+.5+(i%3-1)*.19)*length/points));mean/=points;for(int i=0;i<points;++i){ref[ch][i]-=mean;refEnergy+=ref[ch][i]*ref[ch][i];}}
    if(refEnergy<1e-12)return wanted;
    auto cost=[&](double begin){double dot=0,energy=0;for(int ch=0;ch<channels;++ch){double values[points],mean=0;for(int i=0;i<points;++i)mean+=(values[i]=sample(ch,begin+(i+.5+(i%3-1)*.19)*length/points));mean/=points;for(int i=0;i<points;++i){double v=values[i]-mean;dot+=v*ref[ch][i];energy+=v*v;}}return energy>1e-12?1-dot/std::sqrt(refEnergy*energy):2.;};
    double radius=juce::jmin(length*.5,1024.),left=juce::jmax(lower,wanted-radius),right=juce::jmin(upper,wanted+radius);
    double best=wanted,bestCost=cost(best),spacing=(right-left)/64;
    auto consider=[&](double pos){double value=cost(pos);if(value<bestCost-1e-10||(std::abs(value-bestCost)<=1e-10&&std::abs(pos-wanted)<std::abs(best-wanted))){best=pos;bestCost=value;}};
    for(int i=0;i<=64;++i)consider(left+i*spacing);
    // Refine to fractional samples; integer rounding would let phase errors build up.
    for(int pass=0;pass<4;++pass){double centre=best;spacing=juce::jmax(.002,spacing/4);for(int i=-4;i<=4;++i)consider(juce::jlimit(left,right,centre+i*spacing));}
    double h=.002,a=cost(juce::jmax(left,best-h)),b=cost(best),c=cost(juce::jmin(right,best+h)),curvature=a-2*b+c;
    if(curvature>1e-14)consider(juce::jlimit(left,right,best+juce::jlimit(-h,h,.5*h*(a-c)/curvature)));
    return best;
}
// Period-locked overlap/add: neighbouring source cycles contribute without shortening
// the loop or advancing its clock. Wider windows blend more surrounding audio.
inline float read(const juce::AudioBuffer<float>& buffer,int channel,double pos,double begin,double end,double crossfade){
    auto* samples=buffer.getReadPointer(juce::jmin(channel,buffer.getNumChannels()-1));int size=buffer.getNumSamples();
    auto raw=[&](double p){p=juce::jlimit(0.,(double)size-1,p);int i=(int)p;return samples[i]+(float)(p-i)*(samples[juce::jmin(i+1,size-1)]-samples[i]);};
    double length=juce::jmax(1.,end-begin),phase=wrap(pos-begin,length);
    // Odd reflection gives continuous context at the physical recording edges too.
    auto context=[&](double p){if(size<2)return raw(0);if(p>=0&&p<=size-1)return raw(p);double span=size-1,cycles=std::floor(p/(2*span)),q=wrap(p,2*span);double offset=2*cycles*(samples[size-1]-samples[0]);return (float)(offset+(q<=span?raw(q):2*samples[size-1]-raw(2*span-q)));};
    if(crossfade<=0)return raw(begin+phase);
    double radius=length*.5+juce::jlimit(0.,length,crossfade),sum=0,weight=0;
    int first=(int)std::ceil((length*.5-radius-phase)/length),last=(int)std::floor((length*.5+radius-phase)/length);
    for(int k=first;k<=last;++k){double t=phase+k*length,u=(t-length*.5)/radius,w=juce::jmax(0.,1-u*u);w*=w;sum+=w*context(begin+t);weight+=w;}
    return weight>1e-12?(float)(sum/weight):raw(begin+phase);
}
}
