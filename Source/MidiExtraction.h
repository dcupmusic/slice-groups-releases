#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Sequence.h"
struct MidiExtractionSettings{int sensitivity=1,minimumMs=80,low=21,high=108;bool monophonic=false;};
struct MidiExtractionResult {
 SequenceClip clip;juce::Image spectrogram;double beats=0;juce::String error;
};
class MidiExtraction {
public:
 static juce::File modelFile();
 static MidiExtractionResult run(const std::vector<float>& mono,double rate,double bpm,std::function<bool()> cancelled={},std::function<void(double)> progress={},MidiExtractionSettings settings={});
};
