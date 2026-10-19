#pragma once
#include <map>

struct WarpPoint{int source=0;double beat=0;bool operator==(const WarpPoint& p)const{return source==p.source&&std::abs(beat-p.beat)<1e-9;}};
struct WarpChunk{
 std::vector<WarpPoint> points;
 double begin()const{return points.front().beat;}double end()const{return points.back().beat;}
 int sourceBegin()const{return points.front().source;}int sourceEnd()const{return points.back().source;}
 bool operator==(const WarpChunk& c)const{return points==c.points;}
 double sourceAt(double beat)const{size_t i=1;while(i+1<points.size()&&beat>points[i].beat)++i;auto a=points[i-1],b=points[i];return a.source+(beat-a.beat)/(b.beat-a.beat)*(b.source-a.source);}
 double beatAt(double source)const{size_t i=1;while(i+1<points.size()&&source>points[i].source)++i;auto a=points[i-1],b=points[i];return a.beat+(source-a.source)/(b.source-a.source)*(b.beat-a.beat);}
 void move(double start){double delta=start-begin();for(auto& p:points)p.beat+=delta;}
 bool valid(int frames)const{if(points.size()<2||points.size()>1024)return false;for(size_t i=0;i<points.size();++i){auto p=points[i];if(!std::isfinite(p.beat)||std::abs(p.beat)>8192||p.source<0||p.source>frames)return false;if(i&&(p.source<=points[i-1].source||p.beat<=points[i-1].beat))return false;}return true;}
 static WarpChunk trim(const WarpChunk& original,double beat,bool left,bool stretch,int frames){auto result=original;double minimum=.001;
  if(stretch){if(left)beat=juce::jmin(beat,original.end()-minimum);else beat=juce::jmax(beat,original.begin()+minimum);double a=left?beat:original.begin(),b=left?original.end():beat;for(auto& p:result.points)p.beat=a+(p.beat-original.begin())/(original.end()-original.begin())*(b-a);return result;}
  beat=left?juce::jmin(beat,original.end()-minimum):juce::jmax(beat,original.begin()+minimum);int source=juce::jlimit(0,frames,(int)std::round(original.sourceAt(beat)));source=left?juce::jmin(source,original.sourceEnd()-1):juce::jmax(source,original.sourceBegin()+1);beat=original.beatAt(source);result.points.clear();if(left){result.points.push_back({source,beat});for(auto p:original.points)if(p.source>source)result.points.push_back(p);}else{for(auto p:original.points)if(p.source<source)result.points.push_back(p);result.points.push_back({source,beat});}return result;
 }
};
using WarpArrangement=std::vector<WarpChunk>;
