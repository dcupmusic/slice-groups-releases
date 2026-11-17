#pragma once
#include "Sequence.h"
#include <random>
#include <numeric>
namespace MidiTools522 {
constexpr int ticks=192;
struct Token {double beat=0,length=.25;int pitch=36;};
struct State {
 bool visible=false,transform=false,fillProfile=false,hasGenerated=false,lastWasTransform=false;uint64_t seed=1,baseSeed=1,patternSeed=1,outputRevision=0,generatedIdentity=0;SequenceClip input;std::vector<Token> base;
 std::array<bool,12> enabled{true,false,false,false,false,false,false,false,false,false,false,false};
 std::array<double,12> main{100,0,0,0,0,0,0,0,0,0,0,0},fill{100,0,0,0,0,0,0,0,0,0,0,0};
 std::map<juce::String,double> values{{"root",0},{"octave",2},{"mainLow",0},{"mainHigh",0},{"fillLow",0},{"fillHigh",1},{"phrase",4},{"density",50},{"base",.25},{"duration1",.5},{"duration2",1},{"duration3",.5},{"chance1",0},{"chance2",0},{"chance3",0},{"triplet",0},{"gate",80},{"gateRandom",0},{"velocity",100},{"velocityRandom",0},{"slop",0},{"rhythmVariation",0},{"pitchVariation",0},{"bDensity",0},{"bDuration",50},{"fillAmount",0},{"firstRoot",100},{"rootAmount",0},{"rootSpread",100},{"mainRange",12},{"fillRange",24},{"mainOctaveProbability",0},{"fillOctaveProbability",0},{"avoidBeats",0},{"blockCount",3},{"blockUnit",.5},{"blocks",0},{"rhythm",0},{"pitch",0}};
 std::vector<int> roots{0},blockOrder;
 double get(const juce::String& key)const{auto i=values.find(key);return i==values.end()?0:i->second;}
 void write(juce::XmlElement& parent)const{auto* x=parent.createNewChildElement("midiTools522");x->setAttribute("hasGenerated",hasGenerated);x->setAttribute("lastWasTransform",lastWasTransform);x->setAttribute("visible",visible);x->setAttribute("transform",transform);x->setAttribute("fillProfile",fillProfile);x->setAttribute("seed",juce::String(seed));x->setAttribute("baseSeed",juce::String(baseSeed));x->setAttribute("patternSeed",juce::String(patternSeed));for(auto& v:values)x->setAttribute(v.first,v.second);for(int i=0;i<12;++i){auto* n=x->createNewChildElement("key");n->setAttribute("pc",i);n->setAttribute("enabled",enabled[i]);n->setAttribute("main",main[i]);n->setAttribute("fill",fill[i]);}for(auto root:roots)x->createNewChildElement("root")->setAttribute("offset",root);for(auto t:base){auto* b=x->createNewChildElement("token");b->setAttribute("beat",t.beat);b->setAttribute("length",t.length);b->setAttribute("pitch",t.pitch);}auto* source=x->createNewChildElement("source");input.write(*source);}
 void read(const juce::XmlElement* x){if(!x)return;State s;s.hasGenerated=x->getBoolAttribute("hasGenerated");s.lastWasTransform=x->getBoolAttribute("lastWasTransform");s.visible=x->getBoolAttribute("visible");s.transform=x->getBoolAttribute("transform");s.fillProfile=x->getBoolAttribute("fillProfile");s.seed=(uint64_t)x->getStringAttribute("seed","1").getLargeIntValue();s.baseSeed=(uint64_t)x->getStringAttribute("baseSeed","1").getLargeIntValue();s.patternSeed=(uint64_t)x->getStringAttribute("patternSeed",juce::String(s.seed)).getLargeIntValue();for(auto& v:s.values){auto n=x->getDoubleAttribute(v.first,v.second);if(std::isfinite(n))v.second=n;}s.roots.clear();for(auto* n:x->getChildIterator()){if(n->hasTagName("key")){int i=n->getIntAttribute("pc",-1);if(i<0||i>=12)continue;s.enabled[i]=n->getBoolAttribute("enabled");s.main[i]=juce::jlimit(0.,100.,n->getDoubleAttribute("main"));s.fill[i]=juce::jlimit(0.,100.,n->getDoubleAttribute("fill"));}else if(n->hasTagName("root")&&s.roots.size()<512)s.roots.push_back(juce::jlimit(0,11,n->getIntAttribute("offset")));else if(n->hasTagName("token")&&s.base.size()<8192){double b=n->getDoubleAttribute("beat"),l=n->getDoubleAttribute("length");if(std::isfinite(b)&&std::isfinite(l)&&b>=0&&l>0)s.base.push_back({b,l,juce::jlimit(0,127,n->getIntAttribute("pitch",36))});}}s.values["phrase"]=4*juce::jlimit(1.,16.,std::round(s.get("phrase")/4));if(!x->hasAttribute("hasGenerated"))s.hasGenerated=!s.base.empty();if(s.roots.empty())s.roots={0};if(auto* source=x->getChildByName("source"))s.input.read(source->getChildByName("sequence"));*this=std::move(s);}
};
struct Random {std::mt19937_64 engine;explicit Random(uint64_t seed):engine(seed){}double unit(){return std::generate_canonical<double,53>(engine);}bool chance(double p){return unit()*100<p;}int integer(int n){return n>0?(int)(engine()%(uint64_t)n):0;}template<class T>void shuffle(std::vector<T>& v){std::shuffle(v.begin(),v.end(),engine);}};
inline int pc(int n){return (n%12+12)%12;}
inline std::vector<int> palette(const State& s){std::vector<int> p;for(int i=0;i<12;++i)if(s.enabled[i])p.push_back(i);return p;}
inline int indexOf(const std::vector<int>& p,int n){auto i=std::find(p.begin(),p.end(),pc(n));return i==p.end()?-1:(int)(i-p.begin());}
inline int rootShift(const State& s,int offset){auto p=palette(s);int a=indexOf(p,(int)s.get("root")),b=indexOf(p,(int)s.get("root")+offset);if(a<0||b<0)throw std::runtime_error("Base and phrase roots must be enabled on the piano.");return (b-a+(int)p.size())%(int)p.size();}
inline int mapSteps(const State& s,int note,int shift){auto p=palette(s);if(p.empty())throw std::runtime_error("Enable at least one piano note.");int nearest=0,distance=99,accidental=0;for(int i=0;i<(int)p.size();++i){int d=pc(note)-p[i];while(d>6)d-=12;while(d< -6)d+=12;if(std::abs(d)<distance){nearest=i;distance=std::abs(d);accidental=d;}}int next=p[(nearest+shift+(int)p.size()*2)%p.size()]+accidental;int result=(note/12)*12+pc(next);while(result>127)result-=12;while(result<0)result+=12;return result;}
inline int mapPitch(const State& s,int note,int offset){return mapSteps(s,note,rootShift(s,offset));}
inline std::vector<int> registerCandidates(const State& s,int note,int offset,bool fill){int low=(int)s.get(fill?"fillLow":"mainLow"),high=(int)s.get(fill?"fillHigh":"mainHigh");if(low>high)std::swap(low,high);double range=s.get(fill?"fillRange":"mainRange");int root=((int)s.get("octave")+1)*12+(int)s.get("root")+offset;std::vector<int> out;for(int oct=(int)s.get("octave")+1+low;oct<=(int)s.get("octave")+1+high;++oct){int n=oct*12+pc(note);if(n>=0&&n<=127&&std::abs(n-root)<=range+.001)out.push_back(n);}return out;}
inline int weightedPitch(const State& s,int offset,bool fill,Random& random){auto p=palette(s);if(p.empty())throw std::runtime_error("Enable at least one piano note.");int shift=rootShift(s,offset);std::array<double,12> weights{};double sum=0;auto& profile=fill?s.fill:s.main;for(int i=0;i<(int)p.size();++i){double w=profile[p[i]];int n=p[(i+shift)%p.size()];if(!registerCandidates(s,n,offset,fill).empty()){weights[n]=w;sum+=w;}}if(sum<=0)throw std::runtime_error(fill?"Give an enabled Fill note a probability.":"Give an enabled Main note a probability.");double slop=juce::jlimit(0.,1.,s.get("slop")/100);if(random.unit()<slop){std::vector<int> allowed;int reach=juce::jmax(1,(int)std::ceil(slop*6));for(int n=0;n<12;++n){int d=6;for(int k:p)d=juce::jmin(d,juce::jmin(pc(n-k),pc(k-n)));if(d<=reach&&!registerCandidates(s,n,offset,fill).empty())allowed.push_back(n);}return allowed[random.integer((int)allowed.size())];}double choice=random.unit()*sum;for(int n=0;n<12;++n){choice-=weights[n];if(choice<0)return n;}return p.front();}
inline int fitRegister(const State& s,int note,bool fill,Random& random,bool choose,int offset=0){auto candidates=registerCandidates(s,note,offset,fill);if(candidates.empty())candidates=registerCandidates(s,(int)s.get("root")+offset,offset,fill);if(candidates.empty())throw std::runtime_error("No notes fit the semitone and octave ranges.");if(choose){int home=((int)s.get("octave")+1)*12+pc(candidates.front());auto preferred=std::min_element(candidates.begin(),candidates.end(),[&](int a,int b){return std::abs(a-home)<std::abs(b-home);});int base=*preferred;std::vector<int> others;for(int n:candidates)if(n!=base)others.push_back(n);if(!others.empty()&&random.chance(s.get(fill?"fillOctaveProbability":"mainOctaveProbability")))return others[random.integer((int)others.size())];return base;}return *std::min_element(candidates.begin(),candidates.end(),[&](int a,int b){return std::abs(a-note)<std::abs(b-note);});}
inline int registerPitch(const State& s,int note,bool fill,Random& random,int offset=0){return fitRegister(s,note,fill,random,true,offset);}
inline int rhythmicGrid525(const State& s){double factor=s.get("triplet")>.5?2./3.:1.;int grid=ticks;for(int i=0;i<4;++i){double length=s.get(i==0?juce::String("base"):"duration"+juce::String(i));grid=std::gcd(grid,juce::jmax(1,(int)std::round(length*factor*ticks)));}return juce::jmax(1,grid);}
inline double fillStart525(const State& s,double absolute,double span){double strength=juce::jlimit(0.,1.,s.get("fillAmount")/100),reach=juce::jmin(span,1.+juce::jmax(0.,span-1)*strength),grid=rhythmicGrid525(s)/(double)ticks;double first=std::ceil(absolute/grid-1e-8)*grid;double start=std::floor((absolute+span-reach)/grid+1e-8)*grid;return juce::jlimit(0.,span,juce::jmax(first,start)-absolute);}
inline std::vector<Token> rhythm(const State& s,double span,double density,Random& random,double durationBias=0){
 double factor=s.get("triplet")>.5?2./3.:1.;
 std::vector<int> lengths{juce::jmax(1,(int)std::round(s.get("base")*factor*ticks))};
 std::vector<double> weights;double totalAlt=0;
 for(int n=1;n<=3;++n)totalAlt+=s.get("chance"+juce::String(n));
 weights.push_back(juce::jmax(0.,100-totalAlt));
 for(int n=1;n<=3;++n){lengths.push_back(juce::jmax(1,(int)std::round(s.get("duration"+juce::String(n))*factor*ticks)));weights.push_back(s.get("chance"+juce::String(n)));}
 int spanTicks=juce::jmax(0,(int)std::floor(span*ticks+1e-7)),quantum=rhythmicGrid525(s);
 int budget=juce::jlimit(0,spanTicks,(int)std::round(spanTicks*density/100/quantum)*quantum);
 std::vector<int> chosen;int used=0;double bias=juce::jlimit(-1.,1.,durationBias),amount=std::abs(bias);
 while(budget-used>0&&chosen.size()<8192){
  std::array<double,4> availableWeights{};double baseline=0;int extreme=bias<0?INT_MAX:0;
  for(int i=0;i<4;++i)if(lengths[i]<=budget-used){baseline+=weights[i];extreme=bias<0?juce::jmin(extreme,lengths[i]):juce::jmax(extreme,lengths[i]);}
  double available=0;
  for(int i=0;i<4;++i)if(lengths[i]<=budget-used){
   // Continuous probability blend toward the shortest/longest selected value.
   // At the ends, choose the best value that fits the remaining budget.
   double w=weights[i]*(1-amount);
   if(amount>0&&lengths[i]==extreme)w+=juce::jmax(100.,baseline)*amount;
   available+=availableWeights[i]=w;
  }
  if(available<=0)break;double pick=random.unit()*available;int length=0;
  for(int i=0;i<4;++i){pick-=availableWeights[i];if(pick<0){length=lengths[i];break;}}
  if(length<=0)break;chosen.push_back(length);used+=length;
 }
 random.shuffle(chosen);std::vector<int> gaps(chosen.size()+1);
 int rest=(spanTicks-used)/quantum;for(int i=0;i<rest;++i)++gaps[random.integer((int)gaps.size())];
 std::vector<Token> out;int pos=0;for(int i=0;i<(int)chosen.size();++i){pos+=gaps[i]*quantum;out.push_back({pos/(double)ticks,chosen[i]/(double)ticks,36});pos+=chosen[i];}return out;
}
inline void randomizeRoots(State& s,double duration){auto p=palette(s);if(p.empty())throw std::runtime_error("Enable at least one piano note.");Random random(++s.seed);int count=juce::jmax(1,(int)std::ceil(duration/juce::jmax(.25,s.get("phrase"))));if(count>512)throw std::runtime_error("Use a longer phrase (512-phrase limit).");std::vector<int> offsets;int radius=(int)std::floor(11*s.get("rootSpread")/100);for(int n=0;n<=radius;++n)if(s.enabled[pc((int)s.get("root")+n)])offsets.push_back(n);if(offsets.empty())throw std::runtime_error("Enable the base root on the piano.");s.roots.resize(count);for(auto& root:s.roots)root=offsets[random.integer((int)offsets.size())];}
enum class Action {generate,vary,roots,transform,refresh,refreshTransform};
struct PhraseNote524 {double beat=0,allocated=.25,length=.2;int pitch=36,velocity=100;};
inline PhraseNote524 resolvePhrase524(const State& s,Token t,int index,bool fill,Random& random){
 PhraseNote524 n;n.beat=t.beat;n.allocated=t.length;
 bool root=random.chance(s.get("rootAmount"))||(index==0&&random.chance(s.get("firstRoot")));
 n.pitch=registerPitch(s,root?(int)s.get("root"):weightedPitch(s,0,fill,random),fill,random);
 n.length=t.length*juce::jlimit(1.,100.,s.get("gate")+(random.unit()*2-1)*s.get("gateRandom"))/100;
 n.velocity=(int)std::round(juce::jlimit(1.,127.,s.get("velocity")+(random.unit()*2-1)*s.get("velocityRandom")));
 return n;
}
inline void varyRhythm524(std::vector<PhraseNote524>& notes,double span,double amount,Random& random){
 if(amount<=0||notes.empty())return;
 // Reorder selected duration values and rests, retaining pitch order and occupied time.
 std::vector<double> gaps;double end=0;
 for(auto n:notes){gaps.push_back(juce::jmax(0.,n.beat-end));end=n.beat+n.allocated;}
 gaps.push_back(juce::jmax(0.,span-end));
 std::vector<int> durations;std::vector<double> lengths;
 for(int i=0;i<(int)notes.size();++i)if(random.chance(amount)){durations.push_back(i);lengths.push_back(notes[i].allocated);}
 random.shuffle(lengths);for(int i=0;i<(int)durations.size();++i){auto& note=notes[durations[i]];double gate=note.length/note.allocated;note.allocated=lengths[i];note.length=note.allocated*gate;}
 std::vector<int> indices;std::vector<double> chosen;
 for(int i=0;i<(int)gaps.size();++i)if(random.chance(amount)){indices.push_back(i);chosen.push_back(gaps[i]);}
 random.shuffle(chosen);for(int i=0;i<(int)indices.size();++i)gaps[indices[i]]=chosen[i];
 double beat=0;for(int i=0;i<(int)notes.size();++i){beat+=gaps[i];notes[i].beat=beat;beat+=notes[i].allocated;}
}
inline void avoidBeats525(std::vector<PhraseNote524>& notes,const State& s,double absolute,double start,double end,uint64_t seed){
 double amount=juce::jlimit(0.,100.,s.get("avoidBeats"));if(amount<=0||notes.empty())return;
 int gridTicks=rhythmicGrid525(s);if(gridTicks%ticks==0)return;double grid=gridTicks/(double)ticks;Random random(seed);
 std::vector<int> indices;double remaining=0;for(int i=0;i<(int)notes.size();++i)if(notes[i].beat>=start-1e-8&&notes[i].beat<end-1e-8){indices.push_back(i);remaining+=notes[i].allocated;}
 // Reserve space for following notes, allowing a packed group to move into
 // available rests later in the phrase without dropping or shortening notes.
 if(remaining>end-start+1e-8)return;double cursor=start;auto planned=notes;
 for(int index:indices){auto& note=planned[index];double low=cursor,high=end-remaining;if(high<low-1e-8)return;
  double target=juce::jlimit(low,juce::jmax(low,high),note.beat),onset=absolute+target;
  if(std::abs(onset-std::round(onset))<1e-7&&random.chance(amount)){
   int centre=(int)std::floor(onset/grid);double bestDistance=1e30;std::vector<double> best;
   for(int step=0;step<=ticks;++step){for(int direction:{-1,1}){int cell=centre+direction*step;double candidate=cell*grid,relative=candidate-absolute;
     if(relative<low-1e-8||relative>high+1e-8||std::abs(candidate-std::round(candidate))<1e-7)continue;
     double distance=std::abs(candidate-onset);if(distance<bestDistance-1e-8){bestDistance=distance;best={relative};}else if(std::abs(distance-bestDistance)<1e-8&&std::find(best.begin(),best.end(),relative)==best.end())best.push_back(relative);
    }if(!best.empty()&&step*grid>bestDistance+grid)break;
   }
   if(!best.empty())target=best[random.integer((int)best.size())];
  }
  note.beat=target;cursor=target+note.allocated;remaining-=note.allocated;
 }
 notes=std::move(planned);
}
inline SequenceClip generated(const SequenceClip& source,State& s,Action action){
 auto out=source;bool refresh=action==Action::refresh;
 double begin=source.loopStart,duration=source.beats(),phrase=4*juce::jlimit(1.,512.,std::round(s.get("phrase")/4));
 int count=juce::jmax(1,(int)std::ceil(duration/phrase));if(count>512)throw std::runtime_error("Use a longer phrase (512-phrase limit).");
 s.roots.resize(count,0);if(palette(s).empty())throw std::runtime_error("Enable piano notes before generating.");for(int root:s.roots)rootShift(s,root);
 if(action==Action::roots){
  auto previous=s.roots;randomizeRoots(s,duration);Random random(s.seed);
  for(auto& n:out.notes)if(n.beat>=begin&&n.beat<source.loopEnd()){
   int i=juce::jlimit(0,count-1,(int)((n.beat-begin)/phrase));int old=previous[i],next=s.roots[i];
   n.pitch=mapSteps(s,n.pitch,rootShift(s,next)-rootShift(s,old));
   double span=juce::jmin(phrase,duration-i*phrase),strength=s.get("fillAmount")/100;
   bool fill=i==count-1&&strength>0&&n.beat-begin-i*phrase>=fillStart525(s,begin+i*phrase,span)-1e-8;
   n.pitch=fitRegister(s,n.pitch,fill,random,false,next);
  }return out;
 }
 if(!refresh){++s.seed;s.patternSeed=s.seed;if(action==Action::generate||s.base.empty())s.baseSeed=s.seed;}
 double baseSpan=juce::jmin(phrase,duration);
 Random aRhythm(s.baseSeed),aPitch(s.baseSeed^0x7b4a59e3ULL);
 s.base=rhythm(s,baseSpan,s.get("density"),aRhythm);
 std::vector<PhraseNote524> a;
 for(int i=0;i<(int)s.base.size();++i){auto n=resolvePhrase524(s,s.base[i],i,false,aPitch);s.base[i].pitch=n.pitch;a.push_back(n);}
 // Every A and every B share fully resolved decisions, including root chance,
 // octave, Gate Random and Velocity Random. Only B uses the variation seed.
 auto b=a;double densityDelta=juce::jlimit(-100.,100.,s.get("bDensity"));
 double durationBias=(juce::jlimit(0.,100.,s.get("bDuration"))-50)/50;
 if(densityDelta!=0||durationBias!=0){
  Random timing(s.patternSeed^0x51ed270bULL),fallback(s.baseSeed^0x19a13d7ULL);
  auto tokens=rhythm(s,baseSpan,juce::jlimit(0.,100.,s.get("density")+densityDelta),timing,durationBias);b.clear();
  for(int i=0;i<(int)tokens.size();++i){
   auto t=tokens[i];auto n=a.empty()?resolvePhrase524(s,t,i,false,fallback):a[i%a.size()];
   double gate=n.allocated>0?n.length/n.allocated:1;n.beat=t.beat;n.allocated=t.length;n.length=t.length*gate;b.push_back(n);
  }
 }
 Random rhythmRandom(s.patternSeed^0x12e45c91ULL),pitchRandom(s.patternSeed^0x86ab391dULL);
 varyRhythm524(b,baseSpan,s.get("rhythmVariation"),rhythmRandom);
 for(int i=0;i<(int)b.size();++i)if(pitchRandom.chance(s.get("pitchVariation"))){
  auto changed=resolvePhrase524(s,{b[i].beat,b[i].allocated,36},i,false,pitchRandom);b[i].pitch=changed.pitch;
 }
 out.notes.erase(std::remove_if(out.notes.begin(),out.notes.end(),[&](auto n){return n.beat>=begin&&n.beat<source.loopEnd();}),out.notes.end());
 for(int part=0;part<count;++part){
  bool isB=part%2==1;double span=juce::jmin(phrase,duration-part*phrase);auto notes=isB?b:a;
  double strength=juce::jlimit(0.,1.,s.get("fillAmount")/100),fillStart=fillStart525(s,begin+part*phrase,span),reach=span-fillStart;
  double partDensity=juce::jlimit(0.,100.,s.get("density")+(isB?densityDelta:0));
  bool final=part==count-1&&strength>0&&partDensity>0;
  if(final&&reach>1e-8){
   double requestedStart=span-juce::jmin(span,1.+juce::jmax(0.,span-1)*strength),eraseStart=requestedStart<1e-8?0:fillStart;
   notes.erase(std::remove_if(notes.begin(),notes.end(),[&](auto n){return n.beat>=eraseStart;}),notes.end());
   for(auto& n:notes)if(n.beat+n.allocated>eraseStart){double gate=n.length/n.allocated;n.allocated=eraseStart-n.beat;n.length=n.allocated*gate;}
   Random fillTiming((isB?s.patternSeed:s.baseSeed)^0x902fce71ULL),fillPitch((isB?s.patternSeed:s.baseSeed)^0x4b193a0dULL);
   auto extra=rhythm(s,reach,partDensity+(100-partDensity)*strength,fillTiming,isB?durationBias:0);
   for(int i=0;i<(int)extra.size();++i){auto n=resolvePhrase524(s,extra[i],i,true,fillPitch);n.beat+=fillStart;notes.push_back(n);}
  }
  bool variedB=isB&&(densityDelta!=0||durationBias!=0||s.get("rhythmVariation")!=0||s.get("pitchVariation")!=0);uint64_t placementSeed=(variedB?s.patternSeed:s.baseSeed)^0xd411be97ULL;
  avoidBeats525(notes,s,begin+part*phrase,0,final?fillStart:span,placementSeed);
  if(final)avoidBeats525(notes,s,begin+part*phrase,fillStart,span,placementSeed^0xf11e31ULL);
  Random placement(s.baseSeed^0x2174bf3ULL);
  for(auto n:notes){
   if(n.beat>=span||n.allocated<=0)continue;bool fill=final&&n.beat>=fillStart;
   int pitch=mapPitch(s,n.pitch,s.roots[part]);pitch=fitRegister(s,pitch,fill,placement,false,s.roots[part]);
   out.notes.push_back({out.nextId++,pitch,n.velocity,1,begin+part*phrase+n.beat,juce::jmin(n.length,span-n.beat)});
   if(out.notes.size()>8192)throw std::runtime_error("Result exceeds the 8,192-note clip limit.");
  }
 }
 out.notePurpose=1;out.key=(int)s.get("root");bool minor=false;SequenceClip::inferKey(out.notes,minor);out.minor=minor;out.manualKey=true;return out;
}
inline SequenceClip transformed(const SequenceClip& current,State& s){if(s.input.notes.empty()||current.revision!=s.outputRevision||s.input.path!=current.path)s.input=current;auto out=s.input;if(s.get("blocks")<=0&&s.get("rhythm")<=0&&s.get("pitch")<=0){s.blockOrder.clear();return out;}Random random(++s.seed);double begin=out.loopStart,end=out.loopEnd();std::vector<SequenceNote> notes;for(auto n:out.notes)if(n.beat>=begin&&n.beat<end)notes.push_back(n);std::stable_sort(notes.begin(),notes.end(),[](auto a,auto b){return a.beat<b.beat;});double block=juce::jmax(.015625,s.get("blockCount")*s.get("blockUnit"));int count=juce::jmax(1,(int)std::ceil((end-begin)/block));if(count>8192)throw std::runtime_error("Choose larger phrase blocks (8,192-block limit).");std::vector<int> order(count);std::iota(order.begin(),order.end(),0);std::vector<int> positions,selected;for(int i=0;i<count;++i)if(random.chance(s.get("blocks"))){positions.push_back(i);selected.push_back(i);}random.shuffle(selected);for(int i=0;i<(int)positions.size();++i)order[positions[i]]=selected[i];s.blockOrder=order;if(s.get("blocks")>0){std::vector<SequenceNote> moved;double destination=begin;for(int index:order){double a=begin+index*block,b=juce::jmin(end,a+block);for(auto n:notes)if(n.beat>=a&&n.beat<b){if(std::abs(destination-a)>1e-9)n.length=juce::jmin(n.length,b-n.beat);n.beat=destination+n.beat-a;moved.push_back(n);}destination+=b-a;}notes=std::move(moved);std::stable_sort(notes.begin(),notes.end(),[](auto a,auto b){return a.beat<b.beat;});}
 if(s.get("rhythm")>0&&!notes.empty()){std::vector<std::vector<SequenceNote>> groups;for(auto n:notes){if(groups.empty()||std::abs(groups.back()[0].beat-n.beat)>1e-8)groups.emplace_back();groups.back().push_back(n);}std::vector<double> spans;for(int i=0;i<(int)groups.size();++i)spans.push_back((i+1<(int)groups.size()?groups[i+1][0].beat:end)-groups[i][0].beat);auto shuffled=spans;std::vector<int> indices;std::vector<double> values;for(int i=0;i<(int)spans.size();++i)if(random.chance(s.get("rhythm"))){indices.push_back(i);values.push_back(spans[i]);}random.shuffle(values);for(int i=0;i<(int)indices.size();++i)shuffled[indices[i]]=values[i];double time=groups[0][0].beat;notes.clear();for(int i=0;i<(int)groups.size();++i){for(auto n:groups[i]){double ratio=spans[i]>0?n.length/spans[i]:1;n.beat=time;n.length=juce::jmin(end-time,shuffled[i]*ratio);notes.push_back(n);}time+=shuffled[i];}}
 std::vector<int> pitchIndices,pitches;for(int i=0;i<(int)notes.size();++i)if(random.chance(s.get("pitch"))){pitchIndices.push_back(i);pitches.push_back(notes[i].pitch);}random.shuffle(pitches);for(int i=0;i<(int)pitches.size();++i)notes[pitchIndices[i]].pitch=pitches[i];out.notes.erase(std::remove_if(out.notes.begin(),out.notes.end(),[&](auto n){return n.beat>=begin&&n.beat<end;}),out.notes.end());out.notes.insert(out.notes.end(),notes.begin(),notes.end());return out;}
inline SequenceClip run(const SequenceClip& source,State& state,Action action){if(action==Action::refreshTransform){if(state.seed>0)--state.seed;return transformed(source,state);}return action==Action::transform?transformed(source,state):generated(source,state,action);}
}
