#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <map>
#include <functional>
#include <sqlite3.h>

// Human judgements remain local and separate from disposable analysis caches.
class SimilarityLearning {
public:
 struct Example{juce::String reference,result,profile;int label=0;int64_t refTime=0,refSize=0,resultTime=0,resultSize=0;double neural=0,dynamics=0;std::array<float,512> refEmbedding{},resultEmbedding{};std::array<float,26> refDynamics{},resultDynamics{};bool valid=false;};
 struct Summary{int positive=0,negative=0,references=0;bool model=false,enabled=true;double weight=-1,previous=-1;double readiness()const{return (juce::jmin(1.,(positive+negative)/20.)+juce::jmin(1.,positive/5.)+juce::jmin(1.,negative/5.)+juce::jmin(1.,references/3.))/4.;}bool trainable()const{return positive>=5&&negative>=5&&positive+negative>=20&&references>=3;}};
 struct Training{bool accepted=false;double weight=0,baseline=0,validation=0;juce::String message;};
 explicit SimilarityLearning(juce::File root);
 ~SimilarityLearning();
 void save(const Example&);
 std::map<juce::String,int> labels(const juce::String& reference,const juce::String& profile);
 Summary summary(const juce::String& profile);
 Training train(const juce::String& profile,double baseline,std::function<bool()> cancelled);
 void enable(const juce::String& profile,bool);
 void revert(const juce::String& profile);
 bool exportTo(juce::File,std::function<bool()> cancelled);
private:
 sqlite3* db=nullptr;
 std::vector<Example> examples(const juce::String& profile);
};
