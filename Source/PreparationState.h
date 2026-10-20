#pragma once
#include <atomic>
namespace LibraryPreparation {
inline constexpr bool automaticEnabled=false;
inline std::atomic<int> foreground{0},warmJobs{0};inline std::atomic<uint64_t> epoch{0};inline std::atomic<bool> checksPaused{false},suspended{false};
inline std::atomic<juce::uint32> lastPlayback{0};
inline void markPlayback(bool playing){if(playing)lastPlayback=juce::Time::getMillisecondCounter();}
inline bool playbackBusy(){auto tick=lastPlayback.load();return tick&&(juce::uint32)(juce::Time::getMillisecondCounter()-tick)<400;}
struct Foreground{Foreground(){++foreground;++epoch;}~Foreground(){--foreground;}};
struct WarmJob{bool acquired=false;WarmJob(){int expected=0;acquired=warmJobs.compare_exchange_strong(expected,1);}~WarmJob(){if(acquired)--warmJobs;}explicit operator bool()const{return acquired;}};
void request(juce::Array<juce::File> files);void requestScope(juce::File folder);void requestStretch(juce::File folder);void cancel();bool busy();juce::String status();
}
