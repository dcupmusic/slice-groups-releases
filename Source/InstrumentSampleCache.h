#pragma once
// Cache only individual decoded samples; always re-read SFZ/native mappings.
class InstrumentSampleCache {
 struct Entry{juce::File file;juce::Time stamp;juce::int64 size=0;size_t bytes=0;std::shared_ptr<const AudioData> data;};
 juce::CriticalSection mutex;std::vector<Entry> entries;size_t used=0;
public:
 static constexpr size_t budget=128u*1024u*1024u;
 static InstrumentSampleCache& instance(){static InstrumentSampleCache cache;return cache;}
 std::shared_ptr<const AudioData> get(juce::File file){auto stamp=file.getLastModificationTime();auto size=file.getSize();const juce::ScopedLock guard(mutex);for(auto it=entries.begin();it!=entries.end();++it)if(it->file==file&&it->stamp==stamp&&it->size==size){auto result=it->data;auto entry=*it;entries.erase(it);entries.push_back(std::move(entry));return result;}return {};}
 void put(juce::File file,std::shared_ptr<const AudioData> data,juce::Time stamp,juce::int64 size){if(!data||file.getSize()!=size||file.getLastModificationTime()!=stamp)return;size_t amount=(size_t)data->buffer.getNumSamples()*(data->buffer.getNumChannels()*sizeof(float)+1);if(amount>budget)return;std::vector<Entry> retired;{const juce::ScopedLock guard(mutex);for(auto it=entries.begin();it!=entries.end();)if(it->file==file){used-=it->bytes;retired.push_back(std::move(*it));it=entries.erase(it);}else ++it;entries.push_back({file,stamp,size,amount,std::move(data)});used+=amount;while(used>budget||entries.size()>128){used-=entries.front().bytes;retired.push_back(std::move(entries.front()));entries.erase(entries.begin());}}}
 void clear(){std::vector<Entry> retired;{const juce::ScopedLock guard(mutex);retired.swap(entries);used=0;}}
 size_t memory(){const juce::ScopedLock guard(mutex);return used;}
};
