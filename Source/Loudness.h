#pragma once
#include "Library.h"

// BS.1770 K-weighting, 400 ms blocks, 75% overlap and absolute/relative gates.
// Short one-shots are measured as a repeated loop, not padded with silence.
struct LoopLoudness {
    struct Biquad {double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;double tick(double x){double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}};
    static double measure(const juce::AudioBuffer<float>& audio,double rate,std::function<bool()> cancel={}){
        int length=audio.getNumSamples(),channels=juce::jmin(2,audio.getNumChannels());if(length<=0||channels<=0||rate<8000)return -100.;
        int hop=juce::jmax(1,(int)std::round(rate*.1)),window=4*hop,frames=juce::jmax(length,window);
        std::array<Biquad,2> shelf,high;
        double k=std::tan(juce::MathConstants<double>::pi*1681.974450955533/rate),q=.7071752369554196,vh=std::pow(10.,3.999843853973347/20),vb=std::pow(vh,.4996667741545416),den=1+k/q+k*k;
        for(int ch=0;ch<channels;++ch){shelf[ch].b0=(vh+vb*k/q+k*k)/den;shelf[ch].b1=2*(k*k-vh)/den;shelf[ch].b2=(vh-vb*k/q+k*k)/den;shelf[ch].a1=2*(k*k-1)/den;shelf[ch].a2=(1-k/q+k*k)/den;double r=std::tan(juce::MathConstants<double>::pi*38.13547087602444/rate),d=1+r/.5003270373238773+r*r;high[ch].b0=1;high[ch].b1=-2;high[ch].b2=1;high[ch].a1=2*(r*r-1)/d;high[ch].a2=(1-r/.5003270373238773+r*r)/d;}
        std::vector<double> ring(window),blocks;double sum=0,absolute=std::pow(10.,(-70.+.691)/10.);
        for(int n=0;n<frames;++n){if((n&4095)==0&&cancel&&cancel())return -100.;double power=0;for(int ch=0;ch<channels;++ch){double y=high[ch].tick(shelf[ch].tick(audio.getSample(ch,n%length)));power+=y*y;}int index=n%window;sum+=power-ring[index];ring[index]=power;if(n+1>=window&&(n+1-window)%hop==0){double energy=juce::jmax(0.,sum/window);if(energy>=absolute)blocks.push_back(energy);}}
        if(blocks.empty())return -100.;double first=0;for(auto energy:blocks)first+=energy;double threshold=juce::jmax(absolute,first/blocks.size()*.1),total=0;int count=0;for(auto energy:blocks)if(energy>=threshold){total+=energy;++count;}return count&&total>0?-.691+10*std::log10(total/count):-100.;
    }
    static juce::File cacheFile(juce::File source){auto dir=LibraryStore::root().getChildFile("loudness-v1");return dir.getChildFile(juce::String::toHexString(source.getFullPathName().hashCode64())+".json");}
    static bool cached(juce::File source,double& value){auto data=juce::JSON::parse(cacheFile(source));if(!data.isObject()||(juce::int64)data.getProperty("size",-1)!=source.getSize()||(juce::int64)data.getProperty("modified",-1)!=source.getLastModificationTime().toMilliseconds())return false;value=(double)data.getProperty("lufs",-100.);return std::isfinite(value)&&value>=-100&&value<=30;}
    static bool exists(juce::File source){double value;return cached(source,value);}
    static double analyse(juce::File source,const juce::AudioBuffer<float>& audio,double rate,std::function<bool()> cancel={}){double result;if(cached(source,result))return result;auto size=source.getSize(),modified=source.getLastModificationTime().toMilliseconds();result=measure(audio,rate,cancel);if(cancel&&cancel())return result;if(source.getSize()!=size||source.getLastModificationTime().toMilliseconds()!=modified)return result;auto data=juce::var(new juce::DynamicObject());data.getDynamicObject()->setProperty("size",size);data.getDynamicObject()->setProperty("modified",modified);data.getDynamicObject()->setProperty("lufs",result);auto file=cacheFile(source);if(file.getParentDirectory().createDirectory().wasOk()){juce::TemporaryFile temp(file);if(temp.getFile().replaceWithText(juce::JSON::toString(data)))temp.overwriteTargetFileWithTemporary();}return result;}
};

// Stereo-linked peak limiter, zero latency, fixed -1 dBFS ceiling and 60 ms release.
// Gain is shared across channels, preserving the stereo image.
struct LoopPeakLimiter {
    double gain=1;float reduction=0;
    void reset(){gain=1;reduction=0;}
    void process(juce::AudioBuffer<float>& audio,double rate){double ceiling=std::pow(10.,-1./20),release=1-std::exp(-1/(juce::jmax(1.,rate)*.06));float maximum=0;
        for(int n=0;n<audio.getNumSamples();++n){double peak=0;for(int ch=0;ch<audio.getNumChannels();++ch)peak=juce::jmax(peak,(double)std::abs(audio.getSample(ch,n)));double desired=peak>ceiling?ceiling/peak:1.;gain=desired<gain?desired:juce::jmin(desired,gain+(1-gain)*release);maximum=juce::jmax(maximum,(float)(1-gain));for(int ch=0;ch<audio.getNumChannels();++ch)audio.setSample(ch,n,audio.getSample(ch,n)*(float)gain);}
        reduction=juce::jmax(maximum,reduction*(float)std::exp(-audio.getNumSamples()/(juce::jmax(1.,rate)*.15)));
    }
};
