#pragma once
#include <juce_dsp/juce_dsp.h>
#include <samplerate.h>

namespace track_analysis {
// Sorted sliding windows avoid allocating a tree node for every spectrogram bin.
class Median {
    std::vector<float> sorted;
public:
    explicit Median(std::vector<float> values):sorted(std::move(values)){std::sort(sorted.begin(),sorted.end());}
    float value()const{return sorted[sorted.size()/2];}
    void advance(float removed,float added){auto old=std::lower_bound(sorted.begin(),sorted.end(),removed);if(old==sorted.end())old=sorted.end()-1;auto index=old-sorted.begin();std::move(sorted.begin()+index+1,sorted.end(),sorted.begin()+index);auto next=std::lower_bound(sorted.begin(),sorted.end()-1,added);std::move_backward(next,sorted.end()-1,sorted.end());*next=added;}
};
inline int reflect(int i,int size){while(i<0||i>=size)i=i<0?-i-1:2*size-i-1;return i;}
inline float interpolate(const std::vector<double>& values,double i){int a=(int)i;if(a<0||a+1>=(int)values.size())return 0;return (float)(values[a]+(i-a)*(values[a+1]-values[a]));}

inline TrackGrid analyse(const AudioData& audio,double low,double high,const std::function<bool()>& cancel,bool constant=false,int sourceBegin=0,const std::function<void(float)>& progress={}){
    TrackGrid result;
    constexpr int fftSize=1024,bins=513,hop=110;
    constexpr double analysisRate=11025;
    const int sourceFrames=audio.buffer.getNumSamples()-sourceBegin,channels=juce::jmin(2,audio.buffer.getNumChannels());
    if(sourceFrames<32||channels<1)return result;
    std::vector<float> mono(sourceFrames);
    for(int n=0;n<sourceFrames;++n){for(int ch=0;ch<channels;++ch)mono[n]+=audio.buffer.getSample(ch,n+sourceBegin)/channels;if((n%65536)==0&&cancel&&cancel())return result;}
    if(std::abs(audio.rate-analysisRate)>.1){
        std::vector<float> resampled((size_t)std::ceil(sourceFrames*analysisRate/audio.rate)+64);
        int error=0;std::unique_ptr<SRC_STATE,decltype(&src_delete)> converter(src_new(SRC_SINC_FASTEST,1,&error),src_delete);if(!converter||error)return result;
        long consumed=0,produced=0;while(consumed<sourceFrames){if(cancel&&cancel())return result;SRC_DATA data{};data.data_in=mono.data()+consumed;data.input_frames=juce::jmin(16384L,(long)sourceFrames-consumed);data.data_out=resampled.data()+produced;data.output_frames=(long)resampled.size()-produced;data.src_ratio=analysisRate/audio.rate;data.end_of_input=consumed+data.input_frames==sourceFrames;if(src_process(converter.get(),&data)||(!data.input_frames_used&&!data.output_frames_gen))return result;consumed+=data.input_frames_used;produced+=data.output_frames_gen;}
        resampled.resize((size_t)produced);mono=std::move(resampled);
    }
    if(cancel&&cancel())return result;
    if(progress)progress(.08f);
    const int frames=1+(int)std::ceil((double)mono.size()/hop);
    if(frames<100)return result;
    constexpr double fps=analysisRate/hop;
    std::vector<float> magnitude((size_t)frames*bins),percussive(magnitude.size());
    juce::dsp::FFT fft(10);std::array<float,fftSize*2> buffer{};std::array<float,fftSize> window{};
    for(int n=0;n<fftSize;++n)window[n]=.5f-.5f*std::cos(2*juce::MathConstants<double>::pi*n/fftSize);
    for(int f=0;f<frames;++f){if((f%512)==0&&cancel&&cancel())return result;
        if((f%128)==0){if(cancel&&cancel())return result;if(progress)progress(.08f+.3f*f/frames);}
        buffer.fill(0);
        for(int n=0;n<fftSize;++n){int sample=f*hop+n-fftSize/2;if(juce::isPositiveAndBelow(sample,(int)mono.size()))buffer[n]=mono[sample]*window[n];}
        fft.performRealOnlyForwardTransform(buffer.data());
        for(int k=0;k<bins;++k)magnitude[(size_t)f*bins+k]=std::hypot(buffer[k*2],buffer[k*2+1])/512.f;
        auto* row=magnitude.data()+(size_t)f*bins;std::vector<float> initial;
        for(int k=-8;k<=8;++k)initial.push_back(row[reflect(k,bins)]);
        Median frequency(std::move(initial));
        for(int k=0;k<bins;++k){percussive[(size_t)f*bins+k]=frequency.value();if(k+1<bins)frequency.advance(row[reflect(k-8,bins)],row[reflect(k+9,bins)]);}
    }
    mono.clear();mono.shrink_to_fit();
    std::array<std::vector<double>,4> bands;for(auto& band:bands)band.resize(frames);
    std::vector<std::array<double,12>> chroma(frames);
    std::vector<double> prefix(frames+1);
    const double edges[]{40,180,600,2000,5500};std::array<int,4> counts{};
    for(int k=0;k<bins;++k){
        if(cancel&&cancel())return result;if(progress)progress(.38f+.34f*k/bins);
        double frequency=k*analysisRate/fftSize;int band=-1;
        for(int b=0;b<4;++b)if(frequency>=edges[b]&&frequency<edges[b+1]){band=b;++counts[b];}
        int pitch=frequency>=70&&frequency<3000?((int)std::round(69+12*std::log2(frequency/440))+1200)%12:-1;
        std::vector<float> initial;for(int f=-15;f<=15;++f)initial.push_back(magnitude[(size_t)reflect(f,frames)*bins+k]);
        Median harmonic(std::move(initial));prefix[0]=0;
        for(int f=0;f<frames;++f)prefix[f+1]=prefix[f]+magnitude[(size_t)f*bins+k];
        double previous=0;
        for(int f=0;f<frames;++f){if((f%512)==0&&cancel&&cancel())return result;
            float h=harmonic.value(),p=percussive[(size_t)f*bins+k],m=magnitude[(size_t)f*bins+k];
            // Reflect-padding for the one-second whitening average matches the spectral median boundaries.
            int lo=f-50,hi=f+50;double total=prefix[juce::jmin(frames,hi+1)]-prefix[juce::jmax(0,lo)];
            if(lo<0)total+=prefix[-lo]-prefix[0];if(hi>=frames)total+=prefix[frames]-prefix[2*frames-hi-1];
            double scale=total/101+.0001,percussion=m*p*p/(p*p+h*h+1e-12);
            double whitened=std::log1p(percussion/scale*10);
            if(band>=0&&f>0)bands[band][f]+=juce::jmax(0.,whitened-previous);
            previous=whitened;if(pitch>=0)chroma[f][pitch]+=h;
            if(f+1<frames)harmonic.advance(magnitude[(size_t)reflect(f-15,frames)*bins+k],magnitude[(size_t)reflect(f+16,frames)*bins+k]);
        }
    }
    magnitude.clear();magnitude.shrink_to_fit();percussive.clear();percussive.shrink_to_fit();
    for(int b=0;b<4;++b){
        auto raw=bands[b];for(auto& value:raw)value/=juce::jmax(1,counts[b]);
        std::vector<float> initial;for(int f=-50;f<=50;++f)initial.push_back((float)raw[reflect(f,frames)]);
        Median baseline(std::move(initial));
        for(int f=0;f<frames;++f){if((f%512)==0&&cancel&&cancel())return result;bands[b][f]=juce::jmax(0.,raw[f]-baseline.value());if(f+1<frames)baseline.advance((float)raw[reflect(f-50,frames)],(float)raw[reflect(f+51,frames)]);}
        raw=bands[b];prefix[0]=0;for(int f=0;f<frames;++f)prefix[f+1]=prefix[f]+raw[f];
        for(int f=0;f<frames;++f){if((f%512)==0&&cancel&&cancel())return result;int lo=f-400,hi=f+400;double total=prefix[juce::jmin(frames,hi+1)]-prefix[juce::jmax(0,lo)];if(lo<0)for(int j=lo;j<0;++j)total+=raw[reflect(j,frames)];if(hi>=frames)for(int j=frames;j<=hi;++j)total+=raw[reflect(j,frames)];bands[b][f]=juce::jmin(5.,raw[f]/(total/801+.01));}
    }
    if(progress)progress(.75f);
    std::vector<double> onset(frames);
    for(int f=0;f<frames;++f){if((f%512)==0&&cancel&&cancel())return result;onset[f]=.35*bands[0][f]+.2*bands[1][f]+.25*bands[2][f]+.2*bands[3][f];double norm=0;for(auto v:chroma[f])norm+=v*v;norm=std::sqrt(norm);for(auto& v:chroma[f])v/=juce::jmax(1e-9,norm);}
    // Several autocorrelation lags avoid quantising the tempo to a single analysis-frame lag.
    int order=1;while((1<<order)<frames*2)++order;int fftLength=1<<order;
    std::vector<float> correlationData((size_t)fftLength*2);for(int f=0;f<frames;++f)correlationData[f]=(float)onset[f];
    juce::dsp::FFT correlationFft(order);correlationFft.performRealOnlyForwardTransform(correlationData.data());
    for(int k=0;k<=fftLength/2;++k){double re=correlationData[k*2],im=correlationData[k*2+1];correlationData[k*2]=(float)(re*re+im*im);correlationData[k*2+1]=0;}
    correlationFft.performRealOnlyInverseTransform(correlationData.data());
    std::vector<double> correlation(frames);for(int f=0;f<frames;++f)correlation[f]=correlationData[f]/(frames-f);
    double best=-1,bpm=120;
    for(double candidate=juce::jmax(30.,low);candidate<=juce::jmin(300.,high);candidate+=.025){double score=0;for(int multiple:{1,2,3,4,6,8})score+=interpolate(correlation,fps*60/candidate*multiple)/std::sqrt((double)multiple);if(score>best){best=score;bpm=candidate;}}
    if(cancel&&cancel())return result;
    if(progress)progress(.84f);
    const double period=fps*60/bpm;std::vector<double> cumulative(frames);std::vector<int> back(frames,-1);
    int shortest=juce::jmax(1,(int)(period*.85)),longest=(int)(period*1.15);std::vector<double> penalty(longest+1);
    for(int interval=shortest;interval<=longest;++interval)penalty[interval]=100*std::pow(std::log(interval/period),2);
    for(int f=0;f<frames;++f){if((f%512)==0&&cancel&&cancel())return result;if((f%2048)==0&&cancel&&cancel())return result;double score=-std::numeric_limits<double>::infinity();for(int interval=shortest;interval<=juce::jmin(f,longest);++interval){double value=cumulative[f-interval]-penalty[interval];if(value>score){score=value;back[f]=f-interval;}}cumulative[f]=onset[f]+(back[f]<0?0:score);}
    int ending=juce::jmax(0,frames-(int)(period*1.2));for(int f=ending;f<frames;++f)if(cumulative[f]>cumulative[ending])ending=f;
    std::vector<int> path;for(int f=ending;f>=0;f=back[f])path.push_back(f);std::reverse(path.begin(),path.end());if(path.size()<4)return result;
    std::vector<double> beats;std::vector<std::array<double,12>> harmony;
    for(int f:path){
        int j=juce::jmax(0,f-1);double level=onset[f]*.25;
        while(j>juce::jmax(0,f-6)&&onset[j]>level)--j;
        double fraction=juce::jlimit(0.,1.,(level-onset[j])/juce::jmax(1e-9,onset[juce::jmin(j+1,frames-1)]-onset[j]));
        beats.push_back((j+fraction)/fps);std::array<double,12> average{};int count=0;
        for(int t=f+5;t<juce::jmin(frames,f+35);++t){for(int k=0;k<12;++k)average[k]+=chroma[t][k];++count;}
        if(!count)average=chroma[f];double norm=0;for(auto value:average)norm+=value*value;norm=std::sqrt(norm);for(auto& v:average)v/=juce::jmax(1e-9,norm);harmony.push_back(average);
    }
    // Distinguish bar 1 from bar 3 using harmonic changes, rather than kick loudness alone.
    std::vector<double> novelty(path.size());double meanNovelty=0;
    for(size_t i=2;i<path.size();++i){double dot=0;for(int k=0;k<12;++k)dot+=harmony[i][k]*harmony[i-2][k];novelty[i]=juce::jmax(0.,1-dot);meanNovelty+=novelty[i];}meanNovelty/=path.size();
    int phase=0;double bestBar=-1e30;
    for(int candidate=0;candidate<4;++candidate){double kick=0,snareBack=0,snareOther=0,change=0;int nk=0,nb=0,no=0;
        for(size_t i=8;i+8<path.size();++i){int p=(int)i-candidate;if((p%4+4)%4==0){kick+=bands[0][path[i]];change+=novelty[i];++nk;}double snare=bands[2][path[i]]+bands[3][path[i]];if((p%2+2)%2==1){snareBack+=snare;++nb;}else{snareOther+=snare;++no;}}
        double score=kick/juce::jmax(1,nk)+.3*snareBack/juce::jmax(1,nb)-.3*snareOther/juce::jmax(1,no)+change/juce::jmax(1,nk)/juce::jmax(.01,meanNovelty);
        auto& features=result.phaseFeatures[candidate];features[0]=kick/juce::jmax(1,nk);features[1]=snareBack/juce::jmax(1,nb)-snareOther/juce::jmax(1,no);features[2]=change/juce::jmax(1,nk)/juce::jmax(.01,meanNovelty);for(int span:{4,8,16}){double changeAll=0,changePhase=0;int count=0;for(size_t i=span;i+8<path.size();++i){double dot=0;for(int k=0;k<12;++k)dot+=harmony[i][k]*harmony[i-span][k];double value=juce::jmax(0.,1-dot);changeAll+=value;if(((int)i-candidate+4)%4==0){changePhase+=value;++count;}}features[span==4?3:span==8?4:5]=changePhase/juce::jmax(1,count)/juce::jmax(.01,changeAll/(path.size()-span-8));}features[6]=score;
        if(score>bestBar){bestBar=score;phase=candidate;}
    }
    result.anchors.clear();
    for(int i=0;i<(int)beats.size();++i){double sumX=0,sumY=0,sumXX=0,sumXY=0;int count=0;for(int j=juce::jmax(0,i-2);j<=juce::jmin((int)beats.size()-1,i+2);++j){double x=j-i;sumX+=x;sumY+=beats[j];sumXX+=x*x;sumXY+=x*beats[j];++count;}double slope=(count*sumXY-sumX*sumY)/juce::jmax(1e-9,count*sumXX-sumX*sumX);double time=(sumY-slope*sumX)/count;double sample=juce::jlimit(0.,(double)sourceFrames-1,time*audio.rate);if(result.anchors.empty()||sample>result.anchors.back().sample+1)result.anchors.push_back({sample,(double)i-phase});}
    if(result.anchors.size()<2){result.anchors={{0,0}};return result;}
    result.bpm=60*(result.anchors.back().beat-result.anchors.front().beat)*audio.rate/(result.anchors.back().sample-result.anchors.front().sample);
    result.confidence=juce::jlimit(0.,1.,best/juce::jmax(1e-9,correlation[0]*3.8));
    if(constant){double fitted=bpm,phaseTime=0,score=-1;auto search=[&](double centre,double radius,double step){for(double candidate=juce::jmax(low,centre-radius);candidate<=juce::jmin(high,centre+radius);candidate+=step){if(cancel&&cancel())return false;std::array<double,96> phases{};double period=fps*60/candidate;for(int f=0;f<frames;++f){double phase=std::fmod(f/period,1.)*96;int bin=(int)phase;double t=phase-bin;phases[bin%96]+=onset[f]*(1-t);phases[(bin+1)%96]+=onset[f]*t;}for(int bin=0;bin<96;++bin){double value=phases[bin]+.5*(phases[(bin+95)%96]+phases[(bin+1)%96]);if(value>score){score=value;fitted=candidate;phaseTime=bin/96.*60/candidate;}}}return true;};if(!search(bpm,.8,.01)||!search(fitted,.015,.0005))return result;
      double reference=result.sampleAt(0,audio.rate),spb=audio.rate*60/fitted,phaseSample=phaseTime*audio.rate;double first=phaseSample+std::round((reference-phaseSample)/spb)*spb;result.bpm=fitted;result.captureAnalysis();result.straight=true;result.straightAnchor={first,0};result.rebuild();}
    if(sourceBegin){for(auto& a:result.anchors)a.sample+=sourceBegin;for(auto& a:result.analysed)a.sample+=sourceBegin;result.straightAnchor.sample+=sourceBegin;}if(progress)progress(1);return result;
}
}
