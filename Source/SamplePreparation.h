#pragma once
// Immutable decoded audio, shared across engines. File identity is validated on every lookup.
class DecodedSampleCache {
 struct Entry{juce::File file;juce::Time modified;juce::int64 size=0;size_t bytes=0;std::shared_ptr<const AudioData> data;};
 juce::CriticalSection mutex;std::vector<Entry> entries;size_t used=0;
public:
 static constexpr size_t budget=128u*1024u*1024u;
 std::atomic<uint64_t> hits{0},misses{0};
 static DecodedSampleCache& instance(){static DecodedSampleCache cache;return cache;}
 static size_t bytes(const AudioData& data){size_t total=(size_t)data.buffer.getNumSamples()*data.buffer.getNumChannels()*sizeof(float)+data.peaks.size()*sizeof(float)+data.transients.size()*sizeof(int);for(auto& level:data.waveformLevels)total+=level.size()*sizeof(level.front());return total;}
 std::shared_ptr<const AudioData> get(juce::File file){if(file.hasFileExtension("sfz;sgmulti"))return {};auto modified=file.getLastModificationTime();auto size=file.getSize();const juce::ScopedLock guard(mutex);for(auto it=entries.begin();it!=entries.end();++it)if(it->file==file&&it->modified==modified&&it->size==size){auto entry=*it;entries.erase(it);entries.push_back(entry);++hits;return entry.data;}++misses;return {};}
 void put(juce::File file,std::shared_ptr<const AudioData> data,juce::Time modified,juce::int64 size){if(file.getLastModificationTime()!=modified||file.getSize()!=size)return;if(!data||file.hasFileExtension("sfz;sgmulti"))return;auto amount=bytes(*data);if(amount>budget)return;Entry entry{file,modified,size,amount,std::move(data)};std::vector<Entry> retired;
  {const juce::ScopedLock guard(mutex);for(auto it=entries.begin();it!=entries.end();)if(it->file==file){used-=it->bytes;retired.push_back(std::move(*it));it=entries.erase(it);}else ++it;
   entries.push_back(std::move(entry));used+=amount;while(entries.size()>24||used>budget){used-=entries.front().bytes;retired.push_back(std::move(entries.front()));entries.erase(entries.begin());}}
 }
 size_t memory(){const juce::ScopedLock guard(mutex);return used;}
};
#include "PreparationState.h"
