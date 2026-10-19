#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <regex>
#include <vector>

struct LoopTiming {
    double bpm = 120, beats = 4, bars = 1;
    juce::String method = "Estimated";
};

inline LoopTiming constrainEstimatedTempo(LoopTiming timing,double low,double high){
    if(timing.method!="Estimated"||low<=0||high<=low)return timing;
    double original=timing.bpm;
    while(timing.bpm<low&&timing.bpm*2<=high)timing.bpm*=2;
    while(timing.bpm>high&&timing.bpm*.5>=low)timing.bpm*=.5;
    timing.beats*=timing.bpm/original;timing.bars*=timing.bpm/original;return timing;
}

// Loop length constrains the estimate to musically useful bar counts. Rhythm
// evidence selects between half/double-time candidates; names and ACID metadata
// are preferred when present. Ambiguous recordings remain editable in the UI.
inline LoopTiming estimateLoopTiming(const juce::AudioBuffer<float>& buffer,double rate,
                                    const juce::String& filename,const juce::StringPairArray& metadata,
                                    double preferredBpm = 120,double beatsPerBar = 4) {
    LoopTiming result;
    double duration = buffer.getNumSamples()/rate;
    if (duration <= 0) return result;
    beatsPerBar = juce::jmax(.25,beatsPerBar);
    double namedBpm = metadata.getValue(juce::WavAudioFormat::acidTempo,"0").getDoubleValue();
    double namedBeats = metadata.getValue(juce::WavAudioFormat::acidBeats,"0").getDoubleValue();
    if (namedBeats > 0 && namedBeats <= 1024 && metadata.getValue(juce::WavAudioFormat::acidOneShot,"0") != "1") {
        result.beats = namedBeats;result.bpm = 60*namedBeats/duration;result.bars = namedBeats/beatsPerBar;result.method = "Audio metadata";return result;
    }
    bool fromMetadata = namedBpm >= 20 && namedBpm <= 400;
    if (!fromMetadata) {
        std::smatch match;auto name = filename.toLowerCase().toStdString();
        const std::regex after(R"((?:^|[^0-9])([0-9]{2,3}(?:\.[0-9]+)?)\s*[-_ ]?bpm(?:[^a-z]|$))");
        const std::regex before(R"(bpm\s*[-_ ]?([0-9]{2,3}(?:\.[0-9]+)?)(?:[^0-9]|$))");
        const std::regex token(R"((?:^|[_ -])([0-9]{2,3})(?:[_ -]|$))");
        if (std::regex_search(name,match,after) || std::regex_search(name,match,before)) namedBpm = std::stod(match[1]);
        else if (std::regex_search(name,match,token)) { double value = std::stod(match[1]);if (value >= 70 && value <= 200) namedBpm = value; }
    }
    const std::array<double,9> candidates {.25,.5,1,2,4,8,16,32,64};
    if (namedBpm >= 20 && namedBpm <= 400) {
        double estimatedBars = duration*namedBpm/60/beatsPerBar;
        double nearest = candidates[0];
        for (auto bars : candidates) if (std::abs(std::log(bars/estimatedBars)) < std::abs(std::log(nearest/estimatedBars))) nearest = bars;
        // Preserve exact musical length for well-trimmed loops.
        result.bars = std::abs(nearest-estimatedBars)/nearest < .035 ? nearest : estimatedBars;
        result.beats = result.bars*beatsPerBar;result.bpm = 60*result.beats/duration;
        result.method = fromMetadata ? "Audio metadata" : "Filename";return result;
    }
    int hop = juce::jmax(1,(int)(rate/200));
    std::vector<double> novelty;novelty.reserve(buffer.getNumSamples()/hop+1);
    double previous = 0;
    for (int start = 0; start < buffer.getNumSamples(); start += hop) {
        double energy = 0;int end = juce::jmin(start+hop,buffer.getNumSamples());
        for (int n = start; n < end; ++n) {
            float maximum = 0;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) maximum = juce::jmax(maximum,std::abs(buffer.getSample(ch,n)));
            energy += maximum;
        }
        energy /= end-start;novelty.push_back(juce::jmax(0.0,energy-previous));previous = previous*.45+energy*.55;
    }
    auto correlation = [&](double seconds) {
        int lag = (int)std::round(seconds*rate/hop);if (lag < 1 || lag >= (int)novelty.size()) return 0.0;
        double product = 0,a = 0,b = 0;
        for (int i = lag; i < (int)novelty.size(); ++i) { product += novelty[i]*novelty[i-lag];a += novelty[i]*novelty[i];b += novelty[i-lag]*novelty[i-lag]; }
        return product/(std::sqrt(a*b)+1.0e-15);
    };
    double best = -1.0e9;
    preferredBpm = juce::jlimit(75.0,160.0,preferredBpm);
    for (auto bars : candidates) {
        double bpm = 60*bars*beatsPerBar/duration;
        if (bpm < 45 || bpm > 240) continue;
        double score = correlation(60/bpm)*1.5 + correlation(120/bpm)*.65 + correlation(30/bpm)*.25;
        score -= std::abs(std::log2(bpm/120))*.38;
        score -= std::abs(std::log2(bpm/preferredBpm))*.12;
        if (bpm < 70 || bpm > 180) score -= .3;
        if (score > best) { best = score;result.bpm = bpm;result.bars = bars;result.beats = bars*beatsPerBar; }
    }
    if (best == -1.0e9) { result.bars = 1;result.beats = beatsPerBar;result.bpm = 60*beatsPerBar/duration; }
    return result;
}
