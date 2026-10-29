#pragma once
#include <filesystem>
#include <stdio.h>

class LibraryCopy final:private juce::Thread,public juce::DeletedAtShutdown {
public:
 struct State{bool busy=false;double progress=0;juce::String message;juce::Array<juce::File> copied;uint64_t serial=0;};
 using Layers=std::map<juce::String,juce::var>;
 static LibraryCopy& instance(){static auto* service=new LibraryCopy();return *service;}
 State state(){const juce::ScopedLock lock(mutex);return published;}
 bool request(juce::Array<juce::File> sources,juce::File destination){const juce::ScopedLock lock(mutex);if(published.busy)return false;inputs=sources;parent=destination;cancelled=false;published={true,0,"Preparing library copy...",{},published.serial+1};wake.signal();return true;}
 void cancel(){cancelled=true;}
 static juce::File canonical(juce::File file){std::error_code error;auto path=std::filesystem::weakly_canonical(file.getFullPathName().toStdString(),error);return error?file:juce::File(juce::String(path.string()));}
 static bool writableDestination(juce::File file){file=canonical(file);if(!LibraryStore::editable(file))return false;auto me=LibraryStore::root().getChildFile("sharing-owner-id.txt").loadFileAsString().trim();for(auto dir=file;dir!=juce::File();){auto marker=dir.getChildFile(".slicegroups/sharing-v2/owner.json");if(marker.existsAsFile()){auto doc=juce::JSON::parse(marker);if((int)doc.getProperty("version",0)!=2||me.isEmpty()||doc.getProperty("owner",{}).toString()!=me)return false;}auto next=dir.getParentDirectory();if(next==dir)break;dir=next;}return true;}
 // Runs only on the background copy worker (or isolated checks).
 static juce::Result perform(juce::Array<juce::File> sources,juce::File destination,juce::Array<juce::File>& copied,std::function<bool()> cancel={},std::function<void(double)> report={}){
  struct Cleanup{juce::Array<juce::File> paths;bool committed=false;~Cleanup(){if(!committed)for(auto path:paths)path.deleteRecursively();}} cleanup;
  try{
   destination=canonical(destination);if(!destination.isDirectory()||!writableDestination(destination))return juce::Result::fail("Choose an editable destination outside your friend's library.");
   juce::Array<juce::File> selected;for(auto source:sources){source=canonical(source);if(!source.exists())return juce::Result::fail("Source is unavailable: "+source.getFileName());selected.addIfNotAlreadyThere(source);}sources.clear();for(auto file:selected){bool nested=false;for(auto other:selected)if(other!=file&&file.isAChildOf(other))nested=true;if(!nested)sources.add(file);}if(sources.isEmpty())return juce::Result::fail("Choose files or folders to copy.");
   for(auto source:sources)if(destination==source||destination.isAChildOf(source))return juce::Result::fail("The destination cannot be inside the folder being copied.");
   // Require the remote view to be ready; never silently copy audio without its shared metadata.
   for(auto source:sources){for(auto dir=source.isDirectory()?source:source.getParentDirectory();dir!=juce::File();){auto manifest=dir.getChildFile(".slicegroups/sharing-v2/owner.json");if(manifest.existsAsFile()&&!writableDestination(dir)&&LibraryStore::editable(source))return juce::Result::fail("Shared metadata is still loading. Try Copy to My Library again shortly.");auto next=dir.getParentDirectory();if(next==dir)break;dir=next;}}
   Layers metadata;for(auto kind:{"tags.json","keys.json","playback-types.json","loops.json","folder-colours.json","musical-keys.json","parameter-edits.json"})metadata[kind]=LibraryStore::read(kind);
   struct Entry{juce::File source,staged,target;bool directory=false;};std::vector<Entry> entries;std::vector<std::pair<juce::File,juce::File>> roots;juce::int64 total=0,done=0;std::set<juce::String> reserved;
   auto stop=[&]{if(cancel&&cancel())throw std::runtime_error("Copy cancelled.");};
   for(auto source:sources){stop();auto target=destination.getChildFile(source.getFileName());int suffix=1;while(target.exists()||reserved.count(target.getFullPathName())){auto stem=source.isDirectory()?source.getFileName():source.getFileNameWithoutExtension();target=destination.getChildFile(stem+" (copy"+(suffix>1?" "+juce::String(suffix):juce::String())+")"+(source.isDirectory()?juce::String():source.getFileExtension()));++suffix;}reserved.insert(target.getFullPathName());auto stage=destination.getChildFile(".slicegroups-copy-"+juce::Uuid().toString());cleanup.paths.add(stage);roots.push_back({stage,target});
    std::function<void(juce::File,juce::File,juce::File)> collect=[&](juce::File from,juce::File temp,juce::File to){stop();if(from.isSymbolicLink())throw std::runtime_error("Symbolic links cannot be copied: "+from.getFileName().toStdString());bool directory=from.isDirectory();entries.push_back({from,temp,to,directory});if(directory){for(auto child:from.findChildFiles(juce::File::findFilesAndDirectories,false))if(!child.getFileName().startsWithChar('.'))collect(child,temp.getChildFile(child.getFileName()),to.getChildFile(child.getFileName()));}else total+=from.getSize();};collect(source,stage,target);
   }
   Layers imported;auto put=[&](juce::String kind,juce::String path,juce::var value){auto& layer=imported[kind];if(!layer.isObject())layer=juce::var(new juce::DynamicObject());layer.getDynamicObject()->setProperty(path,value);};
   juce::HeapBlock<char> buffer(1024*1024);
   for(auto& entry:entries){stop();auto path=entry.source.getFullPathName(),target=entry.target.getFullPathName();if(entry.directory){if(!entry.staged.createDirectory())throw std::runtime_error("Could not create destination folder.");}else{
     if(!entry.staged.getParentDirectory().createDirectory())throw std::runtime_error("Could not create destination folder.");auto input=entry.source.createInputStream();auto output=entry.staged.createOutputStream();if(!input||!output||output->failedToOpen())throw std::runtime_error("Could not copy "+entry.source.getFileName().toStdString());auto initialSize=entry.source.getSize();auto modified=entry.source.getLastModificationTime();juce::int64 bytes=0;while(bytes<initialSize){stop();int n=input->read(buffer.get(),(int)juce::jmin((juce::int64)(1024*1024),initialSize-bytes));if(n<=0||!output->write(buffer.get(),(size_t)n))throw std::runtime_error("Copy failed or disk is full.");bytes+=n;done+=n;if(report)report(.9*done/juce::jmax((juce::int64)1,total));}output->flush();if(output->getStatus().failed()||entry.source.getSize()!=initialSize||entry.source.getLastModificationTime()!=modified)throw std::runtime_error("The source changed while copying. Please try again.");
    }
    for(auto kind:{"tags.json","keys.json","playback-types.json","folder-colours.json","musical-keys.json","parameter-edits.json"})if(auto* values=metadata[kind].getDynamicObject())if(values->hasProperty(path))put(kind,target,values->getProperty(path));
    // Materialise inherited types at each copied root so a parent's Stretch/Track setting survives.
    bool root=false;for(auto& item:roots)root|=entry.target==item.second;if(root)put("playback-types.json",target,LibraryStore::typeFromValues(entry.source,metadata["playback-types.json"]));
    if(!entry.directory){auto xmlText=metadata["loops.json"].getProperty(path,{}).toString();if(xmlText.isEmpty()){auto hash=LibraryStore::fingerprint(entry.source);xmlText=metadata["loops.json"].getProperty("sha256:"+hash,{}).toString();}
     if(xmlText.isNotEmpty()){auto config=juce::parseXML(xmlText);if(!config)throw std::runtime_error("Invalid saved settings for "+entry.source.getFileName().toStdString());config->setAttribute("file",target);for(auto attr:{"normalise","normaliseLufs","targetLufs"})config->removeAttribute(attr);put("loops.json",target,config->toString());}
    }
   }
   stop();if(!writableDestination(destination))throw std::runtime_error("Destination became read-only.");for(auto& item:roots){if(item.second.exists())throw std::runtime_error("Destination already exists. Try again.");if(::renamex_np(item.first.getFullPathName().toRawUTF8(),item.second.getFullPathName().toRawUTF8(),RENAME_EXCL)!=0)throw std::runtime_error("Could not finish copying files.");cleanup.paths.add(item.second);}
   // Register a new destination library in the same metadata transaction as the copy.
   if(!LibraryStore::inLibrary(destination))put("library-roots.json",destination.getFullPathName(),true);
   LibraryStore::database()->importCopiedMetadata(imported);cleanup.committed=true;for(auto& item:roots)copied.add(item.second);LibraryStore::pollDatabase();LibraryStore::notifySharing();if(report)report(1);return juce::Result::ok();
  }catch(const std::exception& e){return juce::Result::fail(e.what());}
 }
private:
 juce::CriticalSection mutex;juce::WaitableEvent wake;juce::Array<juce::File> inputs;juce::File parent;State published;std::atomic<bool> cancelled{false};
 LibraryCopy():Thread("Slice Groups library copy"){startThread(juce::Thread::Priority::background);}
 ~LibraryCopy()override{signalThreadShouldExit();cancelled=true;wake.signal();stopThread(-1);}
 void run()override{while(!threadShouldExit()){wake.wait(1000);juce::Array<juce::File> files;juce::File folder;{const juce::ScopedLock lock(mutex);if(!published.busy)continue;files=inputs;folder=parent;}
   juce::Array<juce::File> results;auto result=perform(files,folder,results,[this]{return cancelled.load()||threadShouldExit();},[this](double progress){const juce::ScopedLock lock(mutex);published.progress=progress;published.message="Copying to library: "+juce::String((int)(progress*100))+"%";++published.serial;});
   const juce::ScopedLock lock(mutex);published.busy=false;published.copied=results;published.message=result.wasOk()?"Copied to your library. Tags and saved settings are independently editable.":result.getErrorMessage();++published.serial;
  }}
};
