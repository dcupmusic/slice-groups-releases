#pragma once
#include "TalExport.h"
// Worker-only decoding/export. Never installs anything in a live group or saves source settings.
namespace TalBatch548 {
struct Snapshot{juce::File file;std::shared_ptr<const AudioData> audio;std::vector<Slice> slices;int begin=0,end=0,repeat=0;double bpm=120;};
struct Result{int exported=0,skipped=0,failed=0;juce::StringArray errors;juce::Array<juce::File> presets;};
struct Progress{std::atomic<bool> busy{false};std::atomic<uint64_t> revision{0};juce::CriticalSection mutex;juce::String text;void report(juce::String value){const juce::ScopedLock guard(mutex);text=std::move(value);++revision;}juce::String status(){const juce::ScopedLock guard(mutex);return text;}};
inline bool sliceConfig(const juce::XmlElement& config){return !config.getBoolAttribute("stretchMode")&&!config.getBoolAttribute("warpMode")&&!config.getBoolAttribute("trackMode")&&!config.getBoolAttribute("oneShot")&&!config.getBoolAttribute("multiMode")&&!config.getBoolAttribute("melodicSliceMode");}
inline Result run(const juce::Array<juce::File>& files,juce::File folder,const Snapshot& active={},std::function<void(juce::String)> progress={}){
 Result result;std::set<juce::String> seen;int done=0;
 for(auto original:files){auto file=LibraryStore::resolveFile(original);if(!seen.insert(file.getFullPathName()).second)continue;auto name=original.getFileName();if(progress)progress("Exporting TAL "+juce::String(++done)+"/"+juce::String(files.size())+": "+name);
 try{
  Processor engine(false);engine.autoSave=false;engine.trackOffline=true;Snapshot snapshot;
  if(active.audio&&file==LibraryStore::resolveFile(active.file))snapshot=active;
  else{
   if(file.hasFileExtension("sfz;sgmulti;sgpreset;mid;midi")||!engine.formats.findFormatForFileExtension(file.getFileExtension())||LibraryStore::modeForType(LibraryStore::playbackType(file))!=0){++result.skipped;continue;}
   if(auto config=LibraryStore::savedLoop(file)){if(!sliceConfig(*config)){++result.skipped;continue;}engine.audio=engine.readAudio(file);if(!engine.audio||!engine.applyConfiguration(*config,false))throw std::runtime_error("Could not load saved slices or source audio.");}
   else{if(!engine.installAudio(engine.readAudio(file),false,0,false))throw std::runtime_error("Could not read audio.");engine.detect(engine.importDetectStrength,0,true);}
   if(engine.stretchMode||engine.trackMode||engine.oneShotMode||engine.melodicSliceMode){++result.skipped;continue;}
   snapshot={file,engine.audio,engine.slices,engine.regionStart(),engine.regionEnd(),engine.autoContinueRepeatFrames(),engine.sourceBpm};
  }
  juce::File saved;auto destination=folder.getChildFile(TalExport::presetName(original.getFullPathName(),snapshot.bpm)+".talsmpl");auto status=TalExport::write(destination,snapshot.audio,snapshot.slices,true,&saved,snapshot.begin,snapshot.end,snapshot.repeat);
  if(status.failed())throw std::runtime_error(status.getErrorMessage().toStdString());++result.exported;result.presets.add(saved);
 }catch(const std::exception& error){++result.failed;result.errors.add(name+": "+juce::String(error.what()));}catch(...){++result.failed;result.errors.add(name+": export failed.");}
 }
 return result;
}
inline juce::String summary(const Result& result){return "TAL export complete: "+juce::String(result.exported)+" exported, "+juce::String(result.skipped)+" unsupported skipped, "+juce::String(result.failed)+" failed."+(result.errors.isEmpty()?juce::String():" "+result.errors.joinIntoString(" | "));}
}
