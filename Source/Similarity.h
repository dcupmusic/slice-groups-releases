#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <deque>
#include "SimilarityLearning.h"

// Local, opt-in sound search. No work is performed on the audio/message threads.
class DrumSimilarity final : private juce::Thread {
public:
 enum class Mode {Drums,General};
 struct Features {std::array<float,512> sound{};std::array<float,26> dynamics{};};
 struct Context{Features reference;std::vector<std::array<float,512>> alternatives;std::array<double,26> scales{};double neuralScale=1,dynamicsScale=1;};
 using Labels=std::map<juce::String,int>;
 using Scores=std::map<juce::String,float>;
 struct State {uint64_t revision=0,resultRevision=0;bool busy=false,paused=false,queryBusy=false,pitchTolerant=false;Mode mode=Mode::General;int queryCompleted=0,queryTotal=0;int completed=0,total=0,failed=0,cached=0;juce::String message,lastError;SimilarityLearning::Summary feedback;std::shared_ptr<const Labels> labels;std::shared_ptr<const Context> context;bool canUndo=false,learning=false;juce::File reference;std::shared_ptr<const Scores> scores;};
 explicit DrumSimilarity(juce::File cacheRoot);
 ~DrumSimilarity() override;
 void prepare(juce::Array<juce::File> files);
 void find(juce::File reference,Mode mode=Mode::Drums,bool pitchTolerant=false);
 void clear();
 void rate(juce::File result,int label);
 void undoRating();
 void learn();void usePersonal(bool);void revertPersonal();void exportFeedback(juce::File);
 static juce::String profile(Mode mode,bool pitch){return mode==Mode::Drums?"drums":pitch?"general-pitch":"general";}
 void pause(bool value);
 void cancel();
 State state()const;
 juce::File referenceForQuery()const;
 static bool supported(juce::File);
 static juce::File modelFile();
 static Scores rank(const Features&,const std::vector<std::pair<juce::String,Features>>&,Mode mode=Mode::Drums,const std::vector<std::array<float,512>>& alternatives={},const std::function<bool()>& cancelled={},Context* context=nullptr,double neuralWeight=-1);
 // Worker-only feature extraction also used by the offline verification harness.
 static Features extract(juce::File,void* model,std::function<bool()> cancelled,double semitones=0,bool body=false);
private:
 void run()override;
 void launch();
 juce::File root;mutable std::mutex mutex;State current;
 juce::Array<juce::File> pending;juce::File query;bool queryPending=false;
 struct FeedbackJob{int action=0,label=0;juce::File reference,result,output;juce::String profile;std::shared_ptr<const Context> context;};
 struct RatingUndo{FeedbackJob job;int previous=0;juce::String previousBest;};std::deque<FeedbackJob> feedbackJobs;std::vector<RatingUndo> ratingUndo;
 std::atomic<uint64_t> generation{0},batchGeneration{0};
};
