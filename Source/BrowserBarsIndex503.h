#pragma once
// Owned by the Bars worker, never the UI or audio thread. Rebuild saved-length
// summaries only after their namespace changes; ordinary group switches reuse them.
struct BrowserBarsIndex503 {
 juce::String root;uint64_t loopsSeen=~uint64_t{},patchesSeen=~uint64_t{};
 std::map<juce::String,double> base;
 std::map<juce::String,juce::String> xmls;
 std::map<juce::String,juce::var> patches;
 std::atomic<uint64_t> reads{0},parses{0};
 static double parse(const juce::String& text){auto xml=juce::parseXML(text);if(!xml)return -1;double meter=xml->getDoubleAttribute("beatsPerBar",4),rate=xml->getDoubleAttribute("rate"),bpm=xml->getDoubleAttribute("bpm",120);double beats=xml->getBoolAttribute("regionTiming")?xml->getDoubleAttribute("regionBeatCount"):(rate>0?(xml->getDoubleAttribute("trimEnd")-xml->getDoubleAttribute("trimStart"))/rate*bpm/60:-1);return meter>0&&beats>0&&std::isfinite(beats)?beats/meter:-1;}
 static std::map<juce::String,juce::var> read(juce::String kind,const std::function<void()>& checkpoint){auto values=LibraryStore::database()->indexedMetadata(kind,checkpoint);auto shared=LibraryStore::sharedSnapshot(kind);if(auto* o=shared.getDynamicObject())for(auto& item:o->getProperties()){checkpoint();values[item.name.toString()]=item.value;}return values;}
 void refresh(const std::function<void()>& checkpoint){auto current=LibraryStore::root().getFullPathName();if(root!=current){root=current;loopsSeen=patchesSeen=~uint64_t{};base.clear();xmls.clear();patches.clear();}auto loopsRevision=LibraryStore::kindRevision("loops.json").load(),patchRevision=LibraryStore::kindRevision("parameter-edits.json").load();
  if(loopsSeen!=loopsRevision){auto values=read("loops.json",checkpoint);++reads;std::map<juce::String,double> next;std::map<juce::String,juce::String> nextXml;int count=0;for(auto& item:values){if(++count%256==0)checkpoint();if(!juce::File::isAbsolutePath(item.first))continue;auto text=item.second.toString();auto old=xmls.find(item.first);auto length=base.find(item.first);double bars;if(old!=xmls.end()&&old->second==text&&length!=base.end())bars=length->second;else{bars=parse(text);++parses;}next.emplace(item.first,bars);nextXml.emplace(item.first,text);}checkpoint();base.swap(next);xmls.swap(nextXml);loopsSeen=loopsRevision;}
  if(patchesSeen!=patchRevision){auto next=read("parameter-edits.json",checkpoint);++reads;checkpoint();patches.swap(next);patchesSeen=patchRevision;}
 }
 double get(const juce::String& path)const{auto p=patches.find(path);if(p!=patches.end()){auto& value=p->second;if(value.hasProperty("bulkResizeBars"))return (double)value.getProperty("bulkResizeBars",-1);if(value.hasProperty("bulkBars"))return (double)value.getProperty("bulkBars",-1);}auto b=base.find(path);return b==base.end()?-1:b->second;}
};
