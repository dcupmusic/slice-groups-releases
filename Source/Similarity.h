#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <deque>

// Local, opt-in sound search. No work is performed on the audio/message threads.
class DrumSimilarity final : private juce::Thread {
public:
 enum class Mode {Drums,General};
 struct Features {std::array<float,512> sound{};std::array<float,26> dynamics{};};
 using Scores=std::map<juce::String,float>;
 struct State {uint64_t revision=0,resultRevision=0;bool busy=false,paused=false,queryBusy=false,pitchTolerant=false;Mode mode=Mode::General;int queryCompleted=0,queryTotal=0;int completed=0,total=0,failed=0,cached=0;juce::String message,lastError;juce::File reference;std::shared_ptr<const Scores> scores;};
 explicit DrumSimilarity(juce::File cacheRoot);
 ~DrumSimilarity() override;
 void prepare(juce::Array<juce::File> files);
 void find(juce::File reference,Mode mode=Mode::Drums,bool pitchTolerant=false);
 void clear();
 void pause(bool value);
 void cancel();
 State state()const;
 juce::File referenceForQuery()const;
 static bool supported(juce::File);
 static juce::File modelFile();
 static Scores rank(const Features&,const std::vector<std::pair<juce::String,Features>>&,Mode mode=Mode::Drums,const std::vector<std::array<float,512>>& alternatives={},const std::function<bool()>& cancelled={});
 // Worker-only feature extraction also used by the offline verification harness.
 static Features extract(juce::File,void* model,std::function<bool()> cancelled,double semitones=0,bool body=false);
private:
 void run()override;
 void launch();
 juce::File root;mutable std::mutex mutex;State current;
 juce::Array<juce::File> pending;juce::File query;bool queryPending=false;
 std::atomic<uint64_t> generation{0},batchGeneration{0};
};
