#include "Loudness.h"
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <memory>
#include <vector>
#include "Tempo.h"
#include "Library.h"
#include <atomic>
#include "PreparationState.h"
#include <set>
#include <mutex>
#include <limits>
#include <regex>
#include <map>

class AudioExportWorker;class FileTrashWorker;
struct Envelope { bool enabled=false;double attack=0,hold=0,decay=150; };
struct Slice { int start = 0, end = 0, note = 36;bool excludeRhythm = false;Envelope envelope;int category=-1;bool muted=false;double gainDb=0;int replacement=-1; };
struct AudioData;
struct SfzRegion {std::shared_ptr<const AudioData> source;int low=0,high=127,lovel=0,hivel=127,root=60,start=0,end=-1,loopStart=0,loopEnd=-1,loopMode=0;double tune=0,gain=1,pan=0;};
struct AudioData {
    std::vector<SfzRegion> sfzRegions;juce::String sfzWarning;juce::ADSR::Parameters sfzEnvelope{.005f,.1f,.8f,.15f};
    juce::AudioBuffer<float> buffer;
    double rate = 44100.0;float peakAmplitude=0;double integratedLufs=-100;
    juce::String path,contentHash;bool analysisCacheHit=false;LoopTiming analysedTiming;double analysedPreferred=0,analysedMeter=0;
    struct TimingMemo { std::mutex mutex;std::map<juce::String,LoopTiming> values; };mutable std::shared_ptr<TimingMemo> timingMemo=std::make_shared<TimingMemo>();
    LoopTiming timingFor(double preferred,double meter)const{
        if(std::abs(analysedPreferred-preferred)<.001&&std::abs(analysedMeter-meter)<.001)return analysedTiming;
        auto key=path+":"+juce::String(buffer.getNumSamples())+":"+juce::String(rate,6)+":"+juce::String(preferred,6)+":"+juce::String(meter,6);
        {std::lock_guard<std::mutex> guard(timingMemo->mutex);auto found=timingMemo->values.find(key);if(found!=timingMemo->values.end())return found->second;}
        auto timing=estimateLoopTiming(buffer,rate,juce::File(path).getFileNameWithoutExtension(),metadata,preferred,meter);
        {std::lock_guard<std::mutex> guard(timingMemo->mutex);if(timingMemo->values.size()>=8)timingMemo->values.clear();timingMemo->values[key]=timing;}return timing;
    }
    juce::StringPairArray metadata;
    std::vector<float> peaks;
    std::vector<int> transients;
    struct Extrema{float low=0,high=0;};std::vector<std::vector<Extrema>> waveformLevels;static constexpr int waveformStride=32;
    void buildWaveformPeaks(){peakAmplitude=0;int length=buffer.getNumSamples();waveformLevels.clear();waveformLevels.emplace_back((length+waveformStride-1)/waveformStride);for(int n=0;n<length;++n)for(int ch=0;ch<buffer.getNumChannels();++ch){auto& e=waveformLevels[0][n/waveformStride];float sample=buffer.getSample(ch,n);peakAmplitude=juce::jmax(peakAmplitude,std::abs(sample));e.low=juce::jmin(e.low,sample);e.high=juce::jmax(e.high,sample);}while(waveformLevels.back().size()>1){auto& last=waveformLevels.back();std::vector<Extrema> next((last.size()+1)/2);for(size_t i=0;i<next.size();++i){next[i]=last[i*2];if(i*2+1<last.size()){next[i].low=juce::jmin(next[i].low,last[i*2+1].low);next[i].high=juce::jmax(next[i].high,last[i*2+1].high);}}waveformLevels.push_back(std::move(next));}}
    Extrema waveformRange(int start,int end)const{start=juce::jlimit(0,buffer.getNumSamples(),start);end=juce::jlimit(start,buffer.getNumSamples(),end);Extrema result;auto raw=[&](int a,int b){for(int n=a;n<b;++n)for(int ch=0;ch<buffer.getNumChannels();++ch){float v=buffer.getSample(ch,n);result.low=juce::jmin(result.low,v);result.high=juce::jmax(result.high,v);}};if(waveformLevels.empty()){raw(start,end);return result;}int first=juce::jmin(end,((start+waveformStride-1)/waveformStride)*waveformStride),last=juce::jmax(first,end/waveformStride*waveformStride);raw(start,first);raw(last,end);int bin=first/waveformStride,limit=last/waveformStride;while(bin<limit){int level=0;while(level+1<(int)waveformLevels.size()&&bin%(1<<(level+1))==0&&bin+(1<<(level+1))<=limit)++level;auto e=waveformLevels[level][bin>>level];result.low=juce::jmin(result.low,e.low);result.high=juce::jmax(result.high,e.high);bin+=1<<level;}return result;}
    static constexpr int peakStride = 128;
};

class AnalysisStore{
public:
 static juce::File fileFor(const juce::File& source){auto path=source.getFullPathName();return LibraryStore::root().getChildFile("analysis-v1").getChildFile(juce::SHA256(path.toRawUTF8(),path.getNumBytesAsUTF8()).toHexString()+".bin");}
 static bool header(juce::InputStream& in,const juce::File& source){return in.readInt()==0x53474131&&in.readInt64()==source.getSize()&&in.readInt64()==source.getLastModificationTime().toMilliseconds();}
 static bool exists(const juce::File& source){auto in=fileFor(source).createInputStream();return in&&header(*in,source);}
 static bool load(const juce::File& source,AudioData& data){auto in=fileFor(source).createInputStream();if(!in||!header(*in,source)||in->readInt()!=data.buffer.getNumSamples()||in->readInt()!=data.buffer.getNumChannels()||in->readDouble()!=data.rate)return false;
  data.contentHash=in->readString();data.peakAmplitude=in->readFloat();data.analysedPreferred=in->readDouble();data.analysedMeter=in->readDouble();data.analysedTiming.bpm=in->readDouble();data.analysedTiming.beats=in->readDouble();data.analysedTiming.bars=in->readDouble();data.analysedTiming.method=in->readString();int n=in->readInt();if(n!=(data.buffer.getNumSamples()+127)/128)return false;data.peaks.resize(n);for(auto& v:data.peaks)v=in->readFloat();n=in->readInt();if(n<0||n>data.buffer.getNumSamples())return false;data.transients.resize(n);for(auto& v:data.transients){v=in->readInt();if(v<0||v>data.buffer.getNumSamples())return false;}n=in->readInt();if(n<1||n>32)return false;data.waveformLevels.clear();int expected=(data.buffer.getNumSamples()+31)/32;for(int level=0;level<n;++level){int count=in->readInt();if(count!=expected)return false;auto& values=data.waveformLevels.emplace_back(count);for(auto& v:values){v.low=in->readFloat();v.high=in->readFloat();}expected=(expected+1)/2;}if(in->readInt()!=0x454e4421||data.contentHash.length()!=64)return false;data.analysisCacheHit=true;return true;
 }
 static void save(const juce::File& source,const AudioData& data){auto file=fileFor(source);if(!file.getParentDirectory().createDirectory().wasOk())return;juce::TemporaryFile tmp(file);{juce::FileOutputStream out(tmp.getFile());if(!out.openedOk())return;out.writeInt(0x53474131);out.writeInt64(source.getSize());out.writeInt64(source.getLastModificationTime().toMilliseconds());out.writeInt(data.buffer.getNumSamples());out.writeInt(data.buffer.getNumChannels());out.writeDouble(data.rate);out.writeString(data.contentHash);out.writeFloat(data.peakAmplitude);out.writeDouble(data.analysedPreferred);out.writeDouble(data.analysedMeter);out.writeDouble(data.analysedTiming.bpm);out.writeDouble(data.analysedTiming.beats);out.writeDouble(data.analysedTiming.bars);out.writeString(data.analysedTiming.method);out.writeInt((int)data.peaks.size());for(auto v:data.peaks)out.writeFloat(v);out.writeInt((int)data.transients.size());for(auto v:data.transients)out.writeInt(v);out.writeInt((int)data.waveformLevels.size());for(auto& level:data.waveformLevels){out.writeInt((int)level.size());for(auto v:level){out.writeFloat(v.low);out.writeFloat(v.high);}}out.writeInt(0x454e4421);out.flush();if(out.getStatus().failed())return;}tmp.overwriteTargetFileWithTemporary();}
};

#include "Warp.h"
#include "Stretch.h"
#include "Track.h"

class Processor final : public juce::AudioProcessor, private juce::Timer {
public:
    explicit Processor(bool backgroundServices=true) : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
        trackExportSafeCPU=(bool)LibraryStore::read("settings.json").getProperty("trackExportSafeCPU",false);trackExportHQ=(bool)LibraryStore::read("settings.json").getProperty("trackExportHQ",false);trackStemHQ=(bool)LibraryStore::read("settings.json").getProperty("trackStemHQ",false);formats.registerBasicFormats();last.fill(-1);slices.reserve(256);loadPreferences();padNames=defaultNames;syncLinkedSwing();
        if (backgroundServices&&juce::MessageManager::getInstanceWithoutCreating()) startTimer(200);
    }
    std::shared_ptr<AudioExportWorker> folderExportWorker,dragExportWorker;std::shared_ptr<FileTrashWorker> trashWorker;
    ~Processor() override { stopTimer();autoSavePool.removeAllJobs(false,-1);stretchWorker.signalThreadShouldExit();stretchWorker.stopThread(-1); }
    bool trackExportSafeCPU=false;bool trackExportHQ=false;bool trackStemHQ=false;bool trackStemsEnabled=true;std::array<double,4> trackStemGainDb{};std::array<bool,4> trackStemMute{},trackStemSolo{};std::array<int,4> trackStemRoute{};std::atomic<bool> trackPlaybackAllowed{true};std::function<void()> claimTrack;
    std::array<float,4> trackStemGains()const{std::array<float,4> values{};bool any=std::any_of(trackStemSolo.begin(),trackStemSolo.end(),[](bool x){return x;});for(int i=0;i<4;++i)values[i]=!trackStemMute[i]&&(!any||trackStemSolo[i])?juce::Decibels::decibelsToGain((float)trackStemGainDb[i]):0;return values;}
    std::atomic<bool> trackMode{false};TrackAnalyser trackAnalyser;TrackPlayer trackPlayer;TrackGrid trackGrid;std::shared_ptr<const AudioData> trackPreparedAudio;bool trackOffline=false;const AudioData* straightAnalysisAudio=nullptr;bool straightAnalysisFailed=false;bool straightAnalysisPending=false;double straightAnalysisSample=std::numeric_limits<double>::quiet_NaN(),straightAnalysisBeat=0;bool trackGridEdited=false,trackGridReady=false,trackLoop=false;int trackJumpBars=4;double trackLoopStart=0,trackLoopEnd=16;
    bool trackExportRaw=false,trackVinyl=false;
    void serviceTrack(){const juce::ScopedTryLock g(lock);if(!g.isLocked()||trackOffline||!trackMode||!audio)return;if(straightAnalysisPending&&straightAnalysisAudio!=audio.get()){straightAnalysisPending=false;straightAnalysisFailed=false;}if(straightAnalysisPending)if(auto grid=trackAnalyser.result(audio)){if(grid->straight){auto fitted=trackGrid;fitted.captureAnalysis();double anchor=std::isfinite(straightAnalysisSample)?straightAnalysisSample:trackGrid.sampleAt(0,audio->rate);fitted.bpm=grid->bpm;fitted.straight=true;fitted.straightAnchor={anchor-fitted.sampleOffset-fitted.downbeatOffset,std::isfinite(straightAnalysisSample)?straightAnalysisBeat:0};fitted.rebuild();trackGrid=fitted;straightAnalysisPending=false;trackGridEdited=trackGridReady=true;sourceBpm=fitted.bpm;edited(false);captureTrackCorrection("constant-tempo-analysis");}else{straightAnalysisPending=false;straightAnalysisFailed=true;}}if(trackPreparedAudio!=audio){straightAnalysisFailed=false;trackPreparedAudio=audio;trackAnalyser.request(audio,bpmRangeMin,bpmRangeMax);}if(!trackGridEdited&&!trackGridReady)if(auto grid=trackAnalyser.result(audio)){double sample=trackGrid.sampleAt(trackPlayer.audioIdentity.load()==audio.get()?trackPlayer.position.load():trackPlayer.hostPhase.load(),audio->rate);double heldOffset=trackGrid.sampleOffset;double priorCalibration=trackGrid.calibrationMs;trackGrid=*grid;trackGrid.autoChooseMode(audio->rate);if(priorCalibration>0||std::abs(heldOffset)>audio->rate*.0001)heldOffset+=(priorCalibration-trackGrid.calibrationMs)*audio->rate/1000.;trackGrid.setSampleOffset(heldOffset);double beat=trackPlayer.playing.load()?trackGrid.beatAt(sample,audio->rate):(trackLoop?trackLoopStart:0.);if(trackPlayer.playing.load()){double phase=trackPlayer.hostPhase.load();beat=std::round((beat-phase)/4)*4+phase;}trackPlayer.queueSeek(beat,true);trackGridReady=true;sourceBpm=trackGrid.bpm;timingMethod="Track beat analysis";}double tempo=trackPlayer.hostTempo.load();trackPlayer.publish(audio,trackGrid,trackLoop,trackLoopStart,trackLoopEnd,normaliseGain(),tempo,pitchSemitones+keyTranspose(),trackStemsEnabled&&trackPlaybackAllowed.load()&&trackGridReady,trackStemGains(),{},trackVinyl,trackStemHQ,trackJumpBars);}
    // Latest correction per source: immutable analysis and final edits, never uploaded.
    bool captureTrackCorrection(const juce::String& action,std::shared_ptr<const AudioData> expected={}){const juce::ScopedLock g(lock);if(!audio||!trackGridReady||(expected&&expected!=audio))return false;trackGrid.captureAnalysis();auto directory=LibraryStore::root().getChildFile("track-corrections-v1");auto identity=audio->contentHash;if(identity.isEmpty())identity=juce::SHA256(audio->path.toRawUTF8(),audio->path.getNumBytesAsUTF8()).toHexString();juce::XmlElement xml("TrackCorrection");xml.setAttribute("version",1);xml.setAttribute("detector","track-analysis-v3");xml.setAttribute("action",action);xml.setAttribute("path",audio->path);xml.setAttribute("hash",audio->contentHash);xml.setAttribute("rate",audio->rate);xml.setAttribute("frames",audio->buffer.getNumSamples());xml.setAttribute("bpm",trackGrid.bpm);xml.setAttribute("confidence",trackGrid.confidence);xml.setAttribute("downbeatOffset",trackGrid.downbeatOffset);xml.setAttribute("calibrationMs",trackGrid.calibrationMs);xml.setAttribute("sampleOffset",trackGrid.sampleOffset);xml.setAttribute("beatOffset",trackGrid.beatOffset);xml.setAttribute("updated",juce::Time::getCurrentTime().toISO8601(true));for(auto pair:{std::make_pair("analysis",&trackGrid.analysed),std::make_pair("manual",&trackGrid.manual)}){auto* layer=xml.createNewChildElement(pair.first);for(auto a:*pair.second){auto* node=layer->createNewChildElement("anchor");node->setAttribute("sample",a.sample);node->setAttribute("beat",a.beat);}}const juce::ScopedUnlock unlocked(lock);if(!directory.createDirectory().wasOk())return false;juce::TemporaryFile temporary(directory.getChildFile(identity+".xml"));return xml.writeTo(temporary.getFile())&&temporary.overwriteTargetFileWithTemporary();}
    void nudgeTrackGrid(double milliseconds){const juce::ScopedLock g(lock);if(!audio||!trackGridReady)return;trackGrid.captureAnalysis();double delta=juce::jlimit(-audio->rate-trackGrid.sampleOffset,audio->rate-trackGrid.sampleOffset,milliseconds*audio->rate/1000);trackGrid.sampleOffset+=delta;trackGrid.rebuild();trackChanged();captureTrackCorrection("grid-nudge");}
    void trackChanged(){straightAnalysisPending=false;trackGridEdited=trackGridReady=true;sourceBpm=trackGrid.bpm;edited(false);serviceTrack();}
    void setTrackBpm(double bpm){if(!std::isfinite(bpm))return;const juce::ScopedLock g(lock);double ratio=juce::jlimit(20.,400.,bpm)/trackGrid.bpm;for(auto& a:trackGrid.anchors)a.beat*=ratio;for(auto& a:trackGrid.analysed)a.beat*=ratio;for(auto& a:trackGrid.manual)a.beat*=ratio;for(auto& beat:trackGrid.removed)beat*=ratio;trackGrid.beatOffset*=ratio;trackGrid.straightAnchor.beat*=ratio;trackGrid.bpm*=ratio;trackChanged();captureTrackCorrection("tempo");}
    void trackDownbeat(double sample){const juce::ScopedLock g(lock);if(!audio||!trackGridReady||!std::isfinite(sample)||sample<0||sample>=audio->buffer.getNumSamples())return;double bar=std::round(trackGrid.beatAt(sample,audio->rate)/4.)*4;trackGrid.captureAnalysis();trackGrid.downbeatOffset+=sample-trackGrid.sampleAt(bar,audio->rate);trackGrid.rebuild();trackChanged();captureTrackCorrection("set-downbeat");}
    void analyseTrackStraight(double sample=std::numeric_limits<double>::quiet_NaN()){const juce::ScopedLock g(lock);if(!audio||!trackGridReady)return;straightAnalysisFailed=false;straightAnalysisSample=sample;straightAnalysisBeat=std::isfinite(sample)?std::round(trackGrid.beatAt(sample,audio->rate)/4)*4:0;straightAnalysisPending=true;straightAnalysisAudio=audio.get();trackPreparedAudio=audio;trackAnalyser.requestStraight(audio,bpmRangeMin,bpmRangeMax,std::isfinite(sample)&&audio->buffer.getNumSamples()-sample>audio->rate*8?juce::jlimit(0,audio->buffer.getNumSamples()-32,(int)sample):0);}
    void setTrackGridStraight(bool on){const juce::ScopedLock g(lock);if(!audio||!trackGridReady)return;if(on&&!trackGrid.straight){analyseTrackStraight();return;}straightAnalysisPending=false;trackGrid.setStraight(on,audio->rate);trackChanged();captureTrackCorrection(on?"straight":"dynamic");}
    void recalculateTrackFrom(double beat){const juce::ScopedLock g(lock);if(!audio||!trackGridReady)return;if(trackGrid.recalculateFrom(beat,audio->rate)){trackChanged();captureTrackCorrection("recalculate-from-anchor");}}
    void seekTrack(double beat){const juce::ScopedLock g(lock);if(trackLoop&&(beat<trackLoopStart||beat>=trackLoopEnd))trackLoop=false;trackPlayer.queueSeek(beat,true);serviceTrack();}
    void removeTrackAnchor(double beat){const juce::ScopedLock g(lock);trackGrid.remove(beat);trackChanged();captureTrackCorrection("remove-anchor");}
    bool trackAnchor(double sample,double beat){const juce::ScopedLock g(lock);if(!audio||!trackGridReady||sample<0||sample>=audio->buffer.getNumSamples()||!std::isfinite(beat)||!trackGrid.put(sample,beat,audio->rate))return false;trackChanged();return true;}
    void jumpTrack(int bars){const juce::ScopedLock g(lock);double beats=bars*4.;if(trackLoop&&audio){double first=trackGrid.beatAt(0,audio->rate),last=trackGrid.beatAt(audio->buffer.getNumSamples(),audio->rate),length=trackLoopEnd-trackLoopStart;double begin=trackLoopStart+beats;beats=begin-trackLoopStart;trackPlayer.queueSeek(trackPlayer.position.load()+beats,true);setTrackLoop(true,begin,begin+length);return;}trackPlayer.jump(beats);}
    void setTrackLoop(bool enabled,double begin,double end){const juce::ScopedLock g(lock);if(!audio||end<=begin)return;double first=trackGrid.beatAt(0,audio->rate),last=trackGrid.beatAt(audio->buffer.getNumSamples(),audio->rate);if(!std::isfinite(begin)||!std::isfinite(end))return;trackLoopStart=begin;trackLoopEnd=end;trackLoop=enabled;edited(false);serviceTrack();}
    bool oneShotMode=false;int rootOctave=3,shotOctaveShift=0;juce::ADSR::Parameters shotEnvelope{.005f,.1f,.8f,.15f};
    bool shotMono=false,shotLegato=true;std::array<uint64_t,128> shotHeldOrder{};std::array<float,128> shotHeldVelocity{};uint64_t shotHeldSerial=0;int shotMonoNote=-1;double shotVelocity=100;double shotNoteCycles=1;bool shotNoteLoop=false;bool shotLoop=false,shotZeroSnap=false;int shotStart=0,shotEnd=-1,shotLoopStart=0,shotLoopEnd=-1;double shotCrossfadeMs=0,shotLoopFade=0,shotFilterFade=0,shotLoopTravel=0;int shotFilterSlope=4;double shotFilterCutoff=20000;bool shotFilterAudition=false;
    int shotEndSample()const{return audio?juce::jlimit(shotStart+1,audio->buffer.getNumSamples(),shotEnd<0?audio->buffer.getNumSamples():shotEnd):0;}
    int shotLoopBeginSample()const{return juce::jlimit(shotStart,juce::jmax(shotStart,shotEndSample()-1),shotLoopStart);}
    int shotLoopEndSample()const{return juce::jlimit(shotLoopBeginSample()+1,shotEndSample(),shotLoopEnd<0?shotEndSample():shotLoopEnd);}
    int shotCrossfadeSamples()const{return audio?juce::jlimit(0,(shotLoopEndSample()-shotLoopBeginSample())/2,(int)std::round(shotCrossfadeMs*audio->rate/1000)):0;}
    int nearestZero(int sample,int lower=0,int upper=INT_MAX)const{if(!audio)return sample;int size=audio->buffer.getNumSamples(),best=juce::jlimit(0,size,sample),radius=(int)(audio->rate*.02);double score=1e20;auto* data=audio->buffer.getReadPointer(0);if(sample>=lower&&sample<=upper&&sample>=0&&sample<size&&std::abs(data[sample])<1e-7)return sample;for(int i=juce::jmax(1,lower,sample-radius);i<juce::jmin(size,upper==INT_MAX?size:upper+1,sample+radius+1);++i)if(data[i]==0||data[i-1]==0||(data[i]<0)!=(data[i-1]<0)){double amplitude=0;for(int ch=0;ch<audio->buffer.getNumChannels();++ch)amplitude+=std::abs(audio->buffer.getSample(ch,i));double cost=std::abs(i-sample)+amplitude*radius*.1;if(cost<score){score=cost;best=i;}}return best;}
    void setShotBoundary(int which,int sample,bool snap=true){const juce::ScopedLock g(lock);if(!audio)return;int length=audio->buffer.getNumSamples();if(which>=2)shotNoteLoop=false;sample=juce::jlimit(0,length,sample);if(snap&&shotZeroSnap)sample=nearestZero(sample,which==3?shotLoopBeginSample()+1:which==1?shotStart+1:0,which==2?shotLoopEndSample()-1:which==0?shotEndSample()-1:length);if(which==0)shotStart=juce::jlimit(0,shotEndSample()-1,sample);if(which==1)shotEnd=juce::jlimit(shotStart+1,length,sample);shotLoopStart=shotLoopBeginSample();shotLoopEnd=shotLoopEndSample();if(which==2)shotLoopStart=juce::jlimit(shotStart,shotLoopEnd-1,sample);if(which==3)shotLoopEnd=juce::jlimit(shotLoopStart+1,shotEndSample(),sample);edited(false);}
    void moveShotLoop(int begin,int length,bool snap=true){const juce::ScopedLock g(lock);if(!audio)return;shotNoteLoop=false;length=juce::jlimit(1,shotEndSample()-shotStart,length);begin=juce::jlimit(shotStart,shotEndSample()-length,begin);if(snap&&shotZeroSnap)begin=nearestZero(begin,shotStart,shotEndSample()-length);int end=juce::jlimit(begin+1,shotEndSample(),begin+length);if(snap&&shotZeroSnap)end=nearestZero(end,begin+1,shotEndSample());shotLoopStart=begin;shotLoopEnd=end;edited(false);}
    bool applyShotNoteLoop(){const juce::ScopedLock g(lock);if(!audio)return false;int note=audio->sfzRegions.empty()?(rootOctave+1)*12+loopKey:audio->sfzRegions[0].root;if(audio->sfzRegions.empty()&&loopKey<0)return false;double hz=440*std::pow(2.,(note-69)/12.);int length=juce::jmax(1,(int)std::round(2*audio->rate/hz*shotNoteCycles));if(length>shotEndSample()-shotStart)return false;int begin=juce::jlimit(shotStart,shotEndSample()-length,shotLoopBeginSample());if(shotZeroSnap)begin=nearestZero(begin,shotStart,shotEndSample()-length);int end=juce::jmin(shotEndSample(),begin+length);if(shotZeroSnap)end=nearestZero(end,begin+1,shotEndSample());shotLoopStart=begin;shotLoopEnd=end;shotLoop=true;shotNoteLoop=true;edited(false);return true;}
    bool scaleShotLoop(double factor){const juce::ScopedLock g(lock);if(!audio||!shotLoop)return false;if(shotNoteLoop){double old=shotNoteCycles;shotNoteCycles=juce::jlimit(1./64,64.,shotNoteCycles*factor);if(applyShotNoteLoop())return true;shotNoteCycles=old;return false;}int end=shotLoopBeginSample()+juce::jmax(1,(int)std::round((shotLoopEndSample()-shotLoopBeginSample())*factor));setShotBoundary(3,juce::jmin(end,shotEndSample()));return true;}
    struct ShotVoice{bool active=false,enteredLoop=false,choked=false,audition=false,loopMoved=false;double choke=1,age=0,filterCutoff=20000,filterMix=0;std::shared_ptr<const AudioData> source,owner;const SfzRegion* region=nullptr;bool envelopeChanged=false;bool held=true;int note=60;double pos=0,ratio=1,targetRatio=1,loopSeconds=0,gainRamp=0,lastEnvelope=0,envelopeOffset=0,fadeRamp=1,crossfadeRamp=0,travelSeconds=0,loopBegin=-1,loopEnd=0,baseBegin=-1,baseEnd=-1,oldPos=0,oldBegin=0,oldEnd=0;int moveLeft=0,moveTotal=1;std::array<double,4> slopeMix{0,0,0,1};std::array<std::array<double,4>,2> filterState{};float velocity=1,velocityRamp=1;juce::ADSR envelope;juce::ADSR::Parameters appliedEnvelope;};std::array<ShotVoice,32> shots;ShotVoice shotAuditionVoice;int shotCursor=0;
    void updateShotEnvelope(ShotVoice& v){auto e=shotEnvelope;if(e.sustain==0)e.decay=juce::jmax(.003f,e.decay);auto& old=v.appliedEnvelope;if(e.attack!=old.attack||e.decay!=old.decay||e.sustain!=old.sustain||e.release!=old.release){if(v.age>0)v.envelopeChanged=true;v.envelope.setParameters(e);old=e;}}
    void startShot(int note,float velocity,const SfzRegion* region=nullptr){if(!audio)return;auto& voice=shots[shotCursor++%shots.size()];voice.envelope.reset();voice.envelope.setSampleRate(outputRate);voice.appliedEnvelope={-1,-1,-1,-1};updateShotEnvelope(voice);voice.envelope.noteOn();voice.loopMoved=false;voice.loopBegin=-1;voice.baseBegin=-1;voice.travelSeconds=0;voice.moveLeft=0;voice.gainRamp=0;voice.lastEnvelope=0;voice.envelopeOffset=0;voice.envelopeChanged=false;voice.fadeRamp=1;voice.crossfadeRamp=shotCrossfadeMs;voice.slopeMix={};voice.slopeMix[shotFilterSlope-1]=1;voice.active=true;voice.audition=false;voice.filterCutoff=shotFilterCutoff;voice.filterMix=0;voice.choked=false;voice.choke=1;voice.age=0;voice.enteredLoop=false;voice.loopSeconds=0;voice.filterState={};voice.region=region;voice.owner=audio;voice.source=region?region->source:audio;voice.held=true;voice.note=note;voice.pos=region&&region!=&audio->sfzRegions[0]?region->start:shotStart;voice.velocity=(float)(1-shotVelocity/100+shotVelocity/100*velocity);voice.velocityRamp=voice.velocity;int root=region?region->root:12*(rootOctave+2)+(loopKey<0?0:loopKey);voice.targetRatio=voice.ratio=std::pow(2.,(note-root+pitchSemitones+12*shotOctaveShift+(region?region->tune:0))/12);}
    void triggerShot(int note,float velocity){if(!audio)return;if(shotMono)for(auto& v:shots)if(v.active)v.choked=true;if(audio->sfzRegions.empty())startShot(note,velocity);else for(auto& region:audio->sfzRegions)if(note>=region.low&&note<=region.high&&velocity*127>=region.lovel-.5&&velocity*127<=region.hivel+.5)startShot(note,velocity,&region);}
    void monoShot(int note,float velocity){ShotVoice* current=nullptr;for(auto& v:shots)if(v.active&&!v.choked&&v.held){current=&v;break;}if(shotLegato&&current){bool reusable=audio->sfzRegions.empty();if(!reusable){reusable=true;int count=0;for(auto& r:audio->sfzRegions)if(note>=r.low&&note<=r.high&&velocity*127>=r.lovel-.5&&velocity*127<=r.hivel+.5){bool found=false;for(auto& v:shots)if(v.active&&!v.choked&&v.region==&r)found=true;reusable&=found;++count;}int active=0;for(auto& v:shots)if(v.active&&!v.choked)++active;reusable&=count==active&&count>0;}if(reusable){for(auto& v:shots)if(v.active&&!v.choked){v.note=note;v.velocity=(float)(1-shotVelocity/100+shotVelocity/100*velocity);int root=v.region?v.region->root:12*(rootOctave+2)+(loopKey<0?0:loopKey);v.targetRatio=std::pow(2.,(note-root+pitchSemitones+12*shotOctaveShift+(v.region?v.region->tune:0))/12);}shotMonoNote=note;return;}auto envelope=current->envelope;double seconds=current->loopSeconds;triggerShot(note,velocity);for(auto& v:shots)if(v.active&&!v.choked){v.envelope=envelope;v.loopSeconds=seconds;}shotMonoNote=note;return;}triggerShot(note,velocity);shotMonoNote=note;}
    void shotNoteOn(int note,float velocity){shotFilterAudition=false;shotAuditionVoice.choked=true;shotHeldOrder[note]=++shotHeldSerial;shotHeldVelocity[note]=velocity;if(shotMono)monoShot(note,velocity);else triggerShot(note,velocity);}
    void shotNoteOff(int note){shotHeldOrder[note]=0;if(shotMono&&note==shotMonoNote){int next=-1;uint64_t latest=0;for(int i=0;i<128;++i)if(shotHeldOrder[i]>latest){latest=shotHeldOrder[i];next=i;}if(next>=0){monoShot(next,shotHeldVelocity[next]);return;}shotMonoNote=-1;}for(auto& v:shots)if(v.active&&v.note==note){v.held=false;v.envelope.noteOff();}}
    void renderShots(juce::AudioBuffer<float>& out,juce::MidiBuffer& midi){if(!audio){midi.clear();return;}if(!oneShotMode||!shotLoop)shotFilterAudition=false;
      if(shotFilterAudition&&(!shotAuditionVoice.active||shotAuditionVoice.choked||shotAuditionVoice.owner!=audio)){for(auto& voice:shots)if(voice.active)voice.choked=true;auto& v=shotAuditionVoice;v=ShotVoice{};v.active=v.audition=v.enteredLoop=true;v.filterMix=1;v.owner=v.source=audio;v.pos=shotLoopBeginSample();v.filterCutoff=shotFilterCutoff;v.ratio=std::pow(2.,(pitchSemitones+12*shotOctaveShift)/12);v.envelope.setSampleRate(outputRate);v.envelope.setParameters({0,0,1,0});v.envelope.noteOn();}
      if(!shotFilterAudition)shotAuditionVoice.choked=true;
      std::array<ShotVoice*,33> renderVoices{};for(size_t i=0;i<shots.size();++i)renderVoices[i]=&shots[i];renderVoices.back()=&shotAuditionVoice;
      auto event=midi.begin();double normalise=normaliseGain(),smooth=-std::expm1(-1./(outputRate*.002));shotAuditionVoice.targetRatio=std::pow(2.,(pitchSemitones+12*shotOctaveShift)/12);for(auto& v:shots)if(v.active){updateShotEnvelope(v);int root=v.region?v.region->root:12*(rootOctave+2)+(loopKey<0?0:loopKey);v.targetRatio=std::pow(2.,(v.note-root+pitchSemitones+12*shotOctaveShift+(v.region?v.region->tune:0))/12);}
      for(int n=0;n<out.getNumSamples();++n){currentNormaliseGain+=(normalise-currentNormaliseGain)*smooth;while(event!=midi.end()&&(*event).samplePosition<=n){auto msg=(*event).getMessage();if(msg.isNoteOn())shotNoteOn(msg.getNoteNumber(),msg.getFloatVelocity());if(msg.isNoteOff())shotNoteOff(msg.getNoteNumber());if(msg.isAllNotesOff()||msg.isAllSoundOff()){shotFilterAudition=false;shotAuditionVoice.choked=true;shotHeldOrder.fill(0);shotMonoNote=-1;for(auto& v:shots)if(v.active){v.held=false;v.envelope.noteOff();}}++event;}
        for(auto* voice:renderVoices){auto& v=*voice;if(!v.active||!v.source)continue;auto& data=*v.source;bool firstRegion=v.region&&!audio->sfzRegions.empty()&&v.region==&audio->sfzRegions[0];int size=data.buffer.getNumSamples(),end=v.region&&!firstRegion?v.region->end:shotEndSample(),begin=v.region&&!firstRegion?v.region->loopStart:shotLoopBeginSample(),loopEnd=v.region&&!firstRegion?v.region->loopEnd:shotLoopEndSample();bool looping=v.region?shotLoop&&(v.region->loopMode==1||(v.region->loopMode==2&&v.held)):shotLoop;v.ratio+=(v.targetRatio-v.ratio)*smooth;v.crossfadeRamp+=(shotCrossfadeMs-v.crossfadeRamp)*smooth;
          if(v.baseBegin!=begin||v.baseEnd!=loopEnd){v.travelSeconds=0;v.baseBegin=begin;v.baseEnd=loopEnd;}
          if(looping&&v.enteredLoop)v.travelSeconds=juce::jmin(juce::jmax(0.,(end-loopEnd)/data.rate),v.travelSeconds+shotLoopTravel/outputRate);
          int wantedBegin=begin+(int)(v.travelSeconds*data.rate),wantedEnd=loopEnd+(int)(v.travelSeconds*data.rate);
          if(v.loopBegin<0||!v.enteredLoop){v.loopBegin=wantedBegin;v.loopEnd=wantedEnd;}
          else if(looping&&!v.moveLeft&&(v.loopBegin!=wantedBegin||v.loopEnd!=wantedEnd)){v.loopMoved=true;v.oldPos=v.pos;v.oldBegin=v.loopBegin;v.oldEnd=v.loopEnd;double phase=std::fmod(std::fmod(v.pos-v.loopBegin,juce::jmax(1.,v.loopEnd-v.loopBegin))+juce::jmax(1.,v.loopEnd-v.loopBegin),juce::jmax(1.,v.loopEnd-v.loopBegin))/juce::jmax(1.,v.loopEnd-v.loopBegin);v.pos=wantedBegin+phase*(wantedEnd-wantedBegin);v.loopBegin=wantedBegin;v.loopEnd=wantedEnd;v.moveTotal=v.moveLeft=juce::jmax(1,(int)(outputRate*.02));}
          begin=(int)v.loopBegin;loopEnd=(int)v.loopEnd;int crossfade=looping?juce::jlimit(0,juce::jmax(0,(loopEnd-begin)/2),(int)std::round(juce::jmax(v.crossfadeRamp,v.loopMoved?1.:0.)*data.rate/1000)):0;
          if(looping&&v.pos>=begin)v.enteredLoop=true;if(looping&&v.pos>=loopEnd)v.pos=begin+std::fmod(v.pos-loopEnd,juce::jmax(1,loopEnd-begin));if(v.pos>=end){v.active=false;continue;}double fade=!v.audition&&looping&&v.enteredLoop&&shotLoopFade>0?std::exp2(-v.loopSeconds*shotLoopFade/10):1.;v.velocityRamp+=(float)juce::jlimit(-1/(outputRate*.005),1/(outputRate*.005),(double)v.velocity-v.velocityRamp);float envelope=v.envelope.getNextSample();if(v.envelopeChanged){v.envelopeOffset=v.lastEnvelope-envelope;v.envelopeChanged=false;}envelope=(float)juce::jlimit(0.,1.,envelope+v.envelopeOffset);v.lastEnvelope=envelope;v.envelopeOffset*=1-smooth;if(shotEnvelope.sustain==0&&envelope<.2f){float x=envelope/.2f;envelope=.2f*x*x*(2-x);}v.fadeRamp+=(fade-v.fadeRamp)*smooth;float gain=envelope*v.velocityRamp*(float)(currentNormaliseGain*v.fadeRamp*(v.region?v.region->gain:1));if(!looping)gain*=(float)juce::jlimit(0.,1.,(end-v.pos)/(data.rate*.003));if(v.choked)v.choke=juce::jmax(0.,v.choke-1/(outputRate*.005));gain*=(float)(v.choke*juce::jmin(1.,v.age/(outputRate*.001)));++v.age;double t=looping&&crossfade>0?juce::jlimit(0.,1.,(v.pos-(loopEnd-crossfade))/crossfade):0;
          auto read=[&](int ch,double pos){pos=juce::jlimit(0.,(double)size-1,pos);int a=(int)pos;auto* values=data.buffer.getReadPointer(juce::jmin(ch,data.buffer.getNumChannels()-1));return values[a]+(float)(pos-a)*(values[juce::jmin(a+1,size-1)]-values[a]);};
          // Warm the filter before loop entry; engage it with a five-millisecond smooth crossfade.
          bool filterOn=looping&&(v.audition||shotFilterFade>0);double filterWet=v.filterMix*v.filterMix*(3-2*v.filterMix);v.filterMix=juce::jlimit(0.,1.,v.filterMix+((filterOn&&v.enteredLoop)?1.:-1.)/(outputRate*.005));double filterCoefficient=0;if(filterOn||v.filterMix>0){double target=juce::jlimit(15.,outputRate*.45,shotFilterCutoff*(v.audition?1.:std::exp2(-v.loopSeconds*shotFilterFade/10)));v.filterCutoff+=(target-v.filterCutoff)*(-std::expm1(-1./(outputRate*.01)));filterCoefficient=-std::expm1(-2*juce::MathConstants<double>::pi*juce::jlimit(15.,outputRate*.45,v.filterCutoff)/outputRate);}auto loopRead=[&](int ch,double pos,double a,double b){double len=juce::jmax(1.,b-a);if(looping&&pos>=b)pos=a+std::fmod(pos-b,len);float value=read(ch,pos);int c=looping?juce::jmin(crossfade,(int)(len/2)):0;double t=c>0?juce::jlimit(0.,1.,(pos-(b-c))/c):0;if(t>0){double tail=b<size?read(ch,b):2*read(ch,b-1)-read(ch,b-2);double delta=read(ch,a)-tail;double slope=(read(ch,a+1)-read(ch,a))-(tail-read(ch,b-1));value+=(float)(delta*(3*t*t-2*t*t*t)+juce::jlimit(-1.,1.,slope*c)*(t*t*t-t*t));}return value;};
          for(int pole=0;pole<4;++pole)v.slopeMix[pole]+=((pole==shotFilterSlope-1?1.:0.)-v.slopeMix[pole])*smooth;
          for(int ch=0;ch<juce::jmin(2,out.getNumChannels());++ch){float value=loopRead(ch,v.pos,begin,loopEnd);if(v.moveLeft){double mix=1.-(double)v.moveLeft/v.moveTotal;mix=mix*mix*(3-2*mix);value=(float)(loopRead(ch,v.oldPos,v.oldBegin,v.oldEnd)*(1-mix)+value*mix);}
          // Cascaded one-poles: 6/12/18/24 dB, with a pitch-independent loop clock.
          if(filterCoefficient>0){float dry=value,filtered=0;for(int pole=0;pole<4;++pole){auto& state=v.filterState[ch][pole];state+=filterCoefficient*(value-state);value=(float)state;filtered+=(float)(state*v.slopeMix[pole]);}value=dry+(float)filterWet*(filtered-dry);}
          else for(auto& state:v.filterState[ch])state=value;
          double pan=v.region?v.region->pan:0;double balance=ch==0?juce::jmin(1.,1-pan):juce::jmin(1.,1+pan);out.addSample(ch,n,value*gain*(float)balance);}v.pos+=data.rate/outputRate*v.ratio;if(v.moveLeft){v.oldPos+=data.rate/outputRate*v.ratio;if(v.oldPos>=v.oldEnd)v.oldPos=v.oldBegin+std::fmod(v.oldPos-v.oldEnd,juce::jmax(1.,v.oldEnd-v.oldBegin));--v.moveLeft;}if(looping&&v.enteredLoop)v.loopSeconds+=1/outputRate;if((!v.envelope.isActive()&&std::abs(v.envelopeOffset)<1e-6)||v.choke<=0||v.fadeRamp<1e-7)v.active=false;}}
      midi.clear();}
    std::atomic<double> hostBeatPosition{0};bool stretchAnchorEnabled=false;double stretchAnchorSample=0;int stretchAnchorBar=1;
    double stretchAnchorOffset()const{if(!stretchAnchorEnabled||!audio||warpMode||oneShotMode||trackMode)return 0;return (stretchAnchorSample-startMarker)/audio->rate*sourceBpm/60-(stretchAnchorBar-1)*hostNumerator*4./hostDenominator;}
    void setStretchAnchor(double sample,bool snap=true){const juce::ScopedLock g(lock);if(!audio)return;if(snap){double step=audio->rate*60/sourceBpm*rhythmGrid;sample=gridOrigin()+std::round((sample-gridOrigin())/step)*step;}stretchAnchorSample=juce::jlimit((double)regionStart(),(double)regionEnd()-1,sample);stretchAnchorEnabled=true;edited(false);}
    StretchWorker stretchWorker;bool stretchMode=false,warpMode=false,warpTempoSync=false;double warpNativeBpm=120;int warpSelectedChunk=0;std::shared_ptr<const WarpArrangement> warpArrangement;bool keyLocked=false,keyAlternate=false;int loopKey=-1,targetKey=5;float stretchFade=0;std::shared_ptr<const StretchWorker::Result> playingStretch,previousStretch;float stretchBlend=1;
    int keyTranspose()const{if(!keyLocked||loopKey<0)return 0;int d=(targetKey-loopKey+12)%12;if(d>6)d-=12;if(d==6)d=-6;if(keyAlternate&&d!=0)d+=d<0?12:-12;return d;}
    void setLoopKey(int key){const juce::ScopedLock g(lock);loopKey=juce::jlimit(-1,11,key);keyAlternate=false;if(shotNoteLoop&&!applyShotNoteLoop())shotNoteLoop=false;edited(false);}
    void setTargetKey(int key){const juce::ScopedLock g(lock);targetKey=juce::jlimit(0,11,key);keyAlternate=false;edited(false);}
    void setKeyLocked(bool on){const juce::ScopedLock g(lock);keyLocked=on;keyAlternate=false;edited(false);}
    void alternateKeyTranspose(){const juce::ScopedLock g(lock);keyAlternate=!keyAlternate;edited(false);}
    double warpChunkBpm(int index=-1)const{if(!audio||!warpArrangement||warpArrangement->empty())return warpNativeBpm;if(index<0)index=warpSelectedChunk;index=juce::jlimit(0,(int)warpArrangement->size()-1,index);auto& chunk=(*warpArrangement)[index];return warpNativeBpm*(chunk.end()-chunk.begin())*60*audio->rate/juce::jmax(1,chunk.sourceEnd()-chunk.sourceBegin())/sourceBpm;}
    void setWarpChunkBpm(double value){const juce::ScopedLock g(lock);if(!warpArrangement||warpArrangement->empty())return;int index=juce::jlimit(0,(int)warpArrangement->size()-1,warpSelectedChunk);auto chunk=(*warpArrangement)[index];double ratio=juce::jlimit(20.,400.,std::isfinite(value)?value:120.)/juce::jmax(1.,warpChunkBpm(index)),begin=chunk.begin();for(auto& point:chunk.points)point.beat=begin+(point.beat-begin)*ratio;replaceWarp(index,std::move(chunk));}
    void setWarpTempoSync(bool enabled){const juce::ScopedLock g(lock);if(warpTempoSync==enabled)return;warpTempoSync=enabled;if(audio&&warpArrangement){double half=sourceBeatsPerBar*.5;auto defaultEnd=[&](double bpm){double beats=loopLengthLocked?lockedBars*sourceBeatsPerBar:juce::jmax(half,std::round(audio->buffer.getNumSamples()/audio->rate*bpm/60/half)*half);return (int)std::round(beats*60/bpm*audio->rate);};bool defaultRegion=trimStart==0&&std::abs(trimEnd-defaultEnd(sourceBpm))<=1;double next=enabled?warpNativeBpm:hasHost?hostBpm:120.;if(defaultRegion)trimEnd=defaultEnd(next);auto chunks=*warpArrangement;for(auto& chunk:chunks)for(auto& point:chunk.points)point.beat*=next/sourceBpm;sourceBpm=next;warpArrangement=std::make_shared<const WarpArrangement>(std::move(chunks));updateRegionTempo();}edited(false);}
    void setStretchMode(bool on){const juce::ScopedLock g(lock);trackMode=false;stretchMode=on;warpMode=false;if(on)playMode=true;clearVoices();stretchFade=0;edited(false);}
    StretchWorker::Request stretchRequest()const{return {audio,regionStart(),regionEnd(),startMarker,sourceBpm,hasHost?hostBpm:sourceBpm,pitchSemitones+keyTranspose(),warpMode?warpArrangement:nullptr,gridOrigin()};}
    void initialiseWarp(){if(!audio||warpArrangement)return;auto chunks=std::make_shared<WarpArrangement>();double spb=audio->rate*60/sourceBpm;WarpChunk chunk;chunk.points.push_back({0,-gridOrigin()/spb});size_t stride=juce::jmax((size_t)1,(audio->transients.size()+509)/510);auto warpTransients=detectedPoints(*audio,0,audio->buffer.getNumSamples(),70,sourceBpm,gridOrigin(),.25,{},false);stride=juce::jmax((size_t)1,(warpTransients.size()+509)/510);for(size_t i=0;i<warpTransients.size();i+=stride){int source=warpTransients[i];if(source>chunk.points.back().source+1&&source<audio->buffer.getNumSamples()-1)chunk.points.push_back({source,(source-gridOrigin())/spb});}chunk.points.push_back({audio->buffer.getNumSamples(),(audio->buffer.getNumSamples()-gridOrigin())/spb});chunks->push_back(std::move(chunk));warpArrangement=chunks;}
    void defaultWarpTiming(){if(!audio)return;warpNativeBpm=sourceBpm;warpSelectedChunk=0;sourceBpm=warpTempoSync?warpNativeBpm:hasHost?hostBpm:120.;independentGrid=regionTiming=true;gridStart=trimStart=0;double halfBar=sourceBeatsPerBar*.5,beats=audio->buffer.getNumSamples()/audio->rate*sourceBpm/60;double rounded=juce::jmax(halfBar,std::round(beats/halfBar)*halfBar);if(loopLengthLocked)rounded=lockedBars*sourceBeatsPerBar;trimEnd=(int)std::round(rounded*60/sourceBpm*audio->rate);updateRegionTempo();startAuto=true;updateStartAnchor();}
    int playbackMode()const{return trackMode?4:oneShotMode?3:warpMode?2:stretchMode?1:0;}
    void setPlaybackMode(int mode){const juce::ScopedLock g(lock);straightAnalysisPending=false;shotFilterAudition=false;shotAuditionVoice.active=false;trackMode=mode==4;oneShotMode=mode==3;warpMode=mode==2;stretchMode=mode==1||mode==2||mode==4;if(!trackMode)trackPlayer.clearSource();if(trackMode){if(claimTrack)claimTrack();trackGrid={};trackGrid.bpm=juce::jlimit(20.,400.,sourceBpm);trackGridEdited=trackGridReady=false;stretchAnchorEnabled=false;stretchAnchorSample=0;trackPreparedAudio.reset();trackLoop=false;serviceTrack();}for(auto& voice:shots)voice.active=false;if(oneShotMode)playMode=false;if(stretchMode)playMode=true;if(warpMode){if(!warpArrangement)defaultWarpTiming();initialiseWarp();}clearVoices();stretchFade=0;edited(false);}
    void setWarpChunks(WarpArrangement chunks){const juce::ScopedLock g(lock);if(!audio||chunks.size()>128)return;for(auto& chunk:chunks)if(!chunk.valid(audio->buffer.getNumSamples()))return;warpArrangement=std::make_shared<const WarpArrangement>(std::move(chunks));edited(false);}
    void stretchWarpRegion(const WarpArrangement& original,int begin,int oldEnd,int newEnd,int oldStart){const juce::ScopedLock g(lock);if(!audio||oldEnd<=begin||newEnd<=begin)return;double ratio=(double)(newEnd-begin)/(oldEnd-begin),anchor=(begin-gridOrigin())/(audio->rate*60/sourceBpm);auto chunks=original;for(auto& chunk:chunks)for(auto& point:chunk.points)point.beat=anchor+(point.beat-anchor)*ratio;setWarpChunks(std::move(chunks));setTrim(begin,newEnd);setStart((int)std::round(begin+(oldStart-begin)*ratio));}
    void resetWarp(){const juce::ScopedLock g(lock);warpArrangement.reset();initialiseWarp();edited(false);}
    bool cutWarp(int index,double beat){const juce::ScopedLock g(lock);if(!warpArrangement||!juce::isPositiveAndBelow(index,(int)warpArrangement->size())||warpArrangement->size()>=128)return false;auto chunks=*warpArrangement;auto old=chunks[index];int source=(int)std::round(old.sourceAt(beat));if(source<=old.sourceBegin()||source>=old.sourceEnd())return false;beat=old.beatAt(source);WarpChunk left,right;for(auto point:old.points){if(point.source<source)left.points.push_back(point);if(point.source>source)right.points.push_back(point);}left.points.push_back({source,beat});right.points.insert(right.points.begin(),{source,beat});chunks[index]=left;chunks.insert(chunks.begin()+index+1,right);setWarpChunks(std::move(chunks));return true;}
    int duplicateWarp(int index){const juce::ScopedLock g(lock);if(!warpArrangement||!juce::isPositiveAndBelow(index,(int)warpArrangement->size())||warpArrangement->size()>=128)return -1;auto chunks=*warpArrangement;chunks.push_back(chunks[index]);setWarpChunks(std::move(chunks));return (int)warpArrangement->size()-1;}
    void deleteWarp(int index){const juce::ScopedLock g(lock);if(!warpArrangement||!juce::isPositiveAndBelow(index,(int)warpArrangement->size()))return;auto chunks=*warpArrangement;chunks.erase(chunks.begin()+index);setWarpChunks(std::move(chunks));}
    void replaceWarp(int index,WarpChunk chunk){const juce::ScopedLock g(lock);if(!warpArrangement||!juce::isPositiveAndBelow(index,(int)warpArrangement->size()))return;auto chunks=*warpArrangement;auto old=chunks[index];bool fit=chunks.size()==1&&std::abs((regionEnd()-gridOrigin())/(audio->rate*60/sourceBpm)-old.end())<.002;chunks[index]=std::move(chunk);double end=chunks[index].end();setWarpChunks(std::move(chunks));if(fit){trimEnd=juce::jmax(trimStart+1,(int)std::round(gridOrigin()+end*audio->rate*60/sourceBpm));updateRegionTempo();}}
    int addWarpPoint(int index,double beat){const juce::ScopedLock g(lock);if(!warpArrangement||!juce::isPositiveAndBelow(index,(int)warpArrangement->size()))return -1;auto chunk=(*warpArrangement)[index];int source=(int)std::round(chunk.sourceAt(beat));if(source<=chunk.sourceBegin()||source>=chunk.sourceEnd()||chunk.points.size()>=1024)return -1;int i=1;while(i<(int)chunk.points.size()&&chunk.points[i].source<source)++i;if(std::abs(chunk.points[i].source-source)<2)return i;chunk.points.insert(chunk.points.begin()+i,{source,chunk.beatAt(source)});replaceWarp(index,std::move(chunk));return i;}
    void moveWarpPoint(int index,int point,double beat){const juce::ScopedLock g(lock);if(!warpArrangement||!juce::isPositiveAndBelow(index,(int)warpArrangement->size()))return;auto chunk=(*warpArrangement)[index];if(!juce::isPositiveAndBelow(point,(int)chunk.points.size()))return;if(point>0)beat=juce::jmax(beat,chunk.points[point-1].beat+.001);if(point+1<(int)chunk.points.size())beat=juce::jmin(beat,chunk.points[point+1].beat-.001);chunk.points[point].beat=beat;replaceWarp(index,std::move(chunk));}
    void removeWarpPoint(int index,int point){const juce::ScopedLock g(lock);if(!warpArrangement||!juce::isPositiveAndBelow(index,(int)warpArrangement->size()))return;auto chunk=(*warpArrangement)[index];if(point<=0||point>=(int)chunk.points.size()-1)return;chunk.points.erase(chunk.points.begin()+point);replaceWarp(index,std::move(chunk));}
    double snapWarpBeat(double beat,bool free=false)const{if(free||gridSnapMode==0)return beat;if(gridSnapMode==1)return std::round(beat/rhythmGrid)*rhythmGrid;double nearest=beat,distance=std::numeric_limits<double>::max();if(audio&&warpArrangement)for(auto& chunk:*warpArrangement)for(auto source:audio->transients)if(source>=chunk.sourceBegin()&&source<=chunk.sourceEnd()){double candidate=chunk.beatAt(source),d=std::abs(candidate-beat);if(d<distance){nearest=candidate;distance=d;}}return nearest;}
    juce::AudioFormatManager formats;
    juce::CriticalSection lock;
    std::shared_ptr<const AudioData> audio;
    std::vector<Slice> slices;
    std::array<int,128> last;
    juce::Random random;
    bool randomOther = false;
    bool normaliseEnabled=false,normaliseLufs=false;double targetLufs=-18,currentNormaliseGain=1;LoopPeakLimiter peakLimiter;std::atomic<float> limiting{0};std::atomic<bool> limiterActive{false};
    void finishAudio(juce::AudioBuffer<float>& out){if(isContentionEngine)return;if(limiterActive.load()){peakLimiter.process(out,outputRate);limiting.store(peakLimiter.reduction);}else{peakLimiter.reset();limiting=0;}}
    double normaliseGain()const{if(!normaliseEnabled||!audio)return 1.;if(normaliseLufs&&!oneShotMode)return audio->integratedLufs>-99?std::pow(10.,juce::jlimit(-60.,36.,targetLufs-audio->integratedLufs-(audio->buffer.getNumChannels()==1?3.01029995664:0.))/20.):1.;return audio->peakAmplitude>1e-9?1./audio->peakAmplitude:1.;}
    void setNormalise(bool enabled){const juce::ScopedLock g(lock);normaliseEnabled=enabled;edited(false);}
    double pitchSemitones=0,currentPitchRatio=1;
    void setPitch(double value){const juce::ScopedLock g(lock);pitchSemitones=juce::jlimit(-24.,24.,value);edited(false);}
    double outputRate = 44100, sourceBpm = 120, detectStrength = 50;
    double sourceBeats = 4, sourceBeatsPerBar = 4, hostBpm = 120;
    int hostNumerator = 4, hostDenominator = 4;
    bool playMode = true, hasHost = false, hostPlaying = false;
    juce::String timingMethod = "Estimated";
    std::array<juce::String,128> padNames;
    int editorWidth = 1360, editorHeight = 1200;
    int browserWidth = 320;bool browserVisible = true;
    bool regionTiming=false,independentGrid=false;double regionBeatCount=4;int gridStart=-1,gridSnapMode=1;bool quantizeEarly=false;double quantizeAmount=1;
    int gridOrigin()const{return gridStart>=0?gridStart:regionStart();}
    void useMarkerTiming(){const juce::ScopedLock g(lock);if(!regionTiming){regionBeatCount=loopBeats();regionTiming=true;}}
    void useIndependentGrid(){const juce::ScopedLock g(lock);useMarkerTiming();independentGrid=true;if(gridStart<0)gridStart=regionStart();updateRegionTempo();}
    void updateRegionTempo(){if(independentGrid){if(audio){regionTiming=true;regionBeatCount=(regionEnd()-regionStart())/audio->rate*sourceBpm/60;double nearest=std::round(regionBeatCount);if(nearest>0&&std::abs(regionBeatCount-nearest)<=sourceBpm/(60*audio->rate)*.51)regionBeatCount=nearest;sourceBeats=audio->buffer.getNumSamples()/audio->rate*sourceBpm/60;}return;}if(regionTiming&&audio&&regionEnd()>regionStart()){sourceBpm=juce::jlimit(20.,400.,regionBeatCount*60*audio->rate/(regionEnd()-regionStart()));sourceBeats=audio->buffer.getNumSamples()/audio->rate*sourceBpm/60;}}
    void setRegionBars(double bars){const juce::ScopedLock g(lock);if(!audio||bars<=0)return;if(independentGrid){setLoopBars(bars);return;}regionTiming=true;regionBeatCount=juce::jlimit(.0625,256.,bars)*sourceBeatsPerBar;lockedBars=regionBeatCount/sourceBeatsPerBar;updateRegionTempo();updateStartAnchor();rhythmDirty=true;edited(false);}
    int trimStart = 0, trimEnd = -1, snapMode = 1,boundarySnap=0, divisions = 16, sliceMethod = 0;
    bool quantizeEnabled = false, swingEnabled = false, swingLinked = true;
    double gridSliceSwing=0;double rhythmGrid = .25, lateWindow = .45, swingAmount = 50;
    juce::String swingGroup{"A"};
    std::array<juce::String,128> defaultNames;
    int preferredSliceMethod=0;double importDetectStrength=50;
    bool chromaticMapping=true,combinedTiming=true;bool loopLengthLocked=false;double lockedBars=1;std::array<double,128> padGainDb{};std::array<int,128> padOutput{};
    bool keepGroove = false,startAuto = true;
    int startMarker = 0;
    double uiScale=1;juce::String browserPath;
    void loadPreferences() {
        const char* names[]{"Kick","Kick 2","Snare","Clap","Closed Hat","Open Hat","Perc","Perc 2","Low Tom","Mid Tom","High Tom","Rim","Shaker","Ride","Crash","FX"};
        for(int i=0;i<16;++i)defaultNames[36+i]=names[i];
        auto preferences=LibraryStore::read("settings.json");auto* object=preferences.getDynamicObject();
        for(int n=36;n<52;++n)if(object->hasProperty(juce::String(n)))defaultNames[n]=object->getProperty(juce::String(n)).toString().substring(0,40);
        if(object->hasProperty("bpmRangeMin")){bpmRangeMin=(double)object->getProperty("bpmRangeMin");bpmRangeMax=(double)object->getProperty("bpmRangeMax");}
        browserPath=object->getProperty("browserPath").toString();if(object->hasProperty("uiScale"))uiScale=juce::jlimit(.7,2.,(double)object->getProperty("uiScale"));
    }
    void saveDefaultNames() {
        std::array<juce::String,128> names;{const juce::ScopedLock g(lock);names=defaultNames;}
        LibraryStore::update("settings.json",[&](juce::DynamicObject& object){for(int n=36;n<52;++n)object.setProperty(juce::String(n),names[n]);});
    }
    void setBpmRange(double low,double high){const juce::ScopedLock g(lock);bpmRangeMin=low;bpmRangeMax=high;LibraryStore::update("settings.json",[&](juce::DynamicObject& o){o.setProperty("bpmRangeMin",low);o.setProperty("bpmRangeMax",high);});}
    void setDefaultName(int note,juce::String name) {const juce::ScopedLock g(lock);if(note>=36&&note<52)defaultNames[note]=name.trim().substring(0,40);}
    void setEnvelope(const std::set<int>& selection,Envelope envelope) {
        const juce::ScopedLock g(lock);envelope.attack=juce::jlimit(0.0,5000.0,envelope.attack);envelope.hold=juce::jlimit(0.0,5000.0,envelope.hold);envelope.decay=juce::jlimit(0.0,10000.0,envelope.decay);
        for(auto i:selection)if(juce::isPositiveAndBelow(i,(int)slices.size()))slices[i].envelope=envelope;edited(false);
    }
    // Edit only the requested AHD parameter, preserving differences in the others.
    void setEnvelopeField(const std::set<int>& selection,int field,double value) {
        const juce::ScopedLock g(lock);for(auto i:selection)if(juce::isPositiveAndBelow(i,(int)slices.size())){auto& e=slices[i].envelope;
            if(field==0)e.enabled=value!=0;else if(field==1)e.attack=juce::jlimit(0.0,5000.0,value);else if(field==2)e.hold=juce::jlimit(0.0,5000.0,value);else if(field==3)e.decay=juce::jlimit(0.0,10000.0,value);}
        edited(false);
    }
    int categoryOf(const Slice& s)const{return chromaticMapping?s.category:s.note;}
    void remapNotes(){if(!chromaticMapping)return;std::array<bool,128> reserved{};for(auto& s:slices)if(s.category>=36&&s.category<52)reserved[s.category]=true;int next=36;for(auto& s:slices){if(s.category>=36)s.note=s.category;else{while(next<128&&reserved[next])++next;s.note=next<128?next++:-1;}}}
    bool padHasMembers(int note)const{for(auto& s:slices)if(categoryOf(s)==note)return true;return false;}
    void setSliceMuted(int index,bool value){const juce::ScopedLock g(lock);if(juce::isPositiveAndBelow(index,(int)slices.size())){slices[index].muted=value;edited(false);}}
    void setSliceGain(const std::set<int>& selection,double db){const juce::ScopedLock g(lock);for(int i:selection)if(juce::isPositiveAndBelow(i,(int)slices.size()))slices[i].gainDb=juce::jlimit(-60.,24.,db);edited(false);}
    void setPadOutput(int note,int bus){const juce::ScopedLock g(lock);if(note>=36&&note<52){padOutput[note]=juce::jlimit(0,9,bus);edited(false);}}
    void setPadGain(int note,double db){const juce::ScopedLock g(lock);if(note>=36&&note<52){padGainDb[note]=juce::jlimit(-60.,24.,db);edited(false);}}
    void setLoopBars(double bars){const juce::ScopedLock g(lock);if(bars<=0)return;lockedBars=juce::jlimit(.0625,256.,bars);if(regionTiming)regionBeatCount=lockedBars*sourceBeatsPerBar;setTrimLength(lockedBars*sourceBeatsPerBar);edited();}
    void setLoopLock(bool enabled){const juce::ScopedLock g(lock);loopLengthLocked=enabled;if(enabled)lockedBars=loopBeats()/sourceBeatsPerBar;edited(false);}
    void assignCategory(const std::set<int>& selection,int note){const juce::ScopedLock g(lock);if(note!=-1&&(note<36||note>=52))return;for(int i:selection)if(juce::isPositiveAndBelow(i,(int)slices.size())){slices[i].category=note;slices[i].note=note;}remapNotes();resetCycles();edited();}
    std::set<int> repeatCandidates(const std::set<int>& seeds,int category=-1)const{
        const juce::ScopedLock g(lock);std::set<int> matches;if(!audio)return matches;double spb=audio->rate*60/sourceBpm,interval=sourceBeatsPerBar==4?2.:sourceBeatsPerBar;
        auto name=category>=0?padNames[category].toLowerCase():juce::String();if(name.contains("hat"))interval=name.contains("closed")?.5:1.;
        for(int seed:seeds){if(!juce::isPositiveAndBelow(seed,(int)slices.size()))continue;double beat=slices[seed].start/spb,phase=std::abs(beat-std::round(beat))<=.18?std::round(beat):std::round(beat*4)/4;std::map<int,std::pair<int,double>> closest;
            for(int i=0;i<(int)slices.size();++i){auto& slice=slices[i];if(slice.muted||slice.start<regionStart()||slice.start>=regionEnd())continue;double b=slice.start/spb;int cycle=(int)std::round((b-phase)/interval);double d=std::abs(b-phase-cycle*interval);if(d>juce::jmin(.18,interval*.4))continue;auto found=closest.find(cycle);if(found==closest.end()||d<found->second.second)closest[cycle]={i,d};}
            for(auto& item:closest)if((categoryOf(slices[item.second.first])<0||(category>=0&&categoryOf(slices[item.second.first])!=category))&&!seeds.count(item.second.first))matches.insert(item.second.first);}
        return matches;
    }
    void updateStartAnchor() {
        if(!audio)return;
        if(!startAuto){startMarker=juce::jlimit(regionStart(),regionEnd()-1,startMarker);return;}
        double beatSamples=audio->rate*60/sourceBpm,barSamples=beatSamples*sourceBeatsPerBar;
        auto candidate=[&](double offset){return (int)std::round((std::ceil((regionStart()-offset)/barSamples-1e-9)*barSamples)+offset);};
        double origin=independentGrid?gridOrigin():0;int first=candidate(origin),third=candidate(origin+2*beatSamples);
        startMarker=first>=regionStart()&&first<regionEnd()?first:(sourceBeatsPerBar>=3&&third>=regionStart()&&third<regionEnd()?third:regionStart());
    }
    void setStart(int position,bool automatic=false) {const juce::ScopedLock g(lock);if(!audio)return;startAuto=automatic;startMarker=juce::jlimit(regionStart(),regionEnd()-1,position);updateStartAnchor();edited();}
    void moveTrim(int delta,bool snap=true) {const juce::ScopedLock g(lock);if(!audio)return;int size=regionEnd()-regionStart(),begin=regionStart()+delta;if(snap)begin=snapPosition(begin);begin=juce::jlimit(0,audio->buffer.getNumSamples()-1,begin);setTrim(begin,begin+size);}

    std::array<bool,128> padMuted {}, padSolo {};bool categoryStateLocked=false;
    double bpmRangeMin=80,bpmRangeMax=160;
    juce::ThreadPool autoSavePool{1};std::atomic<int> autoSaveBusy{0};std::atomic<uint64_t> manualSaveCompleted{0};std::atomic<bool> manualSaveOK{true};
    bool autoSave = false, libraryDirty = false, hasSavedConfig = false;
    uint64_t configRevision = 0;
    double lastEditTime = 0;
    std::atomic<uint64_t> loadGeneration {0}, detectionGeneration {0};
    struct Event { double beat = 0;int start = 0,end = 0,note = -1,sliceIndex = 0; };
    std::array<Event,260> events;
    int eventCount = 0;
    static uint64_t nextHistoryOrder(){static std::atomic<uint64_t> sequence{0};return ++sequence;}
    std::function<void()> historyEdited;
    struct HistoryEntry {juce::String xml;std::shared_ptr<const AudioData> audio;uint64_t order=nextHistoryOrder();};
    juce::String historyBaseline;std::shared_ptr<const AudioData> historyAudio;std::vector<HistoryEntry> undoStates,redoStates;int editDepth=0,historySuppressed=0;
    juce::String historyState(){auto xml=configurationXml();xml->setAttribute("preferredSliceMethod",preferredSliceMethod);xml->setAttribute("importDetectStrength",importDetectStrength);xml->setAttribute("loopLengthLocked",loopLengthLocked);xml->setAttribute("lockedBars",lockedBars);xml->setAttribute("historySaved",hasSavedConfig);xml->setAttribute("historyDirty",libraryDirty);xml->setAttribute("swingLinked",swingLinked);xml->setAttribute("swingGroup",swingGroup);return xml->toString();}
    void limitHistory(){while(undoStates.size()>100)undoStates.erase(undoStates.begin());
        for(;;){std::set<const AudioData*> seen;int64_t bytes=0;auto count=[&](const std::shared_ptr<const AudioData>& a){if(a&&seen.insert(a.get()).second)bytes+=(int64_t)a->buffer.getNumSamples()*a->buffer.getNumChannels()*sizeof(float);};count(audio);for(auto& e:undoStates)count(e.audio);for(auto& e:redoStates)count(e.audio);if(bytes<=256*1024*1024||undoStates.size()<=1)break;undoStates.erase(undoStates.begin());}}
    void recordHistory(){if(historySuppressed||editDepth>0||!audio||slices.empty())return;auto current=historyState();if(historyBaseline.isNotEmpty()&&(current!=historyBaseline||audio!=historyAudio)){undoStates.push_back({historyBaseline,historyAudio});redoStates.clear();if(historyEdited)historyEdited();limitHistory();}historyBaseline=current;historyAudio=audio;}
    void resetHistory(){const juce::ScopedLock g(lock);undoStates.clear();redoStates.clear();editDepth=0;historyBaseline=audio&&!slices.empty()?historyState():juce::String();historyAudio=audio;}
    void beginEdit(){const juce::ScopedLock g(lock);if(editDepth++==0&&historyBaseline.isEmpty()&&audio&&!slices.empty())historyBaseline=historyState();}
    void endEdit(){const juce::ScopedLock g(lock);if(editDepth>0&&--editDepth==0)recordHistory();}
    uint64_t nextUndoOrder(bool redo){const juce::ScopedLock guard(lock);const auto& entries=redo?redoStates:undoStates;return entries.empty()?0:entries.back().order;}
    bool canUndo(){const juce::ScopedLock g(lock);return !undoStates.empty()&&editDepth==0;}
    bool canRedo(){const juce::ScopedLock g(lock);return !redoStates.empty()&&editDepth==0;}
    bool undoRedo(bool redo){juce::String group;LibraryStore::Swing linkedSwing;bool linked=false;
        {const juce::ScopedLock g(lock);auto& from=redo?redoStates:undoStates;auto& to=redo?undoStates:redoStates;if(from.empty()||editDepth>0)return false;
            auto entry=from.back();auto xml=juce::parseXML(entry.xml);if(!xml||!entry.audio)return false;HistoryEntry current{historyState(),audio,entry.order};++historySuppressed;clearVoices();audio=entry.audio;bool ok=applyConfiguration(*xml,false);
            if(ok){preferredSliceMethod=xml->getIntAttribute("preferredSliceMethod",sliceMethod);importDetectStrength=xml->getDoubleAttribute("importDetectStrength",detectStrength);loopLengthLocked=xml->getBoolAttribute("loopLengthLocked");lockedBars=xml->getDoubleAttribute("lockedBars",1);hasSavedConfig=xml->getBoolAttribute("historySaved");libraryDirty=xml->getBoolAttribute("historyDirty",true);swingLinked=xml->getBoolAttribute("swingLinked",true);swingGroup=xml->getStringAttribute("swingGroup","A");from.pop_back();to.push_back(current);++loadGeneration;++detectionGeneration;++configRevision;lastEditTime=juce::Time::getMillisecondCounterHiRes();timelineDirty=true;historyBaseline=historyState();historyAudio=audio;linked=swingLinked;group=swingGroup;linkedSwing={swingEnabled,swingAmount};}else audio=current.audio;
            --historySuppressed;if(!ok)return false;limitHistory();}
        if(linked)LibraryStore::writeSwing(group,linkedSwing);return true;
    }
    void edited(bool reschedule=true) {libraryDirty=true;++configRevision;lastEditTime=juce::Time::getMillisecondCounterHiRes();if(reschedule)timelineDirty=true;recordHistory();}
    int regionEnd() const { return audio ? (trimEnd < 0 ? audio->buffer.getNumSamples() : juce::jmax(1,trimEnd)) : 0; }
    int regionStart() const { return juce::jlimit(0,juce::jmax(0,regionEnd()-1),trimStart); }
    double loopBeats(double bpm = -1) const { return audio ? (regionTiming?regionBeatCount:(regionEnd()-regionStart())/audio->rate*(bpm < 0 ? sourceBpm : bpm)/60) : 4; }
    bool audible(int note) const {
        bool solo = false;for (auto value : padSolo) solo |= value;
        if (note < 0) return !solo;
        return !padMuted[note] && (!solo || padSolo[note]);
    }
    void setPadMuted(int note,bool value) { const juce::ScopedLock g(lock);if (note >= 0 && note < 128 && padMuted[note] != value) { padMuted[note] = value;edited(false); } }
    void setPadSolo(int note,bool value) { const juce::ScopedLock g(lock);if (note >= 0 && note < 128 && padSolo[note] != value) { padSolo[note] = value;edited(false); } }
    void setRandomOther(bool value) { const juce::ScopedLock g(lock);if (randomOther != value) { randomOther = value;resetCycles();edited(false); } }
    void setRhythm(bool quantized,double grid,double window) {
        const juce::ScopedLock g(lock);quantizeEnabled = quantized;rhythmGrid = juce::jlimit(.03125,4.0,grid);lateWindow = juce::jlimit(0.0,.99,window);rhythmDirty=true;edited(false);
    }
    void setQuantizeEarly(bool enabled){const juce::ScopedLock g(lock);quantizeEarly=enabled;rhythmDirty=true;edited(false);}
    void setQuantizeAmount(double amount){const juce::ScopedLock g(lock);quantizeAmount=juce::jlimit(0.,1.,amount);rhythmDirty=true;edited(false);}
    void excludeFromRhythm(int index) { const juce::ScopedLock g(lock);if (index >= 0 && index < (int)slices.size()) { slices[index].excludeRhythm = !slices[index].excludeRhythm;rhythmDirty=true;edited(false); } }
    void setSwing(bool enabled,double amount) {
        juce::String group;bool linked;
        { const juce::ScopedLock g(lock);swingEnabled = enabled;swingAmount = juce::jlimit(50.0,75.0,amount);group = swingGroup;linked = swingLinked;rhythmDirty=true;edited(false); }
        if (linked) LibraryStore::writeSwing(group,{enabled,amount});
    }
    void setSwingLink(bool enabled,juce::String group) {
        { const juce::ScopedLock g(lock);swingGroup = group.trim().substring(0,80);swingLinked = enabled && swingGroup.isNotEmpty();rhythmDirty=true;edited(false); }
        if (swingLinked) { LibraryStore::Swing value;if (LibraryStore::readSwing(swingGroup,value)) { const juce::ScopedLock g(lock);swingEnabled = value.enabled;swingAmount = value.amount;rhythmDirty=true;edited(false); }
            else LibraryStore::writeSwing(swingGroup,{swingEnabled,swingAmount}); }
        if (juce::MessageManager::getInstanceWithoutCreating()) startTimer(200);
    }
    void syncLinkedSwing() {
        juce::String group;{ const juce::ScopedTryLock g(lock);if (!g.isLocked()||!swingLinked) return;group = swingGroup; }
        LibraryStore::Swing value;if (!LibraryStore::readSwing(group,value)) return;
        const juce::ScopedTryLock g(lock);if (!g.isLocked())return;if (swingLinked && group == swingGroup && (swingEnabled != value.enabled || swingAmount != value.amount)) { swingEnabled = value.enabled;swingAmount = value.amount;rhythmDirty=true;edited(false); }
    }
    void beginBackgroundServices() { startTimer(200); }
    void timerCallback() override {
        serviceTrack();
        syncLinkedSwing();bool save;
        { const juce::ScopedTryLock g(lock);if (!g.isLocked()) return;save = autoSave && libraryDirty && audio && editDepth==0 && !autoSaveBusy && juce::Time::getMillisecondCounterHiRes()-lastEditTime > 800;if (save) queueAutoSave(); }
    }
    mutable std::shared_ptr<const AudioData> snapAudio;mutable double snapBpm=0,snapGrid=0;mutable int snapOrigin=0;mutable std::vector<int> snapPoints;
    int snapPosition(int position,int mode=-1) const {
        if(mode<0)mode=snapMode;
        if (!audio || mode == 0) return position;
        if (mode == 1) {double anchor=independentGrid?gridOrigin():0;return (int)std::round(anchor+std::round((position-anchor)/audio->rate*sourceBpm/60)*60/sourceBpm*audio->rate);}
        int best = position;double distance = 1e20;
        if(audio->contentHash.isNotEmpty()){if(snapAudio!=audio||snapBpm!=sourceBpm||snapGrid!=rhythmGrid||snapOrigin!=gridOrigin()){snapPoints=detectedPoints(*audio,0,audio->buffer.getNumSamples(),100,sourceBpm,gridOrigin(),rhythmGrid,{},false);snapAudio=audio;snapBpm=sourceBpm;snapGrid=rhythmGrid;snapOrigin=gridOrigin();}for(int point:snapPoints)if(std::abs(point-position)<distance){best=point;distance=std::abs(point-position);}return best;}
        if(!audio->transients.empty()){for(int point:audio->transients)if(std::abs(point-position)<distance){best=point;distance=std::abs(point-position);}return best;}
        for (const auto& slice : slices) for (int point : {slice.start,slice.end}) if (std::abs(point-position) < distance) { best = point;distance = std::abs(point-position); }
        return best;
    }
    void setTrim(int start,int end,bool snap = false) {
        const juce::ScopedLock g(lock);if (!audio) return;
        if (snap) { start = snapPosition(start);end = snapPosition(end); }
        int length = audio->buffer.getNumSamples();start = juce::jlimit(0,warpMode?juce::jmax(length-1,end-1):length-1,start);end = juce::jmax(start+1,end);
        if (trimStart != start || regionEnd() != end) { trimStart = start;trimEnd = end;updateRegionTempo();updateStartAnchor();++detectionGeneration;clearVoices();resetCycles();edited(); }
    }
    void setTrimLength(double beats) { const juce::ScopedLock g(lock);if (audio && beats > 0) setTrim(regionStart(),(int)juce::jmin((double)INT_MAX,(double)regionStart()+std::round(beats*60/sourceBpm*audio->rate))); }
    double quantizeTarget(double beat)const{
        if(!combinedTiming)return std::round(beat/rhythmGrid)*rhythmGrid;
        double cell=std::floor(beat/rhythmGrid),phase=beat/rhythmGrid-cell;
        if(phase<1e-8)return cell*rhythmGrid;
        return (phase<lateWindow?cell:cell+1)*rhythmGrid;
    }
    bool quantizeCandidate(double beat,bool excluded=false)const{
        if(!quantizeEnabled||excluded)return false;
        double delta=beat-quantizeTarget(beat);
        if(combinedTiming)return delta>=-1e-8||quantizeEarly;
        return std::abs(delta)<=rhythmGrid*lateWindow+1e-8&&(delta>=-1e-8||quantizeEarly);
    }
    double adjustedBeat(double beat,bool excluded=false)const{
        if(excluded||(combinedTiming&&!quantizeEnabled))return beat;
        double target=quantizeTarget(beat),delta=beat-target;
        bool captured=combinedTiming?quantizeCandidate(beat):std::abs(delta)<=rhythmGrid*lateWindow+1e-8;
        double swingOffset=((combinedTiming||swingEnabled)&&((int)std::round(target/rhythmGrid)&1)&&captured)?rhythmGrid*(swingAmount/50-1):0;
        if(quantizeCandidate(beat))return beat+(target+swingOffset-beat)*quantizeAmount;
        return beat+swingOffset*(quantizeEnabled?quantizeAmount:1);
    }
    void rebuildTimeline(double bpm = -1) {
        eventCount=0;if(!audio)return;updateStartAnchor();double tempo=bpm<0?sourceBpm:bpm,length=loopBeats(tempo);
        std::array<double,260> originals{},cells{},distances{};std::array<bool,260> candidates{};
        auto wrap=[&](double beat){beat=std::fmod(beat+length,length);if(beat<0)beat+=length;if(std::abs(beat)<1e-8||std::abs(beat-length)<1e-8)return 0.;return juce::jlimit(0.,juce::jmax(0.,length-1e-7),beat);};
        auto add=[&](int a,int b,int index){if(b<=a||eventCount>=260)return;const auto& slice=slices[index];double raw,anchor=0;
            if(regionTiming){double perSample=length/(regionEnd()-regionStart());int origin=independentGrid?gridOrigin():regionStart();raw=(a-origin)*perSample;anchor=(startMarker-origin)*perSample;}
            else{int offset=a-startMarker;if(offset<0)offset+=regionEnd()-regionStart();raw=offset/audio->rate*tempo/60;}
            bool continuation=a==startMarker&&slice.start<startMarker;bool excluded=slice.excludeRhythm||continuation;
            double retimed=excluded?raw:adjustedBeat(raw);
            // A hit after the playback start must not quantize backwards across
            // START and wrap to the end of the loop. Keep its original lead-in.
            bool crossesStart=raw>=anchor-1e-9&&retimed<anchor-1e-9;if(crossesStart)retimed=raw;
            originals[eventCount]=wrap(raw-anchor);cells[eventCount]=wrap(quantizeTarget(raw)-anchor);distances[eventCount]=std::abs(raw-quantizeTarget(raw));candidates[eventCount]=quantizeCandidate(raw,excluded||crossesStart);
            events[eventCount++]={wrap(retimed-anchor),a,b,slice.note,index};};
        for(int i=0;i<(int)slices.size();++i){int a=juce::jmax(regionStart(),slices[i].start),b=juce::jmin(regionEnd(),slices[i].end);if(b<=a)continue;if(a<startMarker&&b>startMarker){add(a,startMarker,i);add(startMarker,b,i);}else add(a,b,i);}
        // Each grid point gets one quantized hit. Exact hits reserve their point;
        // otherwise the closest candidate wins, with an earlier hit breaking ties.
        for(int i=0;i<eventCount;++i)if(candidates[i])for(int j=0;j<eventCount;++j)if(i!=j){bool same=std::abs(cells[i]-cells[j])<1e-7;bool reserved=std::abs(originals[j]-cells[i])<1e-7&&!candidates[j];if(reserved||(same&&candidates[j]&&(distances[j]<distances[i]-1e-8||(std::abs(distances[j]-distances[i])<1e-8&&events[j].start<events[i].start)))){events[i].beat=originals[i];break;}}
        // Protect original/off-grid hits from a moved/swung hit landing on them.
        for(int pass=0;pass<eventCount;++pass){bool changed=false;for(int i=0;i<eventCount;++i)for(int j=0;j<i;++j)if(std::abs(events[i].beat-events[j].beat)<1e-7&&std::abs(originals[i]-originals[j])>1e-7){int loser=std::abs(events[i].beat-originals[i])<1e-7?j:std::abs(events[j].beat-originals[j])<1e-7?i:distances[i]>distances[j]?i:j;events[loser].beat=originals[loser];changed=true;}if(!changed)break;}
        std::sort(events.begin(),events.begin()+eventCount,[](const Event& a,const Event& b){return a.beat!=b.beat?a.beat<b.beat:a.start<b.start;});
    }
    std::unique_ptr<Processor> contentionEngine;bool isContentionEngine=false,usedContention=false;uint64_t externalVoiceRevision=0;
    bool timelineDirty = true, rhythmDirty = false;
    std::array<int,260> firedStarts{};int firedCount=0;double firedCycle=-1;
    double expectedHostPpq = 0;
    int autoSlice = -1;
    double autoCycle = 0;
    struct Voice { double pos = 0; int start = 0, end = 0, note = -1; float gain = 0; bool active = false; bool automatic = false;double stopBeat = -1;Envelope envelope;double age=0,fadeInSamples=16,envelopeOffset=0;int sliceIndex=-1;float level=1;float audibleGain=1;int destinationIndex=-1;bool fullTail=false;std::array<float,2> lastOutput{},tail{};int tailRemaining=0,tailTotal=1,outputPair=0,tailPair=0; };
    std::array<Voice,64> voices;
    int voiceCursor = 0;
    std::array<float,2> lastRendered{};int resumeFade=0;
    bool wasPlaying = false;
    double lastPpq = 0;

    const juce::String getName() const override { return "Slice Groups"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    // Host-synchronised playback can generate sound without MIDI/input activity.
    // VST3 maps infinity to kInfiniteTail, keeping inactive tracks eligible to process.
    double getTailLengthSeconds() const override { return std::numeric_limits<double>::infinity(); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void releaseResources() override { const juce::ScopedLock g(lock);for(auto& v:voices)v={}; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate,int blockSize) override {if(wrapperType!=wrapperType_Undefined&&!trackOffline)trackPlayer.enableRealtimeCache();if(!isContentionEngine&&!contentionEngine){contentionEngine=std::make_unique<Processor>(false);contentionEngine->isContentionEngine=true;contentionEngine->stretchWorker.signalThreadShouldExit();contentionEngine->stretchWorker.stopThread(-1);}usedContention=false;if(contentionEngine){contentionEngine->audio.reset();contentionEngine->prepareToPlay(rate,blockSize);} outputRate = rate;currentPitchRatio=std::pow(2.,(pitchSemitones+keyTranspose())/12);currentNormaliseGain=normaliseGain();peakLimiter.reset();limiting=0; }
    void retireVoice(Voice& v) {if(!v.active)return;v.active=false;v.tailPair=v.outputPair;v.tail=v.lastOutput;v.tailTotal=juce::jmax(1,(int)(outputRate*.003));v.tailRemaining=(std::abs(v.tail[0])+std::abs(v.tail[1])>1e-8f)?v.tailTotal:0;}
    void unload(){const juce::ScopedLock g(lock);++loadGeneration;++detectionGeneration;++configRevision;clearVoices();shotFilterAudition=false;shotAuditionVoice={};audio.reset();slices.clear();warpArrangement.reset();playingStretch.reset();previousStretch.reset();trackPreparedAudio.reset();trackGrid={};trackGridReady=trackGridEdited=false;trackLoop=false;trackPlayer.clearSource();trackAnalyser.request({},bpmRangeMin,bpmRangeMax);hasSavedConfig=libraryDirty=false;trimStart=0;trimEnd=-1;startMarker=0;resetHistory();}
    void clearVoices() {shotHeldOrder.fill(0);shotMonoNote=-1;for(auto& v:shots){v.active=false;v.owner.reset();v.source.reset();v.region=nullptr;} for (auto& v : voices) retireVoice(v);timelineDirty = true;autoSlice = -1; }
    void clearAutomaticVoices(bool includeTails=true) { for (auto& v : voices) if (v.automatic&&(includeTails||!v.fullTail)) retireVoice(v);autoSlice = -1; }
    void setPlayMode(bool enabled) { const juce::ScopedLock g(lock);if (playMode != enabled) { playMode = enabled;clearVoices();resetCycles();edited(); } }
    void setGridTempo(double bpm,double beats){
        const juce::ScopedLock g(lock);if(!audio)return;double first=std::round((regionStart()-gridOrigin())/audio->rate*sourceBpm/60);sourceBpm=juce::jlimit(20.,400.,std::isfinite(bpm)?bpm:120.);
        int begin=(int)juce::jlimit(0.,(double)audio->buffer.getNumSamples()-1,std::round(gridOrigin()+first*60/sourceBpm*audio->rate));
        int end=(int)juce::jmin((double)INT_MAX,begin+std::round(beats*60/sourceBpm*audio->rate));
        if(trimStart!=begin||trimEnd!=end){trimStart=begin;trimEnd=juce::jmax(begin+1,end);++detectionGeneration;}
        updateRegionTempo();timingMethod="Manual";updateStartAnchor();rhythmDirty=true;edited(false);
    }
    void setGridSnap(int mode){const juce::ScopedLock g(lock);gridSnapMode=juce::jlimit(0,2,mode);edited(false);}
    void setGridStart(int position,double beats=-1,double first=0){const juce::ScopedLock g(lock);if(!audio)return;useIndependentGrid();if(beats<=0){beats=juce::jmax(1.,std::round(loopBeats()));first=std::round((regionStart()-gridOrigin())/audio->rate*sourceBpm/60);}gridStart=juce::jlimit(0,audio->buffer.getNumSamples()-1,position);trimStart=(int)juce::jlimit(0.,(double)audio->buffer.getNumSamples()-1,std::round(gridStart+first*audio->rate*60/sourceBpm));trimEnd=(int)juce::jmin((double)INT_MAX,trimStart+std::round(beats*audio->rate*60/sourceBpm));++detectionGeneration;updateRegionTempo();updateStartAnchor();rhythmDirty=true;edited(false);}
    void setSourceBpm(double bpm) {
        if(!std::isfinite(bpm))return;const juce::ScopedLock g(lock);bpm=juce::jlimit(20.,400.,bpm);if(std::abs(bpm-(warpMode?warpChunkBpm():sourceBpm))<1e-8)return;
        if(warpMode){setWarpChunkBpm(bpm);return;}if(independentGrid&&audio){double beats=stretchMode&&!warpMode?juce::jmax(1.,std::round(loopBeats()/sourceBeatsPerBar))*sourceBeatsPerBar:juce::jmax(1.,std::round(loopBeats()));setGridTempo(bpm,beats);return;}if(stretchMode&&!warpMode&&audio){regionTiming=true;regionBeatCount=juce::jmax(1.,std::round(loopBeats()/sourceBeatsPerBar))*sourceBeatsPerBar;}sourceBpm = juce::jlimit(20.0,400.0,bpm);if(regionTiming&&!independentGrid&&audio){trimEnd=(int)juce::jmin((double)INT_MAX,regionStart()+std::round(regionBeatCount*60/sourceBpm*audio->rate));updateRegionTempo();}
        if (audio) sourceBeats = audio->buffer.getNumSamples()/audio->rate*sourceBpm/60;
        updateRegionTempo();timingMethod = "Manual";updateStartAnchor();rhythmDirty=true;edited(false);
    }
    void alignGridBeat(double beat,int position,double loopGridBeats=-1){const juce::ScopedLock g(lock);if(!audio||beat<=0||position<=gridOrigin())return;useIndependentGrid();double beats=loopGridBeats>0?loopGridBeats:juce::jmax(1.,std::round(loopBeats()));setGridTempo(beat*60*audio->rate/(position-gridOrigin()),beats);}
    void setSourceBars(double bars) {
        if(!std::isfinite(bars)||bars<=0)return;
        const juce::ScopedLock g(lock);sourceBeats = juce::jlimit(.0625,256.0,bars)*sourceBeatsPerBar;
        if (audio) { sourceBpm = 60*bars*sourceBeatsPerBar/((regionEnd()-regionStart())/audio->rate);sourceBeats = audio->buffer.getNumSamples()/audio->rate*sourceBpm/60; }
        timingMethod = "Manual";updateStartAnchor();edited();
    }
    void setPadName(int note,juce::String name) {
        const juce::ScopedLock g(lock);if (note >= 0 && note < 128 && padNames[note] != name.trim().substring(0,40)) { padNames[note] = name.trim().substring(0,40);edited(false); }
    }
    void detectTiming() {
        auto data = getAudio();if (!data) return;
        double preferred, meter;{ const juce::ScopedLock g(lock);preferred = hostBpm;meter = sourceBeatsPerBar; }
        auto result = estimateLoopTiming(data->buffer,data->rate,juce::File(data->path).getFileNameWithoutExtension(),data->metadata,preferred,meter);
        const juce::ScopedLock g(lock);if (audio != data) return;
        sourceBpm = result.bpm;sourceBeats = result.beats;timingMethod = result.method;updateStartAnchor();edited();
    }
    void resetCycles() { last.fill(-1); }
    std::shared_ptr<const AudioData> getAudio() { const juce::ScopedLock g(lock); return audio; }

    std::shared_ptr<const AudioData> readSfz(const juce::File& file,std::function<bool()> cancel){
        using Map=std::map<std::string,std::string>;Map global,master,group,region,control;std::vector<Map> regions;std::set<std::string> unsupported;juce::String text;std::map<juce::String,juce::String> macros;std::function<bool(juce::File,int)> expand=[&](juce::File input,int depth){if(depth>8||!input.existsAsFile()||(cancel&&cancel()))return false;for(auto line:juce::StringArray::fromLines(input.loadFileAsString())){auto clean=line.upToFirstOccurrenceOf("//",false,false).trim();if(clean.startsWith("#define ")){auto definition=clean.substring(8).trim();auto key=definition.upToFirstOccurrenceOf(" ",false,false);macros[key]=definition.fromFirstOccurrenceOf(" ",false,false).trim();continue;}for(auto& macro:macros)clean=clean.replace(macro.first,macro.second);if(clean.startsWith("#include")){auto path=clean.fromFirstOccurrenceOf("\"",false,false).upToLastOccurrenceOf("\"",false,false);if(!expand(input.getParentDirectory().getChildFile(path),depth+1))return false;}else text+=clean+"\n";}return true;};if(!expand(file,0))return {};
        std::string content=text.replaceCharacters("\r\n\t","   ").toStdString();std::regex tokens(R"(<([^>]+)>|([A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.*?)(?=\s+[A-Za-z_][A-Za-z_0-9]*\s*=|\s*<|$))");std::string section;auto flush=[&]{if(section=="region"&&!region.empty())regions.push_back(region);};for(auto it=std::sregex_iterator(content.begin(),content.end(),tokens);it!=std::sregex_iterator();++it){if(cancel&&cancel())return {};auto match=*it;if(match[1].matched){flush();section=match[1].str();if(section=="global"){global.clear();master.clear();group.clear();}if(section=="master"){master=global;group.clear();}if(section=="group"){group=global;for(auto& v:master)group[v.first]=v.second;}if(section=="region"){region=global;for(auto& v:master)region[v.first]=v.second;for(auto& v:group)region[v.first]=v.second;}}else{auto key=match[2].str();auto value=juce::String(match[3].str()).trim().unquoted().toStdString();if(section=="control")control[key]=value;else if(section=="global")global[key]=value;else if(section=="master")master[key]=value;else if(section=="group")group[key]=value;else if(section=="region")region[key]=value;}}flush();if(regions.empty()||regions.size()>1024)return {};
        std::map<juce::String,std::shared_ptr<const AudioData>> decoded;std::shared_ptr<AudioData> result;auto note=[](juce::String value){if(value.containsOnly("0123456789-"))return value.getIntValue();auto name=value.toUpperCase();int root=name.startsWith("C")?0:name.startsWith("D")?2:name.startsWith("E")?4:name.startsWith("F")?5:name.startsWith("G")?7:name.startsWith("A")?9:name.startsWith("B")?11:-1000;auto suffix=name.substring(1);if(suffix.startsWith("#")){++root;suffix=suffix.substring(1);}else if(suffix.startsWith("B")){--root;suffix=suffix.substring(1);}return root+12*(suffix.getIntValue()+1);};
        std::set<std::string> supported{"sample","lokey","hikey","key","pitch_keycenter","lovel","hivel","transpose","tune","volume","pan","offset","end","loop_start","loop_end","loop_mode","ampeg_attack","ampeg_decay","ampeg_sustain","ampeg_release"};for(auto& op:regions){if(cancel&&cancel())return {};if(!op.count("sample"))return {};auto sampleName=juce::String(op["sample"]).replaceCharacter('\\','/');if(sampleName.startsWithChar('*')||sampleName.endsWithIgnoreCase(".sfz"))return {};auto sample=file.getParentDirectory().getChildFile(juce::String(control["default_path"]).replaceCharacter('\\','/')).getChildFile(sampleName);auto key=sample.getFullPathName();if(!decoded.count(key))decoded[key]=readAudio(sample,cancel);auto source=decoded[key];if(!source)return {};if(!result){result=std::make_shared<AudioData>(*source);result->sfzRegions.clear();result->path=file.getFullPathName();result->contentHash=LibraryStore::fingerprint(file);auto val=[&](std::string k,double fallback){return op.count(k)?juce::String(op[k]).getDoubleValue():fallback;};result->sfzEnvelope={(float)juce::jlimit(0.,5.,val("ampeg_attack",.005)),(float)juce::jlimit(0.,5.,val("ampeg_decay",.1)),(float)juce::jlimit(0.,1.,val("ampeg_sustain",80)/100),(float)juce::jlimit(0.,10.,val("ampeg_release",.15))};}
          auto val=[&](std::string k,double fallback){return op.count(k)?juce::String(op[k]).getDoubleValue():fallback;};SfzRegion r;r.source=source;if(op.count("key"))r.low=r.high=r.root=note(op["key"]);if(op.count("lokey"))r.low=note(op["lokey"]);if(op.count("hikey"))r.high=note(op["hikey"]);if(op.count("pitch_keycenter"))r.root=note(op["pitch_keycenter"]);r.lovel=(int)val("lovel",0);r.hivel=(int)val("hivel",127);r.tune=juce::jlimit(-120.,120.,val("transpose",0)+val("tune",0)/100);r.gain=juce::Decibels::decibelsToGain(juce::jlimit(-96.,24.,val("volume",0)));r.pan=juce::jlimit(-1.,1.,val("pan",0)/100);int length=source->buffer.getNumSamples();r.start=juce::jlimit(0,length-1,(int)val("offset",0));r.end=juce::jlimit(r.start+1,length,op.count("end")?(int)val("end",length-1)+1:length);r.loopStart=juce::jlimit(r.start,r.end-1,(int)val("loop_start",r.start));r.loopEnd=juce::jlimit(r.loopStart+1,r.end,op.count("loop_end")?(int)val("loop_end",r.end-1)+1:r.end);r.loopMode=op["loop_mode"]=="loop_continuous"?1:op["loop_mode"]=="loop_sustain"?2:0;result->sfzRegions.push_back(r);for(auto& v:op)if(!supported.count(v.first))unsupported.insert(v.first);}
        if(!unsupported.empty()){juce::StringArray names;for(auto& name:unsupported)names.add(name);result->sfzWarning="Unsupported SFZ opcodes: "+names.joinIntoString(", ");}return result;
    }
    std::shared_ptr<const AudioData> readAudio(const juce::File& file,std::function<bool()> cancel={}) {
        if(file.hasFileExtension("sfz"))return readSfz(file,cancel);auto originalSize=file.getSize();auto originalModified=file.getLastModificationTime();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > INT_MAX) return {};
        auto next = std::make_shared<AudioData>();
        next->buffer.setSize((int) reader->numChannels, (int) reader->lengthInSamples);
        for(int start=0;start<next->buffer.getNumSamples();start+=65536){if(cancel&&cancel())return {};int count=juce::jmin(65536,next->buffer.getNumSamples()-start);if(!reader->read(&next->buffer,start,count,start,true,true))return {};}next->rate=reader->sampleRate;next->path=file.getFullPathName();next->metadata=reader->metadataValues;next->integratedLufs=LoopLoudness::analyse(file,next->buffer,next->rate,cancel);if(cancel&&cancel())return {};if(file.getSize()!=originalSize||file.getLastModificationTime()!=originalModified)return {};if(AnalysisStore::load(file,*next))return next;if(cancel&&cancel())return {};
        next->contentHash=LibraryStore::fingerprint(file);next->rate = reader->sampleRate;next->path = file.getFullPathName();next->metadata = reader->metadataValues;
        int length = next->buffer.getNumSamples();
        next->peaks.resize((length + AudioData::peakStride - 1) / AudioData::peakStride);
        for (int n = 0; n < length; ++n)
            for (int ch = 0; ch < next->buffer.getNumChannels(); ++ch)
                next->peaks[n / AudioData::peakStride] = juce::jmax(next->peaks[n / AudioData::peakStride], std::abs(next->buffer.getSample(ch,n)));
        if(cancel&&cancel())return {};next->buildWaveformPeaks();if(cancel&&cancel())return {};
        next->transients=detectedPoints(*next,0,length,100,120,0,.25,cancel);if(cancel&&cancel())return {};{const juce::ScopedLock lock(this->lock);next->analysedPreferred=hostBpm;next->analysedMeter=hostNumerator*4.0/hostDenominator;}int numerator=next->metadata.getValue(juce::WavAudioFormat::acidNumerator,"0").getIntValue(),denominator=next->metadata.getValue(juce::WavAudioFormat::acidDenominator,"0").getIntValue();if(numerator>0&&denominator>0)next->analysedMeter=numerator*4.0/denominator;next->analysedTiming=estimateLoopTiming(next->buffer,next->rate,file.getFileNameWithoutExtension(),next->metadata,next->analysedPreferred,next->analysedMeter);if(cancel&&cancel())return {};if(file.getSize()!=originalSize||file.getLastModificationTime()!=originalModified)return {};AnalysisStore::save(file,*next);
        return next;
    }
    static int keyFromFilename(const juce::String& path){
        auto name=juce::File(path).getFileNameWithoutExtension().replace(juce::String::charToString(0x266f),"#").replace(juce::String::charToString(0x266d),"b");
        juce::StringArray tokens;tokens.addTokens(name,"_ -()[]{}.,","");
        for(int index=tokens.size();--index>=0;){auto token=tokens[index];if(token.isEmpty())continue;auto root=token.substring(0,1).toUpperCase();int key=-1;
            if(root=="C")key=0;else if(root=="D")key=2;else if(root=="E")key=4;else if(root=="F")key=5;else if(root=="G")key=7;else if(root=="A")key=9;else if(root=="B")key=11;if(key<0)continue;
            auto suffix=token.substring(1);if(suffix.startsWith("#")){++key;suffix=suffix.substring(1);}else if(suffix.startsWith("b")){--key;suffix=suffix.substring(1);}
            // Octave labels identify pitch class too: C3, G#3, Bb2.
            while(suffix.isNotEmpty()&&juce::CharacterFunctions::isDigit(suffix.getLastCharacter()))suffix=suffix.dropLastCharacters(1);
            auto quality=suffix.toLowerCase();if(quality.isNotEmpty()&&quality!="m"&&quality!="min"&&quality!="minor"&&quality!="maj"&&quality!="major")continue;
            if(token.length()==1&&token!=root)continue; // Avoid ordinary lowercase words such as “a”.
            return (key+12)%12;
        }return -1;
    }
    bool installAudio(std::shared_ptr<const AudioData> next,bool restoreSaved = true,uint64_t serial = 0,bool prepareStretch=true) {
        if (!next) return false;
        queueAutoSave(true);
        double preferred,meter,heldBegin=0,heldStart=0;
        { const juce::ScopedLock g(lock);if(audio){heldBegin=(regionStart()-gridOrigin())/audio->rate*sourceBpm/60;heldStart=(startMarker-regionStart())/audio->rate*sourceBpm/60;}preferred = hostBpm;meter = hostNumerator*4.0/hostDenominator; }
        int numerator = next->metadata.getValue(juce::WavAudioFormat::acidNumerator,"0").getIntValue();
        int denominator = next->metadata.getValue(juce::WavAudioFormat::acidDenominator,"0").getIntValue();
        if (numerator > 0 && denominator > 0) meter = numerator*4.0/denominator;
        auto timing = next->timingFor(preferred,meter);
        timing=constrainEstimatedTempo(timing,bpmRangeMin,bpmRangeMax);if(!std::isfinite(timing.bpm)||timing.bpm<=0)timing.bpm=120;double bounded=juce::jlimit(20.,400.,timing.bpm);timing.beats*=bounded/timing.bpm;timing.bars*=bounded/timing.bpm;timing.bpm=bounded;
        auto saved = restoreSaved ? LibraryStore::savedLoop(juce::File(next->path),next->contentHash) : nullptr;
        const auto importType=LibraryStore::playbackType(juce::File(next->path));const bool explicitImportType=LibraryStore::hasPlaybackType(juce::File(next->path));const int importedBrowserKey=LibraryStore::fileKey(juce::File(next->path),next->contentHash);
        const juce::ScopedLock g(lock);if(serial&&serial!=loadGeneration.load())return false;
        double heldCalibration=trackGrid.calibrationMs;bool carryTrackNudge=trackMode&&audio;double heldTrackNudgeMs=carryTrackNudge?trackGrid.sampleOffset/audio->rate*1000.:0.;auto heldMuted=padMuted,heldSolo=padSolo;bool holdStates=categoryStateLocked;bool previous=audio!=nullptr;if(previous)recordHistory();++historySuppressed;int method=preferredSliceMethod;double strength=importDetectStrength;bool q=quantizeEnabled,sw=swingEnabled,early=quantizeEarly;double grid=rhythmGrid,window=lateWindow,amount=swingAmount,strengthAmount=quantizeAmount;
        shotFilterAudition=false;shotAuditionVoice.active=false;++detectionGeneration;trackPlayer.queueSeek(std::numeric_limits<double>::quiet_NaN());stretchAnchorEnabled=false;stretchAnchorSample=0;trackPreparedAudio.reset();trackGrid={};trackGrid.bpm=juce::jlimit(20.,400.,timing.bpm);if(carryTrackNudge){trackGrid.calibrationMs=heldCalibration;trackGrid.setSampleOffset(heldTrackNudgeMs*next->rate/1000.);}trackGridEdited=trackGridReady=false;trackLoop=false;if(trackMode&&claimTrack)claimTrack();trackPlayer.seek=std::numeric_limits<double>::quiet_NaN();shotLoopTravel=0;shotNoteCycles=1;shotNoteLoop=false;shotHeldOrder.fill(0);shotMonoNote=-1;shotOctaveShift=0;shotStart=shotLoopStart=0;shotEnd=shotLoopEnd=-1;shotLoop=false;audio = next;trimStart = 0;trimEnd = next->buffer.getNumSamples();sourceBpm = timing.bpm;sourceBeats = timing.beats;sourceBeatsPerBar = meter;regionTiming=chromaticMapping&&combinedTiming;independentGrid=regionTiming;gridStart=0;regionBeatCount=next->buffer.getNumSamples()/next->rate*sourceBpm/60;
        if(!saved||explicitImportType){int mode=LibraryStore::modeForType(importType);trackMode=mode==4;oneShotMode=mode==3;warpMode=mode==2;stretchMode=mode==1||mode==2||mode==4;}
        loopKey=keyFromFilename(next->path);keyAlternate=false;warpArrangement.reset();if(warpMode&&!saved){defaultWarpTiming();initialiseWarp();}timingMethod = timing.method;startAuto=true;updateStartAnchor();padNames=defaultNames;if(!holdStates){padMuted.fill(false);padSolo.fill(false);}padGainDb.fill(0);slices.clear();split(divisions);preferredSliceMethod=method;importDetectStrength=strength;
        hasSavedConfig=saved&&applyConfiguration(*saved,false);int browserKey=importedBrowserKey;if(browserKey!=-2){loopKey=browserKey;keyAlternate=false;}if(holdStates){padMuted=heldMuted;padSolo=heldSolo;}if(keepGroove&&restoreSaved){quantizeEnabled=q;swingEnabled=sw;rhythmGrid=grid;lateWindow=window;swingAmount=amount;quantizeEarly=early;quantizeAmount=strengthAmount;}
        libraryDirty=!hasSavedConfig||(keepGroove&&saved&&(q!=saved->getBoolAttribute("quantize")||sw!=saved->getBoolAttribute("swing")||grid!=saved->getDoubleAttribute("grid",.25)||window!=saved->getDoubleAttribute("lateWindow",.45)||amount!=saved->getDoubleAttribute("swingAmount",50)||early!=saved->getBoolAttribute("quantizeEarly",false)||strengthAmount!=saved->getDoubleAttribute("quantizeAmount",1)));lastEditTime=juce::Time::getMillisecondCounterHiRes();
        if(loopLengthLocked){if(regionTiming)regionBeatCount=lockedBars*sourceBeatsPerBar;double spb=60/sourceBpm*audio->rate,len=lockedBars*sourceBeatsPerBar*spb;int begin=(int)std::round(gridOrigin()+heldBegin*spb);begin=juce::jlimit(0,juce::jmax(0,(int)std::floor(audio->buffer.getNumSamples()-len)),begin);setTrim(begin,begin+(int)std::round(len));setStart(begin+(int)std::round(heldStart*spb));if(!hasSavedConfig){split(divisions);preferredSliceMethod=method;importDetectStrength=strength;}}if(!hasSavedConfig||explicitImportType)oneShotMode=importType==1;bool sliceImport=(explicitImportType||!hasSavedConfig)?LibraryStore::modeForType(importType)==0:!stretchMode&&!oneShotMode&&!trackMode;bool detectNew=sliceImport&&restoreSaved&&!hasSavedConfig&&method==1;{const juce::ScopedUnlock unlock(lock);if(detectNew)detect(strength,0,true);bool categorise=false;{const juce::ScopedLock check(lock);categorise=sliceImport&&audio==next&&(!serial||serial==loadGeneration.load())&&autoCategories&&!oneShotMode&&std::none_of(slices.begin(),slices.end(),[this](const Slice& slice){return categoryOf(slice)>=0;});}if(categorise)autoCategorise();}if(audio!=next||(serial&&serial!=loadGeneration.load())){--historySuppressed;return false;}if(!hasSavedConfig||explicitImportType)oneShotMode=importType==1;if(!next->sfzRegions.empty()){oneShotMode=true;if(!hasSavedConfig){shotEnvelope=next->sfzEnvelope;auto& first=next->sfzRegions[0];shotStart=first.start;shotEnd=first.end;shotLoopStart=first.loopStart;shotLoopEnd=first.loopEnd;shotLoop=std::any_of(next->sfzRegions.begin(),next->sfzRegions.end(),[](const SfzRegion& r){return r.loopMode>0;});}}if((!hasSavedConfig&&importType==2)||explicitImportType){trackMode=importType==2;}if(trackMode){if(carryTrackNudge){double carriedOffset=(heldTrackNudgeMs+((heldCalibration>0||std::abs(heldTrackNudgeMs)>.1)?heldCalibration-trackGrid.calibrationMs:0))*audio->rate/1000.;if(std::abs(trackGrid.sampleOffset-carriedOffset)>1e-6)libraryDirty=true;trackGrid.setSampleOffset(carriedOffset);}if(claimTrack)claimTrack();oneShotMode=warpMode=false;stretchMode=playMode=true;serviceTrack();}else trackPlayer.clearSource();if(explicitImportType||!hasSavedConfig){int type=importType;if(type>=3||type==0){trackMode=oneShotMode=false;warpMode=type==5;stretchMode=type==4||type==5;if(stretchMode)playMode=true;if(warpMode&&!warpArrangement){defaultWarpTiming();initialiseWarp();}}}if(oneShotMode){trackMode=false;stretchMode=warpMode=false;playMode=false;for(auto& voice:shots)voice.active=false;}if(stretchMode&&!warpMode&&!trackMode&&!hasSavedConfig&&audio)setGridTempo(sourceBpm,juce::jmax(1.,std::round(loopBeats()/sourceBeatsPerBar))*sourceBeatsPerBar);if(stretchMode&&!trackMode&&prepareStretch)stretchWorker.request(stretchRequest());--historySuppressed;if(restoreSaved&&previous)recordHistory();else resetHistory();return true;
    }
    bool load(const juce::File& file,bool restoreSaved = true) {
        auto serial = ++loadGeneration;return installAudio(readAudio(file),restoreSaved,serial);
    }
    void replaceRegionSlices(std::vector<Slice> incoming,int begin,int end,bool preserveInterior=false){
        auto old=slices;std::vector<Slice> result;
        for(auto slice:old){if(slice.start<begin){auto part=slice;part.end=juce::jmin(slice.end,begin);if(part.end>part.start)result.push_back(part);}if(slice.end>end){auto part=slice;part.start=juce::jmax(slice.start,end);if(part.end>part.start)result.push_back(part);}}
        size_t remaining=result.size()<256?256-result.size():0;
        if(incoming.size()>remaining&&remaining>0){incoming.resize(remaining);incoming.back().end=end;}
        if(remaining>0)for(auto slice:incoming){auto found=std::find_if(old.begin(),old.end(),[&](const Slice& x){return x.start==slice.start;});if(preserveInterior&&found!=old.end()){int finish=slice.end;slice=*found;slice.end=finish;}result.push_back(slice);}
        std::sort(result.begin(),result.end(),[](const Slice& a,const Slice& b){return a.start<b.start;});
        for(auto& slice:result)if(juce::isPositiveAndBelow(slice.replacement,(int)old.size())){int target=old[slice.replacement].start;slice.replacement=-1;for(int i=0;i<(int)result.size();++i)if(result[i].start==target){slice.replacement=i;break;}}
        slices=std::move(result);
    }
    void split(int count,bool perBar=true) {
        const juce::ScopedLock g(lock);resetCycles();clearVoices();if (!audio) return;
        int length=audio->buffer.getNumSamples(),start=juce::jmin(regionStart(),length-1),end=juce::jmin(regionEnd(),length);
        divisions=juce::jlimit(1,256,count);double step=perBar?audio->rate*60/sourceBpm*sourceBeatsPerBar/divisions:(double)(end-start)/divisions;step=juce::jmax(1.,step);count=juce::jlimit(1,juce::jmin(256,end-start),(int)std::ceil((end-start)/step-1e-9));sliceMethod = 0;preferredSliceMethod=0;
        auto boundary=[&](int i){if(i==count)return end;return juce::jlimit(start,end-1,start+(int)(step*i+((i&1)&&i<count?step*gridSliceSwing/200.:0)));};std::vector<Slice> incoming;for(int i=0;i<count;++i)incoming.push_back({boundary(i),boundary(i+1),-1});
        if(slices.empty()){if(start>0)slices.push_back({0,start,-1});if(end<length)slices.push_back({end,length,-1});}replaceRegionSlices(std::move(incoming),start,end);remapNotes();edited();
    }
    void setGridSliceSwing(double value){
        const juce::ScopedLock g(lock);gridSliceSwing=juce::jlimit(0.,100.,value);if(!audio||sliceMethod!=0){edited(false);return;}
        int begin=regionStart(),end=juce::jmin(regionEnd(),audio->buffer.getNumSamples());std::vector<int> active;for(int i=0;i<(int)slices.size();++i)if(slices[i].start>=begin&&slices[i].end<=end)active.push_back(i);
        if(active.empty())return;double step=juce::jmax(1.,audio->rate*60/sourceBpm*sourceBeatsPerBar/divisions);auto boundary=[&](int i){if(i==(int)active.size())return end;return juce::jlimit(begin,end-1,begin+(int)std::round(step*i+((i&1)&&i<(int)active.size()?step*gridSliceSwing/200.:0)));};for(int i=0;i<(int)active.size();++i){slices[active[i]].start=boundary(i);slices[active[i]].end=boundary(i+1);}++detectionGeneration;clearVoices();resetCycles();edited();
    }
    static int refineTransientStart(const AudioData& data,int detected,int begin,int end){
        int window=juce::jmax(1,(int)(data.rate*.00025)),left=juce::jmax(begin,detected-(int)(data.rate*.012)),right=juce::jmin(end,detected+(int)(data.rate*.003));
        std::vector<std::pair<int,double>> envelope;double peak=0,baseline=0;int baselineCount=0;
        for(int a=left;a<right;a+=window){double level=0;int b=juce::jmin(a+window,right);for(int n=a;n<b;++n){float amplitude=0;for(int ch=0;ch<data.buffer.getNumChannels();++ch)amplitude=juce::jmax(amplitude,std::abs(data.buffer.getSample(ch,n)));level+=amplitude;}level/=b-a;envelope.push_back({a,level});peak=juce::jmax(peak,level);if(a<left+(int)(data.rate*.004)){baseline+=level;++baselineCount;}}
        baseline/=juce::jmax(1,baselineCount);double threshold=juce::jmax(.0014,juce::jmax(baseline*1.8,baseline+(peak-baseline)*.08));
        for(size_t i=1;i<envelope.size();++i)if(envelope[i].second>=threshold&&envelope[i-1].second<threshold){int onset=envelope[i-1].first;return juce::jmin(detected,onset);}
        return detected;
    }
    static std::vector<int> detectedPoints(const AudioData& data,int begin,int end,double strength,double bpm=120,int origin=0,double grid=.25,std::function<bool()> cancel={},bool limitSlices=true){
        int length=data.buffer.getNumSamples();
        const int hop = juce::jmax(32,(int)(data.rate * .003));
        // Build a sensitivity-independent hierarchy. Earlier, stronger detections
        // keep their place when weaker detections become eligible.
        struct Candidate { int onset; double sensitivity; };
        std::vector<Candidate> candidates;
        double previous=0;
        for(int a=begin;a<end;a+=hop){
            if(cancel&&cancel())return {};double energy=0;
            for(int n=a;n<juce::jmin(a+hop,end);++n){float amplitude=0;for(int ch=0;ch<data.buffer.getNumChannels();++ch)amplitude=juce::jmax(amplitude,std::abs(data.buffer.getSample(ch,n)));energy+=amplitude;}
            energy/=juce::jmin(hop,end-a);
            double ratio=previous>1e-12?energy/previous:energy>0?1e12:0;
            double activation=juce::jmax(0.,juce::jmax((.055-energy)/.00051,(3.5-ratio)/.0225));
            if(energy>.004&&ratio>1.25&&activation<=100){
                int onset=a;
                while(onset>juce::jmax(begin,a-hop)){float amplitude=0;for(int ch=0;ch<data.buffer.getNumChannels();++ch)amplitude=juce::jmax(amplitude,std::abs(data.buffer.getSample(ch,onset-1)));if(amplitude<.0014f)break;--onset;}
                onset=refineTransientStart(data,onset,begin,end);candidates.push_back({onset,activation});
            }
            previous=previous*.65+energy*.35;
        }
        // Group only genuinely close candidates; choose the representative
        // once for all sensitivity values, preferring the aligned onset.
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.onset<b.onset;});
        std::vector<Candidate> clusters;double spb=data.rate*60/bpm;
        auto gridDistance=[&](int onset){double beat=(onset-origin)/spb;return std::abs(beat-std::round(beat/grid)*grid);};
        for(size_t i=0;i<candidates.size();){size_t j=i+1;Candidate chosen=candidates[i];double activation=chosen.sensitivity;while(j<candidates.size()&&candidates[j].onset-candidates[i].onset<data.rate*.025){auto candidate=candidates[j++];activation=juce::jmin(activation,candidate.sensitivity);double a=gridDistance(candidate.onset),b=gridDistance(chosen.onset);if(a<b-.001||(std::abs(a-b)<=.001&&candidate.sensitivity<chosen.sensitivity))chosen=candidate;}chosen.sensitivity=activation;clusters.push_back(chosen);i=j;}
        for(size_t i=0;i<clusters.size();++i){bool isolated=(i==0||clusters[i].onset-clusters[i-1].onset>data.rate*.08)&&(i+1==clusters.size()||clusters[i+1].onset-clusters[i].onset>data.rate*.08);clusters[i].sensitivity=juce::jlimit(0.,100.,clusters[i].sensitivity+(isolated?-12.:8.));}
        candidates=std::move(clusters);
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.sensitivity!=b.sensitivity?a.sensitivity<b.sensitivity:a.onset<b.onset;});
        std::vector<int> accepted;
        const size_t limit=limitSlices?(end<length?253u:255u):std::numeric_limits<size_t>::max();
        for(const auto& candidate:candidates){
            if(candidate.sensitivity>strength)break;
            int gap=(int)(data.rate*.025);
            if(candidate.onset-begin<juce::jmax(1,(int)(data.rate*.003)))continue;
            bool nearby=false;for(int point:accepted)if(std::abs(point-candidate.onset)<=gap){nearby=true;break;}
            if(!nearby&&accepted.size()<limit)accepted.push_back(candidate.onset);
        }
        std::sort(accepted.begin(),accepted.end());
        std::vector<int> points{0};if(begin>0)points.push_back(begin);
        points.insert(points.end(),accepted.begin(),accepted.end());
        if (end < length) points.push_back(end);points.push_back(length);
        return points;
    }
    void detect(double strength,uint64_t serial = 0,bool wholeFile=false) {
        auto data = getAudio(); if (!data) return;
        strength = juce::jlimit(0.0,100.0,strength);
        const int length = data->buffer.getNumSamples();
        int begin,end,requestedBegin,requestedEnd,origin;double bpm,grid;{ const juce::ScopedLock g(lock);requestedBegin=regionStart();requestedEnd=regionEnd();begin=wholeFile?0:requestedBegin;end=wholeFile?length:juce::jmin(requestedEnd,length);origin=gridOrigin();bpm=sourceBpm;grid=rhythmGrid; }
        auto points=detectedPoints(*data,begin,end,strength,bpm,origin,grid);
        const juce::ScopedLock g(lock);
        if (audio != data || requestedBegin != regionStart() || requestedEnd != regionEnd() || (serial && serial != detectionGeneration.load())) return;
        detectStrength = strength;importDetectStrength=strength;preferredSliceMethod=1;resetCycles(); clearVoices();
        std::vector<Slice> incoming;for(int i=0;i+1<(int)points.size();++i)if(points[i]>=begin&&points[i+1]<=end)incoming.push_back({points[i],points[i+1],-1});replaceRegionSlices(std::move(incoming),begin,end,true);sliceMethod = 1;remapNotes();edited();if(autoCategories){const juce::ScopedUnlock unlock(lock);autoCategorise();}
    }
    bool autoCategories=false,repeatCategories=false;double categoryAnalysisMs=0;
    int autoCategorise(){
        auto started=juce::Time::getMillisecondCounterHiRes();std::shared_ptr<const AudioData> data;std::vector<Slice> snapshot;double bpm;int origin;
        {const juce::ScopedLock g(lock);data=audio;snapshot=slices;bpm=sourceBpm;origin=gridOrigin();}if(!data||!chromaticMapping)return 0;
        struct Feature{double low=0,bright=0,tail=0,energy=0;};std::vector<Feature> features(snapshot.size());std::vector<int> notes(snapshot.size(),-1);
        int stride=juce::jmax(1,(int)(data->rate/16000));double alpha=1-std::exp(-2*juce::MathConstants<double>::pi*250/(data->rate/stride));
        for(int i=0;i<(int)snapshot.size();++i){auto slice=snapshot[i];double low=0,previous=0,total=0,bass=0,diff=0,tail=0;int count=0;int end=juce::jmin(slice.end,slice.start+(int)(data->rate*.22));
            for(int n=slice.start;n<end;n+=stride){double v=0;for(int ch=0;ch<data->buffer.getNumChannels();++ch)v+=data->buffer.getSample(ch,n);v/=data->buffer.getNumChannels();low+=alpha*(v-low);total+=v*v;bass+=low*low;diff+=(v-previous)*(v-previous);previous=v;if(n-slice.start>data->rate*.06)tail+=v*v;++count;}
            auto& f=features[i];f.energy=count?total/count:0;f.low=bass/(total+1e-12);f.bright=diff/(total+1e-12);f.tail=tail/(total+1e-12);notes[i]=slice.category;
            if(notes[i]>=0||f.energy<1e-7)continue;double beat=(slice.start-origin)/data->rate*bpm/60;double phase=beat-std::floor(beat),bar=std::fmod(std::round(beat),4.);bool backbeat=(bar==1||bar==3)&&std::abs(beat-std::round(beat))<.13;bool offbeat=std::abs(phase-.5)<.13;
            if(f.low>.60&&f.bright<.12)notes[i]=36;
            else if(backbeat&&f.low<.45&&f.bright>.12)notes[i]=39;
            else if(offbeat&&f.low<.18&&f.bright>.65)notes[i]=f.tail>.12?41:40;
        }
        // Compare level-independent features to confident or manually assigned hits.
        auto distance=[](Feature a,Feature b){return std::abs(a.low-b.low)*3+std::abs(std::log((a.bright+.02)/(b.bright+.02)))*.4+std::abs(a.tail-b.tail);};
        auto seeds=notes;for(int i=0;i<(int)snapshot.size();++i)if(notes[i]<0&&features[i].energy>1e-7){double best=.32;int note=-1;for(int j=0;j<(int)snapshot.size();++j)if(seeds[j]>=0){double d=distance(features[i],features[j]);if(d<best){best=d;note=seeds[j];}}notes[i]=note;}
        const juce::ScopedLock g(lock);if(audio!=data||slices.size()!=snapshot.size())return 0;for(int i=0;i<(int)slices.size();++i)if(slices[i].start!=snapshot[i].start||slices[i].end!=snapshot[i].end)return 0;
        int changed=0;beginEdit();for(int i=0;i<(int)slices.size();++i)if(slices[i].category<0&&notes[i]>=36&&notes[i]<52){slices[i].category=notes[i];++changed;}if(changed){remapNotes();resetCycles();edited(false);}endEdit();categoryAnalysisMs=juce::Time::getMillisecondCounterHiRes()-started;return changed;
    }
    int unusedNote() const {
        for (int n = 36; n < 128; ++n) {
            bool used = false; for (const auto& s : slices) if (s.note == n) { used = true; break; }
            if (!used) return n;
        }
        return -1;
    }
    bool moveBoundary(int index,int position,bool snap=true){const juce::ScopedLock g(lock);if(index<=0||index>=(int)slices.size())return false;
        if(snap)position=snapPosition(position,boundarySnap);position=juce::jlimit(slices[index-1].start+1,slices[index].end-1,position);
        if(position==slices[index].start)return false;slices[index-1].end=position;slices[index].start=position;++detectionGeneration;clearVoices();resetCycles();edited();return true;}
    bool addBoundary(int position) {
        const juce::ScopedLock g(lock);
        if (slices.size() >= 256) return false;
        for (int i = 0; i < (int)slices.size(); ++i) {
            auto old = slices[i];
            if (position <= old.start || position >= old.end) continue;
            int note = old.note;slices[i].end = position;
            old.start=position;slices[i].end=position;slices.insert(slices.begin()+i+1,old);for(auto& slice:slices)if(slice.replacement>i)++slice.replacement;remapNotes();
            clearVoices();resetCycles();edited();return true;
        }
        return false;
    }
    bool removeBoundary(int index) {
        const juce::ScopedLock g(lock);
        if (index <= 0 || index >= (int)slices.size()) return false;
        slices[index-1].end = slices[index].end; slices.erase(slices.begin()+index);for(auto& slice:slices){if(slice.replacement==index)slice.replacement=-1;else if(slice.replacement>index)--slice.replacement;}remapNotes();
        clearVoices();resetCycles();edited();return true;
    }
    void toggleMember(int index,int note) {
        const juce::ScopedLock g(lock);
        if (index < 0 || index >= (int)slices.size() || note < 0 || note >= 128) return;
        slices[index].note = slices[index].note == note ? -1 : note;
        resetCycles();edited();
    }
    void replaceSlices(const std::set<int>& destinations,int source){
        const juce::ScopedLock g(lock);if(source<-1||source>=(int)slices.size())return;
        std::set<int> seen;while(source>=0&&slices[source].replacement>=0){if(!seen.insert(source).second)return;source=slices[source].replacement;if(!juce::isPositiveAndBelow(source,(int)slices.size()))return;}for(int i:destinations)if(juce::isPositiveAndBelow(i,(int)slices.size()))slices[i].replacement=i==source?-1:source;
        edited(false);
    }
    void startVoice(const Slice& input,float velocity,bool automatic = false,double position = -1,double stopBeat = -1,double fadeSeconds=.0005,bool allowReplacement=true) {
        Slice s=input;int destination=-1;
        for(int i=0;i<(int)slices.size();++i)if(input.start>=slices[i].start&&input.start<slices[i].end){destination=i;break;}
        int source=destination>=0?slices[destination].replacement:-1;
        bool replaced=allowReplacement&&juce::isPositiveAndBelow(source,(int)slices.size());
        if(replaced){s=slices[source];double elapsed=(position<0?input.start:position)-slices[destination].start;position=s.start+elapsed;stopBeat=-1;if(position>=s.end)return;}
        int chokeCategory=categoryOf(input);for(auto& v:voices)if(v.active){int category=juce::isPositiveAndBelow(v.destinationIndex,(int)slices.size())?categoryOf(slices[v.destinationIndex]):-1;if(chokeCategory>=0&&category==chokeCategory)retireVoice(v);}
        Voice* free = nullptr;
        for (auto& v : voices) if (!v.active && v.tailRemaining==0) { free = &v; break; }
        if (!free) free = &voices[(voiceCursor++)%64];
        int carryPair=free->active?free->outputPair:free->tailPair;auto carry=free->active?free->lastOutput:std::array<float,2>{};if(free->tailRemaining>0)for(int ch=0;ch<2;++ch)carry[ch]+=free->tail[ch]*(float)juce::jmin(free->tailRemaining+1,free->tailTotal)/free->tailTotal;bool stealing=free->active||free->tailRemaining>0;
        *free={position<0?(double)s.start:position,s.start,s.end,s.note,velocity,true,automatic,stopBeat,s.envelope};
        for(int i=0;i<(int)slices.size();++i)if(s.start>=slices[i].start&&s.start<slices[i].end){free->sliceIndex=i;break;}free->level=(float)juce::Decibels::decibelsToGain(s.gainDb+(categoryOf(input)>=0?padGainDb[categoryOf(input)]:0));
        free->outputPair=juce::isPositiveAndBelow(categoryOf(input),128)?padOutput[categoryOf(input)]:0;free->tailPair=carryPair;free->destinationIndex=destination;free->fullTail=replaced;free->note=input.note;
        free->envelopeOffset=audio&&position>s.start?(position-s.start)/audio->rate*1000:0;free->fadeInSamples=audio?juce::jmax(1.0,audio->rate*fadeSeconds):16;free->audibleGain=!input.muted&&audible(categoryOf(input))?1:0;
        if(stealing){free->tail=carry;free->tailTotal=juce::jmax(1,(int)(outputRate*.003));free->tailRemaining=free->tailTotal;}
    }
    void auditionOriginal(int index){const juce::ScopedLock g(lock);++externalVoiceRevision;if(juce::isPositiveAndBelow(index,(int)slices.size()))startVoice(slices[index],.85f,false,-1,-1,.0005,false);}
    void audition(int index) {
        const juce::ScopedLock g(lock);++externalVoiceRevision;
        if (index >= 0 && index < (int)slices.size()&&!slices[index].muted) { auto slice = slices[index];slice.start = juce::jmax(slice.start,regionStart());slice.end = juce::jmin(slice.end,regionEnd());if (slice.end > slice.start) startVoice(slice,.85f); }
    }
    void trigger(int note,float velocity) {
        if (note < 0 || note >= 128) return;
        std::array<int,256> members{}; int n = 0;
        for (int i = 0; i < (int)slices.size() && n < 256; ++i) if (!slices[i].muted&&slices[i].note == note && slices[i].end > regionStart() && slices[i].start < regionEnd()) members[n++] = i;
        if (!n) return;
        int previous = last[note];
        if (previous >= n) previous = -1;
        int chosen = 0;
        if (randomOther && n > 1) {
            chosen = random.nextInt(n-(previous >= 0 ? 1 : 0));
            if (previous >= 0 && chosen >= previous) ++chosen;
        } else chosen = (previous+1)%n;
        last[note] = chosen;auto slice = slices[members[chosen]];slice.start = juce::jmax(slice.start,regionStart());slice.end = juce::jmin(slice.end,regionEnd());startVoice(slice,velocity);
    }
    void scheduleAuto(double ppq,bool seek) {
        double length = loopBeats();if (!audio || eventCount == 0 || length <= 0) return;
        double cycle = std::floor(ppq/length),phase = ppq-cycle*length;
        int index = -1;for (int i = 0; i < eventCount; ++i) if (events[i].beat <= phase+1e-9) index = i;else break;
        if (!seek && index == autoSlice && cycle == autoCycle) return;
        if(seek || cycle!=firedCycle){firedCount=0;firedCycle=cycle;}
        autoSlice=index;autoCycle=cycle;if(index<0)return;
        bool fresh=false;for(int i=index;i>=0&&std::abs(events[i].beat-events[index].beat)<1e-9;--i)
            if(std::find(firedStarts.begin(),firedStarts.begin()+firedCount,events[i].start)==firedStarts.begin()+firedCount)fresh=true;
        if(!fresh&&!seek)return;
        clearAutomaticVoices(false);autoSlice=index;
        int first = index;while (first > 0 && std::abs(events[first-1].beat-events[index].beat) < 1e-9) --first;
        double stop = cycle*length+(index+1 < eventCount ? events[index+1].beat : length);
        for (int i = first; i <= index; ++i) {
            auto event = events[i];
            if(!seek&&std::find(firedStarts.begin(),firedStarts.begin()+firedCount,event.start)!=firedStarts.begin()+firedCount)continue;
            if(firedCount<260)firedStarts[firedCount++]=event.start;
            double elapsed = seek ? juce::jmax(0.0,phase-event.beat)*60/hostBpm*audio->rate*currentPitchRatio : 0;
            double position = event.start+elapsed;if (position >= event.end) continue;
            auto slice=slices[event.sliceIndex];if(slice.muted)continue;slice.start=event.start;slice.end=event.end;startVoice(slice,1.0f,true,position,stop,seek?.003:.0005);
        }
    }

    void copySliceRuntime(Processor& target)const{target.shotHeldOrder=shotHeldOrder;target.shotHeldVelocity=shotHeldVelocity;target.shotHeldSerial=shotHeldSerial;target.shotMonoNote=shotMonoNote;target.shotAuditionVoice=shotAuditionVoice;target.shots=shots;target.shotCursor=shotCursor;target.voices=voices;target.voiceCursor=voiceCursor;target.last=last;target.random=random;target.firedStarts=firedStarts;target.firedCount=firedCount;target.firedCycle=firedCycle;target.autoSlice=autoSlice;target.autoCycle=autoCycle;target.expectedHostPpq=expectedHostPpq;target.lastPpq=lastPpq;target.wasPlaying=wasPlaying;target.currentPitchRatio=currentPitchRatio;target.currentNormaliseGain=currentNormaliseGain;target.lastRendered=lastRendered;target.resumeFade=resumeFade;}
    void snapshotSliceEngine(){if(!contentionEngine||stretchMode)return;auto& target=*contentionEngine;
    target.stretchAnchorEnabled=stretchAnchorEnabled;target.stretchAnchorSample=stretchAnchorSample;target.stretchAnchorBar=stretchAnchorBar;target.shotNoteCycles=shotNoteCycles;target.shotNoteLoop=shotNoteLoop;target.shotLegato=shotLegato;target.shotMono=shotMono;target.shotVelocity=shotVelocity;target.shotLoop=shotLoop;target.shotZeroSnap=shotZeroSnap;target.shotStart=shotStart;target.shotEnd=shotEnd;target.shotLoopStart=shotLoopStart;target.shotLoopEnd=shotLoopEnd;target.shotCrossfadeMs=shotCrossfadeMs;target.shotLoopFade=shotLoopFade;target.shotLoopTravel=shotLoopTravel;target.shotFilterFade=shotFilterFade;target.shotFilterSlope=shotFilterSlope;target.shotFilterCutoff=shotFilterCutoff;target.shotFilterAudition=shotFilterAudition;target.shotAuditionVoice=shotAuditionVoice;target.oneShotMode=oneShotMode;target.rootOctave=rootOctave;target.shotOctaveShift=shotOctaveShift;target.shotEnvelope=shotEnvelope;target.shots=shots;target.shotCursor=shotCursor;target.audio=audio;target.slices=slices;target.sourceBpm=sourceBpm;target.sourceBeats=sourceBeats;target.sourceBeatsPerBar=sourceBeatsPerBar;target.trimStart=trimStart;target.trimEnd=trimEnd;target.startMarker=startMarker;target.gridStart=gridStart;target.regionTiming=regionTiming;target.regionBeatCount=regionBeatCount;target.independentGrid=independentGrid;target.playMode=playMode;target.normaliseEnabled=normaliseEnabled;target.normaliseLufs=normaliseLufs;target.targetLufs=targetLufs;target.pitchSemitones=pitchSemitones;target.keyLocked=keyLocked;target.keyAlternate=keyAlternate;target.loopKey=loopKey;target.targetKey=targetKey;target.padMuted=padMuted;target.padSolo=padSolo;target.padGainDb=padGainDb;target.padOutput=padOutput;target.chromaticMapping=chromaticMapping;target.combinedTiming=combinedTiming;target.quantizeEnabled=quantizeEnabled;target.quantizeEarly=quantizeEarly;target.quantizeAmount=quantizeAmount;target.rhythmGrid=rhythmGrid;target.lateWindow=lateWindow;target.swingEnabled=swingEnabled;target.swingAmount=swingAmount;target.randomOther=randomOther;target.events=events;target.eventCount=eventCount;target.outputRate=outputRate;target.configRevision=configRevision;target.externalVoiceRevision=externalVoiceRevision;target.timelineDirty=timelineDirty;target.rhythmDirty=rhythmDirty;target.hostBpm=hostBpm;target.hostNumerator=hostNumerator;target.hostDenominator=hostDenominator;copySliceRuntime(target);}
    void processBlock(juce::AudioBuffer<float>& out,juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals noDenormals;trackPlayer.releaseInactive();struct Finish{Processor& p;juce::AudioBuffer<float>& out;~Finish(){p.finishAudio(out);}}finish{*this,out};out.clear();
        if(trackMode){auto info=getPlayHead()?getPlayHead()->getPosition():juce::Optional<juce::AudioPlayHead::PositionInfo>();double tempo=info&&info->getBpm()?*info->getBpm():trackPlayer.hostTempo.load();hasHost=info.hasValue();hostPlaying=info&&info->getIsPlaying();LibraryPreparation::markPlayback(hostPlaying&&!isNonRealtime());hostBpm=tempo;double ppq=info&&info->getPpqPosition()?*info->getPpqPosition():trackPlayer.expected;bool playing=playMode&&(!info||info->getIsPlaying());limiterActive=normaliseEnabled&&normaliseLufs;trackPlayer.render(out,outputRate,playing,ppq,tempo,!isNonRealtime());midi.clear();return;}
        const juce::ScopedTryLock g(lock);if(g.isLocked())limiterActive=normaliseEnabled&&normaliseLufs&&!oneShotMode;
        if(!g.isLocked()){if(contentionEngine&&contentionEngine->audio&&!contentionEngine->stretchMode){contentionEngine->setPlayHead(getPlayHead());contentionEngine->processBlock(out,midi);usedContention=true;return;}int ramp=juce::jmax(1,(int)(outputRate*.003));for(int ch=0;ch<juce::jmin(2,out.getNumChannels());++ch)for(int n=0;n<out.getNumSamples();++n)out.setSample(ch,n,lastRendered[juce::jmin(ch,1)]*juce::jmax(0.f,1.f-(float)(n+1)/ramp));for(int ch=0;ch<2;++ch)lastRendered[ch]=out.getNumSamples()>0?out.getSample(juce::jmin(ch,out.getNumChannels()-1),out.getNumSamples()-1):0;resumeFade=ramp;midi.clear();return;}
        if(usedContention){if(!stretchMode&&contentionEngine&&(configRevision==contentionEngine->configRevision||(oneShotMode&&contentionEngine->oneShotMode))&&audio==contentionEngine->audio&&externalVoiceRevision==contentionEngine->externalVoiceRevision)contentionEngine->copySliceRuntime(*this);else timelineDirty=true;usedContention=false;}
        juce::Optional<juce::AudioPlayHead::PositionInfo> position;
        if (auto* head = getPlayHead()) position = head->getPosition();
        hasHost = position.hasValue();hostPlaying = position && position->getIsPlaying();LibraryPreparation::markPlayback(hostPlaying&&!isNonRealtime());
        if (position) {
            if (auto bpm = position->getBpm()) if (*bpm > 0) hostBpm = *bpm;
            if (auto meter = position->getTimeSignature()) if (meter->numerator > 0 && meter->denominator > 0) { hostNumerator = meter->numerator;hostDenominator = meter->denominator; }
        }
        auto ppq = position ? position->getPpqPosition() : juce::Optional<double>();if(ppq)hostBeatPosition=*ppq;
        if(oneShotMode){renderShots(out,midi);snapshotSliceEngine();return;}
        if(stretchMode&&audio){
            if(contentionEngine)contentionEngine->audio.reset();auto request=stretchRequest();stretchWorker.request(request);auto ready=stretchWorker.result();if(ready&&ready->request==request&&ready!=playingStretch){if(playingStretch&&playingStretch->request.audio==request.audio){previousStretch=playingStretch;stretchBlend=0;}else{previousStretch.reset();stretchBlend=1;stretchFade=0;}playingStretch=ready;}
            bool play=playMode&&hostPlaying&&ppq&&playingStretch&&playingStretch->request.audio==request.audio;
            const bool anchored=stretchAnchorEnabled&&!warpMode&&!trackMode;
            auto renderedBeats=[](const StretchWorker::Request& r){return (r.end-r.begin)/r.audio->rate*r.tempo/60;};
            auto playbackLength=[&](const StretchWorker::Request& r){return (r.end-(anchored?r.start:r.begin))/r.audio->rate*r.tempo/60;};
            auto playbackBeat=[&](const StretchWorker::Request& r,double hostBeat){
                if(anchored)return hostBeat+(stretchAnchorSample-r.start)/r.audio->rate*r.tempo/60-(stretchAnchorBar-1)*hostNumerator*4./hostDenominator;
                double length=renderedBeats(r),beat=std::fmod(hostBeat,length);return beat<0?beat+length:beat;
            };
            double length=play?renderedBeats(playingStretch->request):0;
            if(play&&length>0){
                double stretchNormalise=normaliseGain();auto& buffer=playingStretch->buffer;double duration=playbackLength(playingStretch->request);
                double activeFrames=buffer.getNumSamples()*duration/length;
                int lastFrame=anchored?juce::jlimit(0,buffer.getNumSamples()-1,(int)std::ceil(activeFrames)-1):buffer.getNumSamples()-1;
                for(int n=0;n<out.getNumSamples();++n){
                    double hostBeat=*ppq+n*hostBpm/60/outputRate,beat=playbackBeat(playingStretch->request,hostBeat);
                    if(anchored&&(beat<0||beat>=duration)){stretchFade=0;continue;}
                    double point=beat/length*buffer.getNumSamples();int a=juce::jlimit(0,lastFrame,(int)point),b=anchored?juce::jmin(a+1,lastFrame):(a+1)%buffer.getNumSamples();float fraction=(float)(point-a);
                    currentNormaliseGain+=(stretchNormalise-currentNormaliseGain)*(1-std::exp(-1/(outputRate*.02)));
                    float seam=juce::jmin(1.f,(float)(point/(audio->rate*.003)),(float)((activeFrames-point)/(audio->rate*.003)));
                    stretchFade=juce::jmin(1.f,stretchFade+1.f/(float)(outputRate*.005));stretchBlend=juce::jmin(1.f,stretchBlend+1.f/(float)(outputRate*.02));
                    for(int ch=0;ch<juce::jmin(2,out.getNumChannels());++ch){
                        auto* values=buffer.getReadPointer(juce::jmin(ch,buffer.getNumChannels()-1));float value=values[a]+fraction*(values[b]-values[a]);
                        if(previousStretch&&stretchBlend<1){
                            auto& old=previousStretch->buffer;double oldLength=renderedBeats(previousStretch->request),oldBeat=playbackBeat(previousStretch->request,hostBeat),oldDuration=playbackLength(previousStretch->request);float prior=0;
                            if(!anchored||(oldBeat>=0&&oldBeat<oldDuration)){
                                double pointOld=oldBeat/oldLength*old.getNumSamples();int lastOld=anchored?juce::jlimit(0,old.getNumSamples()-1,(int)std::ceil(old.getNumSamples()*oldDuration/oldLength)-1):old.getNumSamples()-1;
                                int oa=juce::jlimit(0,lastOld,(int)pointOld),ob=anchored?juce::jmin(oa+1,lastOld):(oa+1)%old.getNumSamples();auto* samples=old.getReadPointer(juce::jmin(ch,old.getNumChannels()-1));prior=samples[oa]+(float)(pointOld-oa)*(samples[ob]-samples[oa]);
                            }value=prior+(value-prior)*stretchBlend;
                        }out.setSample(ch,n,value*stretchFade*seam*(float)currentNormaliseGain);
                    }
                }
                double beat=playbackBeat(playingStretch->request,*ppq+juce::jmax(0,out.getNumSamples()-1)*hostBpm/60/outputRate);
                voices[0].active=!anchored||(beat>=0&&beat<duration);voices[0].automatic=true;
                voices[0].pos=anchored?juce::jlimit((double)regionStart(),(double)regionEnd(),playingStretch->request.start+beat/playingStretch->request.tempo*60*audio->rate):regionStart()+std::fmod(startMarker-regionStart()+beat/length*(regionEnd()-regionStart()),regionEnd()-regionStart());voices[0].start=regionStart();voices[0].end=regionEnd();
            }else{stretchFade=0;voices[0].active=false;}
            for(int ch=0;ch<2;++ch)lastRendered[ch]=out.getNumSamples()?out.getSample(juce::jmin(ch,out.getNumChannels()-1),out.getNumSamples()-1):0;wasPlaying=hostPlaying;midi.clear();return;
        }
        const bool automatic = playMode && hostPlaying && ppq && audio;
        double beatsPerSample = hostBpm/60/outputRate;
        bool discontinuity = !wasPlaying || timelineDirty || (ppq && std::abs(*ppq-expectedHostPpq) > beatsPerSample*2+1e-6);
        if (hostPlaying && (!wasPlaying || (ppq && *ppq < lastPpq))) resetCycles();
        if (timelineDirty || rhythmDirty) {
            rebuildTimeline();
            if(automatic&&!discontinuity&&rhythmDirty){
                double length=loopBeats(),cycle=std::floor(*ppq/length),phase=*ppq-cycle*length;
                if(cycle!=firedCycle){firedCount=0;firedCycle=cycle;}
                autoSlice=-1;autoCycle=cycle;
                // Timing edits affect future onsets; never replay an onset already passed.
                for(int i=0;i<eventCount&&events[i].beat<=phase+1e-9;++i){
                    autoSlice=i;
                    if(firedCount<260&&std::find(firedStarts.begin(),firedStarts.begin()+firedCount,events[i].start)==firedStarts.begin()+firedCount)
                        firedStarts[firedCount++]=events[i].start;
                }
            }
            rhythmDirty=false;
        }
        if (!automatic) clearAutomaticVoices();
        if (automatic && discontinuity) { clearAutomaticVoices();scheduleAuto(*ppq,true); }
        if (!audio) { if(contentionEngine)contentionEngine->audio.reset();wasPlaying = hostPlaying;midi.clear();return; }
        auto it = midi.begin();double baseStep=audio->rate/outputRate,targetNormalise=normaliseGain(),targetPitch=std::pow(2.,(pitchSemitones+keyTranspose())/12),pitchSmoothing=1-std::exp(-1/(outputRate*.02));
        for (int frame = 0; frame < out.getNumSamples(); ++frame) {
            currentNormaliseGain+=(targetNormalise-currentNormaliseGain)*pitchSmoothing;currentPitchRatio+=(targetPitch-currentPitchRatio)*pitchSmoothing;double step=baseStep*currentPitchRatio;
            double framePpq = ppq ? *ppq+frame*beatsPerSample : 0;
            if (automatic) scheduleAuto(framePpq,false);
            while (it != midi.end() && (*it).samplePosition <= frame) {
                auto msg = (*it).getMessage();
                if (msg.isNoteOn()) trigger(msg.getNoteNumber(),msg.getFloatVelocity());
                ++it;
            }
            for(auto& v:voices) {
                if(v.tailRemaining>0){float gain=(float)v.tailRemaining/v.tailTotal;for(int ch=0;ch<juce::jmin(2,out.getNumChannels());++ch)out.addSample(out.getNumChannels()>=20?v.tailPair*2+ch:ch,frame,v.tail[ch]*gain);--v.tailRemaining;}
                if(!v.active)continue;int a=(int)v.pos;if(a>=v.end){retireVoice(v);continue;}
                float frac=(float)(v.pos-a);double startFade=v.fadeInSamples,endFade=juce::jmax(1.0,audio->rate*.003);
                float fade=juce::jmin(1.0f,(float)((v.age*step+1)/startFade),(float)((v.end-v.pos)/endFade));
                if(v.automatic&&automatic&&v.stopBeat>=0)fade=juce::jmin(fade,(float)juce::jlimit(0.0,1.0,(v.stopBeat-framePpq)/beatsPerSample/(outputRate*.003)));
                double milliseconds=v.age/outputRate*1000+v.envelopeOffset,env=1;
                if(v.envelope.enabled){auto e=v.envelope;if(milliseconds<e.attack&&e.attack>0)env=milliseconds/e.attack;
                    else if(milliseconds<e.attack+e.hold)env=1;
                    else env=e.decay>0?juce::jlimit(0.0,1.0,1-(milliseconds-e.attack-e.hold)/e.decay):0;}
                int category=v.note;bool sliceMuted=false;float desiredLevel=v.level;if(juce::isPositiveAndBelow(v.sliceIndex,(int)slices.size())){auto& s=slices[v.sliceIndex];category=categoryOf(s);sliceMuted=s.muted;desiredLevel=(float)juce::Decibels::decibelsToGain(s.gainDb+(category>=0?padGainDb[category]:0));}if(juce::isPositiveAndBelow(v.destinationIndex,(int)slices.size())){sliceMuted=slices[v.destinationIndex].muted;category=categoryOf(slices[v.destinationIndex]);if(juce::isPositiveAndBelow(v.sliceIndex,(int)slices.size()))desiredLevel=(float)juce::Decibels::decibelsToGain(slices[v.sliceIndex].gainDb+(category>=0?padGainDb[category]:0));}v.level+=juce::jlimit(-.02f,.02f,desiredLevel-v.level);v.outputPair=juce::isPositiveAndBelow(category,128)?padOutput[category]:0;float target=!sliceMuted&&audible(category)?1.f:0.f;v.audibleGain+=juce::jlimit(-1.f/(float)(outputRate*.003),1.f/(float)(outputRate*.003),target-v.audibleGain);
                for(int ch=0;ch<juce::jmin(2,out.getNumChannels());++ch){auto* samples=audio->buffer.getReadPointer(juce::jmin(ch,audio->buffer.getNumChannels()-1));float value=samples[a]+frac*(samples[juce::jmin(a+1,v.end-1)]-samples[a]);
                    float result=value*v.gain*v.level*fade*(float)env*v.audibleGain*(float)currentNormaliseGain;out.addSample(out.getNumChannels()>=20?v.outputPair*2+ch:ch,frame,result);if(ch<2)v.lastOutput[ch]=result;}
                ++v.age;v.pos+=step;if(v.pos>=v.end||(v.envelope.enabled&&milliseconds>=v.envelope.attack+v.envelope.hold+v.envelope.decay))retireVoice(v);
            }
        }
        if(resumeFade>0){int total=juce::jmax(1,(int)(outputRate*.003));for(int n=0;n<out.getNumSamples();++n){float gain=1.f-(float)resumeFade/total;for(int ch=0;ch<out.getNumChannels();++ch)out.setSample(ch,n,out.getSample(ch,n)*gain);if(resumeFade>0)--resumeFade;}}
        for(int ch=0;ch<2;++ch)lastRendered[ch]=out.getNumSamples()>0?out.getSample(juce::jmin(ch,out.getNumChannels()-1),out.getNumSamples()-1):0;
        if (ppq) { lastPpq = *ppq;expectedHostPpq = *ppq+out.getNumSamples()*beatsPerSample; }
        wasPlaying = hostPlaying;timelineDirty = false;midi.clear();snapshotSliceEngine();
    }
    std::function<int()> midiExportChannel;
    juce::MidiFile makeMidi(double bpm) {
        int channel=midiExportChannel?juce::jlimit(1,16,midiExportChannel()):1;
        const juce::ScopedLock g(lock);juce::MidiMessageSequence sequence;bpm = juce::jlimit(1.0,20000.0,bpm);rebuildTimeline(bpm);
        int sliceCount=0;for(auto& slice:slices)if(slice.end>regionStart()&&slice.start<regionEnd())++sliceCount;bool halfExport=sliceCount>128;double length = loopBeats(bpm)*960*(halfExport?.5:1.);int exported=0;
        for (int i = 0; i < eventCount; ++i) {
            auto event = events[i];if(event.start>slices[event.sliceIndex].start)continue; // Continuations are not new MIDI hits.
            if ((!halfExport&&event.note < 0) || slices[event.sliceIndex].muted||!audible(categoryOf(slices[event.sliceIndex]))) continue;
            double tick = event.beat*960,duration = (event.end-event.start)/audio->rate*bpm/60*960;
            if(tick>=length||exported>=128)continue;int note=event.note;if(halfExport&&categoryOf(slices[event.sliceIndex])<0)note=exported;if(note<0)continue;++exported;sequence.addEvent(juce::MidiMessage::noteOn(channel,note,(juce::uint8)100),tick);
            sequence.addEvent(juce::MidiMessage::noteOff(channel,note),juce::jmin(length,tick+juce::jmax(1.0,juce::jmin(120.0,duration*.5))));
        }
        sequence.addEvent(juce::MidiMessage::endOfTrack(),length);sequence.updateMatchedPairs();
        juce::MidiFile midi;midi.setTicksPerQuarterNote(960);midi.addTrack(sequence);timelineDirty = true;return midi;
    }
    bool writeMidi(const juce::File& file,double bpm) {
        auto midi = makeMidi(bpm); juce::FileOutputStream stream(file);
        if (!stream.openedOk()) return false;
        stream.setPosition(0); if (!stream.truncate().wasOk()) return false;
        return midi.writeTo(stream,0);
    }
    std::unique_ptr<juce::XmlElement> configurationXml(bool project = false) {
        const juce::ScopedLock g(lock);auto x = std::make_unique<juce::XmlElement>("SliceGroups");
        x->setAttribute("file",audio ? audio->path : "");x->setAttribute("samples",audio ? audio->buffer.getNumSamples() : 0);x->setAttribute("rate",audio ? audio->rate : 0);
        x->setAttribute("gridStart",gridOrigin());x->setAttribute("gridSnapMode",gridSnapMode);x->setAttribute("quantizeEarly",quantizeEarly);x->setAttribute("quantizeAmount",quantizeAmount);x->setAttribute("independentGrid",independentGrid);x->setAttribute("regionTiming",regionTiming);x->setAttribute("regionBeatCount",regionBeatCount);x->setAttribute("normalise",normaliseEnabled);x->setAttribute("normaliseLufs",normaliseLufs);x->setAttribute("targetLufs",targetLufs);x->setAttribute("pitch",pitchSemitones);x->setAttribute("random",randomOther);x->setAttribute("bpm",sourceBpm);x->setAttribute("strength",detectStrength);
        x->setAttribute("mappingVersion",chromaticMapping?1:0);x->setAttribute("combinedTiming",combinedTiming);x->setAttribute("beats",sourceBeats);x->setAttribute("beatsPerBar",sourceBeatsPerBar);x->setAttribute("playMode",playMode);
        x->setAttribute("startMarker",startMarker);x->setAttribute("startAuto",startAuto);x->setAttribute("trimStart",regionStart());x->setAttribute("trimEnd",regionEnd());x->setAttribute("snap",snapMode);x->setAttribute("boundarySnap",boundarySnap);x->setAttribute("divisions",divisions);x->setAttribute("divisionsPerBar",true);x->setAttribute("sliceMethod",sliceMethod);
        x->setAttribute("trackVinyl",trackVinyl);x->setAttribute("trackExportRaw",trackExportRaw);x->setAttribute("trackExportSafeCPU",trackExportSafeCPU);x->setAttribute("trackExportHQ",trackExportHQ);x->setAttribute("trackStemHQ",trackStemHQ);x->setAttribute("trackStemsEnabled",trackStemsEnabled);for(int i=0;i<4;++i){auto key="trackStem"+juce::String(i);x->setAttribute(key+"Gain",trackStemGainDb[i]);x->setAttribute(key+"Mute",trackStemMute[i]);x->setAttribute(key+"Solo",trackStemSolo[i]);x->setAttribute(key+"Route",trackStemRoute[i]);}x->setAttribute("trackMode",trackMode.load());x->setAttribute("trackLoop",trackLoop);x->setAttribute("trackLoopStart",trackLoopStart);x->setAttribute("trackLoopEnd",trackLoopEnd);x->setAttribute("trackJumpBars",trackJumpBars);if(trackMode){auto* grid=x->createNewChildElement("trackGrid");grid->setAttribute("straight",trackGrid.straight);grid->setAttribute("straightSample",trackGrid.straightAnchor.sample);grid->setAttribute("straightBeat",trackGrid.straightAnchor.beat);for(double beat:trackGrid.removed){auto* deleted=grid->createNewChildElement("removed");deleted->setAttribute("beat",beat);}grid->setAttribute("bpm",trackGrid.bpm);grid->setAttribute("confidence",trackGrid.confidence);grid->setAttribute("beatOffset",trackGrid.beatOffset);grid->setAttribute("downbeatOffset",trackGrid.downbeatOffset);grid->setAttribute("calibrationMs",trackGrid.calibrationMs);grid->setAttribute("sampleOffset",trackGrid.sampleOffset);for(auto a:trackGrid.anchors){auto* anchor=grid->createNewChildElement("anchor");anchor->setAttribute("sample",a.sample);anchor->setAttribute("beat",a.beat);}for(auto pair:{std::make_pair("analysis",&trackGrid.analysed),std::make_pair("manual",&trackGrid.manual)})if(!pair.second->empty()){auto* layer=grid->createNewChildElement(pair.first);for(auto a:*pair.second){auto* node=layer->createNewChildElement("anchor");node->setAttribute("sample",a.sample);node->setAttribute("beat",a.beat);}}}
        x->setAttribute("stretchAnchorEnabled",stretchAnchorEnabled);x->setAttribute("stretchAnchorSample",stretchAnchorSample);if(project)x->setAttribute("stretchAnchorBar",stretchAnchorBar);x->setAttribute("shotNoteCycles",shotNoteCycles);x->setAttribute("shotNoteLoop",shotNoteLoop);x->setAttribute("shotLegato",shotLegato);x->setAttribute("shotMono",shotMono);x->setAttribute("shotVelocity",shotVelocity);x->setAttribute("shotLoop",shotLoop);x->setAttribute("shotZeroSnap",shotZeroSnap);x->setAttribute("shotStart",shotStart);x->setAttribute("shotEnd",shotEnd);x->setAttribute("shotLoopStart",shotLoopStart);x->setAttribute("shotLoopEnd",shotLoopEnd);x->setAttribute("shotCrossfadeMs",shotCrossfadeMs);x->setAttribute("shotLoopFade",shotLoopFade);x->setAttribute("shotLoopTravel",shotLoopTravel);x->setAttribute("shotFilterFade",shotFilterFade);x->setAttribute("shotFilterSlope",shotFilterSlope);x->setAttribute("shotFilterCutoff",shotFilterCutoff);x->setAttribute("oneShot",oneShotMode);x->setAttribute("rootOctave",rootOctave);x->setAttribute("shotOctaveShift",shotOctaveShift);x->setAttribute("shotAttack",shotEnvelope.attack);x->setAttribute("shotDecay",shotEnvelope.decay);x->setAttribute("shotSustain",shotEnvelope.sustain);x->setAttribute("shotRelease",shotEnvelope.release);x->setAttribute("stretchMode",stretchMode);x->setAttribute("warpMode",warpMode);x->setAttribute("warpTempoSync",warpTempoSync);x->setAttribute("warpNativeBpm",warpNativeBpm);if(warpArrangement){auto* warp=x->createNewChildElement("warp");for(auto& chunk:*warpArrangement){auto* c=warp->createNewChildElement("chunk");for(auto& point:chunk.points){auto* p=c->createNewChildElement("point");p->setAttribute("source",point.source);p->setAttribute("beat",point.beat);}}}x->setAttribute("loopKey",loopKey);x->setAttribute("keyAlternate",keyAlternate);if(project){x->setAttribute("keyLocked",keyLocked);x->setAttribute("targetKey",targetKey);}x->setAttribute("gridSliceSwing",gridSliceSwing);x->setAttribute("quantize",quantizeEnabled);x->setAttribute("grid",rhythmGrid);x->setAttribute("lateWindow",lateWindow);x->setAttribute("swing",swingEnabled);x->setAttribute("swingAmount",swingAmount);x->setAttribute("timingMethod",timingMethod);
        if (project) { x->setAttribute("categoryStateLocked",categoryStateLocked);x->setAttribute("repeatCategories",repeatCategories);x->setAttribute("autoCategories",autoCategories);x->setAttribute("loopLengthLocked",loopLengthLocked);x->setAttribute("lockedBars",lockedBars);x->setAttribute("preferredSliceMethod",preferredSliceMethod);x->setAttribute("importDetectStrength",importDetectStrength);x->setAttribute("keepGroove",keepGroove);x->setAttribute("browserPath",browserPath);x->setAttribute("width",editorWidth);x->setAttribute("height",editorHeight);x->setAttribute("browserWidth",browserWidth);x->setAttribute("browserVisible",browserVisible);x->setAttribute("autoSave",autoSave);x->setAttribute("swingLinked",swingLinked);x->setAttribute("swingGroup",swingGroup); }
        for (int n = 0; n < 128; ++n) if (padNames[n].isNotEmpty() || padMuted[n] || padSolo[n]) {
            auto* pad = x->createNewChildElement("pad");pad->setAttribute("note",n);pad->setAttribute("name",padNames[n]);pad->setAttribute("mute",padMuted[n]);pad->setAttribute("solo",padSolo[n]);pad->setAttribute("gainDb",padGainDb[n]);if(project)pad->setAttribute("output",padOutput[n]);
        }
        for (const auto& slice : slices) { auto* child = x->createNewChildElement("slice");child->setAttribute("start",slice.start);child->setAttribute("end",slice.end);child->setAttribute("note",slice.note);child->setAttribute("exclude",slice.excludeRhythm);child->setAttribute("category",slice.category);child->setAttribute("muted",slice.muted);child->setAttribute("gainDb",slice.gainDb);child->setAttribute("replacement",slice.replacement);child->setAttribute("env",slice.envelope.enabled);child->setAttribute("attack",slice.envelope.attack);child->setAttribute("hold",slice.envelope.hold);child->setAttribute("decay",slice.envelope.decay); }
        return x;
    }
    bool applyConfiguration(const juce::XmlElement& x,bool project) {
        const juce::ScopedLock g(lock);if (!audio) return false;
        if (x.hasAttribute("samples") && x.getIntAttribute("samples") != audio->buffer.getNumSamples()) return false;
        std::vector<Slice> restored;
        for (auto* child : x.getChildIterator()) if (child->hasTagName("slice")) {
            int a = child->getIntAttribute("start"),b = child->getIntAttribute("end"),n = child->getIntAttribute("note");
            if (a < 0 || b <= a || b > audio->buffer.getNumSamples() || n < -1 || n >= 128 || restored.size() >= 256 || a != (restored.empty() ? 0 : restored.back().end)) return false;
            restored.push_back({a,b,n,child->getBoolAttribute("exclude"),{child->getBoolAttribute("env"),juce::jlimit(0.0,5000.0,child->getDoubleAttribute("attack",0)),juce::jlimit(0.0,5000.0,child->getDoubleAttribute("hold",0)),juce::jlimit(0.0,10000.0,child->getDoubleAttribute("decay",150))},child->getIntAttribute("category",n>=36&&n<52?n:-1),child->getBoolAttribute("muted"),juce::jlimit(-60.,24.,child->getDoubleAttribute("gainDb",0)),child->getIntAttribute("replacement",-1)});
        }
        if (restored.empty() || restored.back().end != audio->buffer.getNumSamples()) return false;
        for(int i=0;i<(int)restored.size();++i)if(restored[i].replacement==i||restored[i].replacement>=(int)restored.size()||restored[i].replacement<-1)return false;
        chromaticMapping=x.getIntAttribute("mappingVersion",1)!=0;combinedTiming=x.getBoolAttribute("combinedTiming",true);slices = std::move(restored);remapNotes();sourceBpm = juce::jlimit(20.0,400.0,std::isfinite(x.getDoubleAttribute("bpm",120))?x.getDoubleAttribute("bpm",120):120.);sourceBeatsPerBar = juce::jlimit(.25,32.0,x.getDoubleAttribute("beatsPerBar",4));
        normaliseEnabled=x.getBoolAttribute("normalise",false);normaliseLufs=x.getBoolAttribute("normaliseLufs",false);targetLufs=juce::jlimit(-36.,-6.,x.getDoubleAttribute("targetLufs",-18));pitchSemitones=juce::jlimit(-24.,24.,x.getDoubleAttribute("pitch",0));
        sourceBeats = x.getDoubleAttribute("beats",audio->buffer.getNumSamples()/audio->rate*sourceBpm/60);randomOther = x.getBoolAttribute("random");playMode = x.getBoolAttribute("playMode",false);
        detectStrength = juce::jlimit(0.0,100.0,x.getDoubleAttribute("strength",50));timingMethod = x.getStringAttribute("timingMethod","Restored");
        trimStart = juce::jlimit(0,x.getBoolAttribute("warpMode")?juce::jmax(audio->buffer.getNumSamples()-1,x.getIntAttribute("trimEnd",audio->buffer.getNumSamples())-1):audio->buffer.getNumSamples()-1,x.getIntAttribute("trimStart",0));trimEnd = juce::jmax(trimStart+1,x.getIntAttribute("trimEnd",audio->buffer.getNumSamples()));
        gridStart=juce::jlimit(0,INT_MAX,x.getIntAttribute("gridStart",trimStart));gridSnapMode=juce::jlimit(0,2,x.getIntAttribute("gridSnapMode",1));quantizeEarly=x.getBoolAttribute("quantizeEarly",false);quantizeAmount=juce::jlimit(0.,1.,x.getDoubleAttribute("quantizeAmount",1));independentGrid=x.getBoolAttribute("independentGrid",chromaticMapping&&combinedTiming);regionTiming=x.getBoolAttribute("regionTiming",chromaticMapping&&combinedTiming);regionBeatCount=juce::jlimit(.015625,8192.,x.getDoubleAttribute("regionBeatCount",(regionEnd()-regionStart())/audio->rate*sourceBpm/60));updateRegionTempo();
        startAuto=x.getBoolAttribute("startAuto",true);startMarker=x.getIntAttribute("startMarker",trimStart);updateStartAnchor();
        boundarySnap=juce::jlimit(0,2,x.getIntAttribute("boundarySnap",0));snapMode = juce::jlimit(0,2,x.getIntAttribute("snap",1));divisions = juce::jlimit(1,256,x.getIntAttribute("divisions",16));if(!x.getBoolAttribute("divisionsPerBar"))divisions=juce::jlimit(1,256,(int)std::round(divisions/juce::jmax(.015625,loopBeats()/sourceBeatsPerBar)));sliceMethod = x.getIntAttribute("sliceMethod",0);
        stretchAnchorEnabled=x.getBoolAttribute("stretchAnchorEnabled");stretchAnchorSample=juce::jlimit(0.,(double)audio->buffer.getNumSamples()-1,x.getDoubleAttribute("stretchAnchorSample"));if(project)stretchAnchorBar=juce::jlimit(1,100000,x.getIntAttribute("stretchAnchorBar",1));shotStart=juce::jlimit(0,audio->buffer.getNumSamples()-1,x.getIntAttribute("shotStart",0));int savedShotEnd=x.getIntAttribute("shotEnd",-1);shotEnd=savedShotEnd<0?-1:juce::jlimit(shotStart+1,audio->buffer.getNumSamples(),savedShotEnd);shotLoopStart=juce::jlimit(shotStart,shotEndSample()-1,x.getIntAttribute("shotLoopStart",shotStart));int savedLoopEnd=x.getIntAttribute("shotLoopEnd",-1);shotLoopEnd=savedLoopEnd<0?-1:juce::jlimit(shotLoopStart+1,shotEndSample(),savedLoopEnd);shotNoteCycles=juce::jlimit(1./64,64.,x.getDoubleAttribute("shotNoteCycles",1));shotNoteLoop=x.getBoolAttribute("shotNoteLoop");shotLegato=x.getBoolAttribute("shotLegato",true);shotMono=x.getBoolAttribute("shotMono");shotVelocity=juce::jlimit(0.,100.,x.getDoubleAttribute("shotVelocity",100));shotLoop=x.getBoolAttribute("shotLoop");shotZeroSnap=x.getBoolAttribute("shotZeroSnap");shotCrossfadeMs=juce::jlimit(0.,1000.,x.getDoubleAttribute("shotCrossfadeMs"));shotLoopFade=juce::jlimit(0.,100.,x.getDoubleAttribute("shotLoopFade"));shotLoopTravel=juce::jlimit(0.,4.,x.getDoubleAttribute("shotLoopTravel"));shotFilterFade=juce::jlimit(0.,100.,x.getDoubleAttribute("shotFilterFade"));shotFilterSlope=juce::jlimit(1,4,x.getIntAttribute("shotFilterSlope",4));shotFilterCutoff=juce::jlimit(20.,20000.,x.getDoubleAttribute("shotFilterCutoff",20000));shotFilterAudition=false;shotAuditionVoice.choked=true;oneShotMode=x.getBoolAttribute("oneShot");rootOctave=juce::jlimit(-1,7,x.getIntAttribute("rootOctave",3));shotOctaveShift=juce::jlimit(-3,3,x.getIntAttribute("shotOctaveShift",0));shotEnvelope={ (float)juce::jlimit(0.,5.,x.getDoubleAttribute("shotAttack",.005)),(float)juce::jlimit(0.,5.,x.getDoubleAttribute("shotDecay",.1)),(float)juce::jlimit(0.,1.,x.getDoubleAttribute("shotSustain",.8)),(float)juce::jlimit(0.,10.,x.getDoubleAttribute("shotRelease",.15))};stretchMode=x.getBoolAttribute("stretchMode");warpMode=x.getBoolAttribute("warpMode");warpTempoSync=x.getBoolAttribute("warpTempoSync");warpNativeBpm=juce::jlimit(1.,20000.,x.getDoubleAttribute("warpNativeBpm",audio->analysedTiming.bpm>0?audio->analysedTiming.bpm:sourceBpm));warpSelectedChunk=0;warpArrangement.reset();if(auto* warp=x.getChildByName("warp")){WarpArrangement chunks;for(auto* child:warp->getChildIterator())if(child->hasTagName("chunk")){WarpChunk chunk;for(auto* point:child->getChildIterator())if(point->hasTagName("point"))chunk.points.push_back({point->getIntAttribute("source",-1),point->getDoubleAttribute("beat",std::numeric_limits<double>::quiet_NaN())});if(!chunk.valid(audio->buffer.getNumSamples())||chunks.size()>=128)return false;chunks.push_back(std::move(chunk));}warpArrangement=std::make_shared<const WarpArrangement>(std::move(chunks));}if(warpMode){stretchMode=true;initialiseWarp();}loopKey=juce::jlimit(-1,11,x.getIntAttribute("loopKey",-1));keyAlternate=x.getBoolAttribute("keyAlternate");if(project){keyLocked=x.getBoolAttribute("keyLocked");targetKey=juce::jlimit(0,11,x.getIntAttribute("targetKey",5));}gridSliceSwing=juce::jlimit(0.,100.,x.getDoubleAttribute("gridSliceSwing",0));quantizeEnabled = x.getBoolAttribute("quantize");rhythmGrid = juce::jlimit(.03125,4.0,x.getDoubleAttribute("grid",.25));lateWindow = juce::jlimit(0.0,.99,x.getDoubleAttribute("lateWindow",.45));
        swingEnabled = x.getBoolAttribute("swing");swingAmount = juce::jlimit(50.0,75.0,x.getDoubleAttribute("swingAmount",50));padNames=defaultNames;auto heldMute=padMuted,heldSolo=padSolo;bool preservePads=categoryStateLocked&&!project;padMuted.fill(false);padSolo.fill(false);padGainDb.fill(0);if(project)padOutput.fill(0);
        for (auto* child : x.getChildIterator()) if (child->hasTagName("pad")) { int n = child->getIntAttribute("note",-1);if (n >= 0 && n < 128) { padNames[n] = child->getStringAttribute("name").substring(0,40);padMuted[n] = child->getBoolAttribute("mute");padSolo[n] = child->getBoolAttribute("solo");padGainDb[n]=juce::jlimit(-60.,24.,child->getDoubleAttribute("gainDb",0));if(project)padOutput[n]=juce::jlimit(0,9,child->getIntAttribute("output",0)); } }
        if(preservePads){padMuted=heldMute;padSolo=heldSolo;}
        trackMode=x.getBoolAttribute("trackMode");if(!trackMode)trackPlayer.clearSource();if(trackMode&&claimTrack)claimTrack();trackVinyl=x.getBoolAttribute("trackVinyl");trackExportRaw=x.getBoolAttribute("trackExportRaw");trackStemHQ=x.getBoolAttribute("trackStemHQ",trackStemHQ);trackExportSafeCPU=x.getBoolAttribute("trackExportSafeCPU",trackExportSafeCPU);trackExportHQ=x.getBoolAttribute("trackExportHQ",trackExportHQ);trackStemsEnabled=x.getBoolAttribute("trackStemsEnabled",true);for(int i=0;i<4;++i){auto key="trackStem"+juce::String(i);trackStemGainDb[i]=juce::jlimit(-60.,12.,x.getDoubleAttribute(key+"Gain"));trackStemMute[i]=x.getBoolAttribute(key+"Mute");trackStemSolo[i]=x.getBoolAttribute(key+"Solo");trackStemRoute[i]=0;}trackLoop=x.getBoolAttribute("trackLoop");trackLoopStart=x.getDoubleAttribute("trackLoopStart");trackLoopEnd=x.getDoubleAttribute("trackLoopEnd",16);trackJumpBars=juce::jlimit(1,16,x.getIntAttribute("trackJumpBars",4));trackGrid={};trackGridEdited=trackGridReady=false;if(trackMode){if(auto* grid=x.getChildByName("trackGrid")){trackGrid.straight=grid->getBoolAttribute("straight");trackGrid.straightAnchor={grid->getDoubleAttribute("straightSample"),grid->getDoubleAttribute("straightBeat")};for(auto* removed:grid->getChildIterator())if(removed->hasTagName("removed")&&std::isfinite(removed->getDoubleAttribute("beat")))trackGrid.removed.push_back(removed->getDoubleAttribute("beat"));trackGrid.bpm=juce::jlimit(20.,400.,std::isfinite(grid->getDoubleAttribute("bpm",120))?grid->getDoubleAttribute("bpm",120):120.);trackGrid.confidence=grid->getDoubleAttribute("confidence");trackGrid.beatOffset=grid->getDoubleAttribute("beatOffset");trackGrid.downbeatOffset=grid->getDoubleAttribute("downbeatOffset");trackGrid.calibrationMs=grid->getDoubleAttribute("calibrationMs");trackGrid.sampleOffset=grid->getDoubleAttribute("sampleOffset");trackGrid.anchors.clear();for(auto* a:grid->getChildIterator())if(a->hasTagName("anchor")&&trackGrid.anchors.size()<65536){double sample=a->getDoubleAttribute("sample"),beat=a->getDoubleAttribute("beat");if(std::isfinite(sample)&&std::isfinite(beat)&&sample>=0&&sample<audio->buffer.getNumSamples()&&(trackGrid.anchors.empty()||(sample>trackGrid.anchors.back().sample&&beat>trackGrid.anchors.back().beat)))trackGrid.anchors.push_back({sample,beat});}if(trackGrid.anchors.empty())trackGrid.anchors={{0,0}};for(auto pair:{std::make_pair("analysis",&trackGrid.analysed),std::make_pair("manual",&trackGrid.manual)})if(auto* layer=grid->getChildByName(pair.first)){for(auto* node:layer->getChildIterator()){double sample=node->getDoubleAttribute("sample"),beat=node->getDoubleAttribute("beat");double actual=sample+(juce::String(pair.first)=="manual"?trackGrid.sampleOffset+trackGrid.downbeatOffset:0);if(std::isfinite(sample)&&std::isfinite(beat)&&actual>=0&&actual<audio->buffer.getNumSamples()&&pair.second->size()<65536)pair.second->push_back({sample,beat});}}if(!trackGrid.analysed.empty()&&!trackGrid.rebuild()){trackGrid.analysed.clear();trackGrid.manual.clear();}trackGridEdited=trackGridReady=true;}oneShotMode=warpMode=false;stretchMode=true;serviceTrack();}
        if (project) {categoryStateLocked=x.getBoolAttribute("categoryStateLocked");repeatCategories=x.getBoolAttribute("repeatCategories");autoCategories=x.getBoolAttribute("autoCategories");loopLengthLocked=x.getBoolAttribute("loopLengthLocked");lockedBars=x.getDoubleAttribute("lockedBars",1);preferredSliceMethod=x.getIntAttribute("preferredSliceMethod",sliceMethod);importDetectStrength=x.getDoubleAttribute("importDetectStrength",detectStrength);keepGroove=x.getBoolAttribute("keepGroove",false);browserPath=x.getStringAttribute("browserPath",browserPath);editorWidth = juce::jlimit(x.getBoolAttribute("browserVisible",true)?1000:620,2600,x.getIntAttribute("width",1360));editorHeight = juce::jlimit(600,1800,x.getIntAttribute("height",1000));browserWidth = juce::jlimit(240,1800,x.getIntAttribute("browserWidth",320));browserVisible = x.getBoolAttribute("browserVisible",true);autoSave = x.getBoolAttribute("autoSave",false);swingLinked = x.getBoolAttribute("swingLinked",true);swingGroup = x.getStringAttribute("swingGroup","A");if(swingGroup!="A"&&swingGroup!="B"&&swingGroup!="C"&&swingGroup!="D"){swingGroup="A";swingLinked=false;} }
        clearVoices();resetCycles();return true;
    }
    std::function<std::unique_ptr<juce::XmlElement>()> rackPreset;
    std::function<bool(const juce::XmlElement&,std::function<bool()>)> rackRecall;
    bool savePreset(juce::String name,bool overwrite=false){
        if(rackPreset){auto xml=rackPreset();return LibraryStore::savePreset(name,*xml,overwrite);}
        std::unique_ptr<juce::XmlElement> xml;
        {const juce::ScopedLock g(lock);if(!audio||audio->path.isEmpty())return false;xml=configurationXml();
            xml->setAttribute("loopLengthLocked",loopLengthLocked);xml->setAttribute("lockedBars",lockedBars);}
        return LibraryStore::savePreset(name,*xml,overwrite);
    }
    bool installPreset(std::shared_ptr<const AudioData> next,const juce::XmlElement& xml,uint64_t serial=0){
        if(xml.hasTagName("SliceGroupsRack")&&rackRecall){if(serial&&serial!=loadGeneration.load())return false;return rackRecall(xml,[this,serial]{return serial&&serial!=loadGeneration.load();});}
        if(!next||xml.getTagName()!="SliceGroups"||xml.getIntAttribute("samples")!=next->buffer.getNumSamples()||std::abs(xml.getDoubleAttribute("rate")-next->rate)>.01)return false;
        const juce::ScopedLock g(lock);if(serial&&serial!=loadGeneration.load())return false;
        auto previous=audio;auto heldMuted=padMuted,heldSolo=padSolo;bool holdStates=categoryStateLocked;audio=next;
        if(!applyConfiguration(xml,false)){audio=previous;return false;}
        if(holdStates){padMuted=heldMuted;padSolo=heldSolo;}++detectionGeneration;preferredSliceMethod=sliceMethod;importDetectStrength=detectStrength;
        loopLengthLocked=xml.getBoolAttribute("loopLengthLocked");lockedBars=xml.getDoubleAttribute("lockedBars",loopBeats()/sourceBeatsPerBar);
        swingLinked=false;hasSavedConfig=false;edited();return true;
    }
    bool queueAutoSave(bool switching=false,bool manual=false){
        juce::String path,xml,hash;uint64_t revision;std::shared_ptr<const AudioData> identity;
        {const juce::ScopedLock g(lock);if((!manual&&(!autoSave||!libraryDirty))||!audio||audio->path.isEmpty()||editDepth>0||(!switching&&!manual&&autoSaveBusy.load()))return false;++autoSaveBusy;identity=audio;path=audio->path;hash=audio->contentHash;revision=configRevision;xml=configurationXml()->toString();}
        autoSavePool.addJob([this,path,xml,hash,revision,identity,manual]{
            bool ok=LibraryStore::saveLoop(juce::File(path),xml,hash);
            if(ok)if(auto config=juce::parseXML(xml)){int explicitKey=LibraryStore::fileKey(juce::File(path),hash),savedKey=config->getIntAttribute("loopKey",-1);if(explicitKey!=-2&&explicitKey!=savedKey)LibraryStore::setFileKey(juce::File(path),savedKey,hash);}
            bool correction=false;{const juce::ScopedLock g(lock);if(ok&&audio==identity){hasSavedConfig=true;if(configRevision==revision){libraryDirty=false;if(manual&&editDepth==0){historyBaseline=historyState();historyAudio=audio;}}correction=manual&&trackMode;} }
            if(correction)captureTrackCorrection("save-track",identity);--autoSaveBusy;if(manual){manualSaveOK=ok;++manualSaveCompleted;}
        });return true;
    }
    bool saveLoop() {
        juce::String path,xml,hash;uint64_t revision;
        { const juce::ScopedLock g(lock);if (!audio || audio->path.isEmpty()) return false;path = audio->path;hash=audio->contentHash;revision = configRevision;xml = configurationXml()->toString(); }
        bool ok = LibraryStore::saveLoop(juce::File(path),xml,hash);if(ok&&LibraryStore::fileKey(juce::File(path),hash)!=-2){auto config=juce::parseXML(xml);LibraryStore::setFileKey(juce::File(path),config->getIntAttribute("loopKey",-1),hash);}
        if (ok) { const juce::ScopedLock g(lock);if (audio && audio->path == path) { hasSavedConfig = true;if(trackMode)captureTrackCorrection("save-track");if (revision == configRevision) {libraryDirty = false;if(editDepth==0){historyBaseline=historyState();historyAudio=audio;}} } }
        return ok;
    }
    void getStateInformation(juce::MemoryBlock& block) override { auto xml = configurationXml(true);copyXmlToBinary(*xml,block); }
    void setStateInformation(const void* data,int size) override {
        if (auto xml = getXmlFromBinary(data,size)) {
            bool loaded = load(juce::File(xml->getStringAttribute("file")),false);
            bool saved = loaded && LibraryStore::savedLoop(juce::File(xml->getStringAttribute("file"))) != nullptr;
            const juce::ScopedLock g(lock);if (!loaded) { audio.reset();slices.clear();clearVoices();return; }
            if (applyConfiguration(*xml,true)) { libraryDirty = true;hasSavedConfig = saved;resetHistory(); }
            if (juce::MessageManager::getInstanceWithoutCreating()) startTimer(200);
        }
    }
};
