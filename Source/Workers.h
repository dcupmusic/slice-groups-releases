#pragma once
#include "Processor.h"

class LoopLoader final : public juce::Thread {
 Processor& p;juce::CriticalSection queueLock;juce::WaitableEvent wake;juce::File next;uint64_t serial=0,warmSerial=0;juce::Array<juce::File> upcoming;juce::String warmSignature;
 struct Cache{juce::File file;juce::Time modified;std::shared_ptr<const AudioData> data;};std::vector<Cache> cache;
 std::shared_ptr<const AudioData> readCached(juce::File file,std::function<bool()> cancel={}){auto modified=file.getLastModificationTime();for(auto it=cache.begin();it!=cache.end();++it)if(!file.hasFileExtension("sfz")&&it->file==file&&it->modified==modified){auto hit=*it;cache.erase(it);cache.push_back(hit);return hit.data;}auto data=p.readAudio(file,cancel);if(data){if(cache.size()>=8)cache.erase(cache.begin());cache.push_back({file,modified,data});}return data;}
public:
 std::atomic<bool> busy{false};std::function<void(bool,juce::File,uint64_t)> completed;
 explicit LoopLoader(Processor& processor):Thread("Slice Groups file loader"),p(processor){}
 ~LoopLoader()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 void request(juce::File file){const juce::ScopedLock g(queueLock);next=file;serial=++p.loadGeneration;busy=true;wake.signal();}
 void cancel(){const juce::ScopedLock g(queueLock);next=juce::File();serial=++p.loadGeneration;upcoming.clear();++warmSerial;busy=false;wake.signal();}
 void prepareUpcoming(juce::Array<juce::File> files,juce::String signature){const juce::ScopedLock g(queueLock);if(files==upcoming&&signature==warmSignature)return;upcoming=std::move(files);warmSignature=signature;++warmSerial;wake.signal();}
 void run()override{uint64_t warmed=0;while(!threadShouldExit()){
  bool queued;{const juce::ScopedLock g(queueLock);queued=next!=juce::File();}if(!queued)wake.wait(50);if(threadShouldExit())break;juce::File file;uint64_t token,warmToken;juce::Array<juce::File> candidates;
  {const juce::ScopedLock g(queueLock);file=next;token=serial;next=juce::File();warmToken=warmSerial;candidates=upcoming;}
  if(file!=juce::File()){auto preset=file.hasFileExtension("sgpreset")?juce::XmlDocument::parse(file):nullptr;auto source=preset?juce::File(preset->getStringAttribute("file")):file;auto data=preset&&preset->hasTagName("SliceGroupsRack")?std::shared_ptr<const AudioData>{}:readCached(source,[&]{return threadShouldExit()||token!=p.loadGeneration.load();});if(threadShouldExit()||token!=p.loadGeneration.load())continue;bool ok=file.hasFileExtension("sgpreset")?(preset&&p.installPreset(data,*preset,token)):p.installAudio(data,true,token);auto callback=completed;juce::MessageManager::callAsync([token,callback,ok,file]{if(callback)callback(ok,file,token);});}
  if(warmed==warmToken||candidates.isEmpty())continue;
  auto cancelled=[&]{const juce::ScopedLock g(queueLock);return threadShouldExit()||next!=juce::File()||warmSerial!=warmToken||serial!=token;};
  while(p.stretchWorker.busy&&!cancelled())wake.wait(20);if(cancelled())continue;
  std::shared_ptr<const AudioData> current;std::unique_ptr<juce::XmlElement> settings;double tempo;bool host,locked,stretch;double bars;{const juce::ScopedLock g(p.lock);if(!p.audio){warmed=warmToken;continue;}stretch=p.stretchMode&&!p.trackMode;current=p.audio;settings=p.configurationXml(true);tempo=p.hostBpm;host=p.hasHost;locked=p.loopLengthLocked;bars=p.lockedBars;}
  if(!stretch){for(auto candidate:candidates){if(cancelled())break;auto preset=candidate.hasFileExtension("sgpreset")?juce::XmlDocument::parse(candidate):nullptr;readCached(preset?juce::File(preset->getStringAttribute("file")):candidate,cancelled);}if(!cancelled())warmed=warmToken;continue;}
  Processor preview(false);preview.audio=current;preview.applyConfiguration(*settings,true);preview.swingLinked=false;preview.autoSave=false;preview.autoCategories=false;preview.loopLengthLocked=locked;preview.lockedBars=bars;
  for(auto candidate:candidates){if(cancelled())break;auto preset=candidate.hasFileExtension("sgpreset")?juce::XmlDocument::parse(candidate):nullptr;auto source=preset?juce::File(preset->getStringAttribute("file")):candidate;auto data=readCached(source,cancelled);if(!data||cancelled())continue;
   preview.audio=current;preview.applyConfiguration(*settings,true);preview.swingLinked=false;preview.autoSave=false;preview.autoCategories=false;preview.hasHost=host;preview.hostBpm=tempo;
   bool ok=preset?preview.installPreset(data,*preset):preview.installAudio(data,true,0,false);if(!ok||!preview.stretchMode||preview.trackMode||cancelled())continue;auto request=preview.stretchRequest();if(!p.stretchWorker.cached(request)){auto result=StretchWorker::render(request,cancelled);if(result&&!cancelled())p.stretchWorker.cachePrepared(result);}
  }if(!cancelled())warmed=warmToken;
 }}
};
class LiveDetector final : public juce::Thread {
    Processor& p;juce::CriticalSection queueLock;juce::WaitableEvent wake;double next = -1;uint64_t serial = 0;
public:
    std::atomic<bool> busy {false};
    explicit LiveDetector(Processor& processor) : Thread("Slice Groups transient detector"),p(processor) {}
    ~LiveDetector() override {signalThreadShouldExit();wake.signal();stopThread(-1);}
    void request(double value) {const juce::ScopedLock g(queueLock);next=value;serial=++p.detectionGeneration;busy=true;wake.signal();}
    void finishNow(double value) { {const juce::ScopedLock g(queueLock);next=-1;++p.detectionGeneration;}p.detect(value);busy=false; }
    void run() override {
        while(!threadShouldExit()) {
            wake.wait(40);if(threadShouldExit())break;double value;uint64_t token;
            {const juce::ScopedLock g(queueLock);value=next;token=serial;next=-1;}
            if(value<0)continue;p.detect(value,token);{const juce::ScopedLock g(queueLock);if(next<0)busy=false;}
        }
    }
};

class FolderAnalyser final:public juce::Thread{
 Processor& p;LoopLoader& loader;juce::CriticalSection mutex;juce::WaitableEvent wake;juce::Array<juce::File> files;uint64_t revision=0;
public:
 FolderAnalyser(Processor& processor,LoopLoader& worker):Thread("Slice Groups folder analysis"),p(processor),loader(worker){startThread(juce::Thread::Priority::low);}
 ~FolderAnalyser()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 void request(juce::Array<juce::File> list){const juce::ScopedLock lock(mutex);files=std::move(list);++revision;wake.signal();}
 void run()override{uint64_t done=0;while(!threadShouldExit()){wake.wait(50);juce::Array<juce::File> list;uint64_t token;{const juce::ScopedLock lock(mutex);if(done==revision)continue;token=revision;list=files;}auto changed=[&]{const juce::ScopedLock lock(mutex);return threadShouldExit()||revision!=token;};for(auto file:list){if(changed())break;if(file.isDirectory()||(AnalysisStore::exists(file)&&LoopLoudness::exists(file)))continue;bool completed=false;while(!completed&&!changed()){if(loader.busy){wake.wait(50);continue;}auto generation=p.loadGeneration.load();auto cancelled=[&]{return changed()||loader.busy||generation!=p.loadGeneration.load();};auto data=p.readAudio(file,cancelled);if(!cancelled())completed=true;else wake.wait(50);}}if(!changed())done=token;}}
};

// Enumerates names only. No decoding, hashing, or transient analysis for search.
class BrowserSearchIndex final:public juce::Thread {
    juce::CriticalSection mutex;juce::WaitableEvent wake;juce::File root;std::atomic<uint64_t> revision{0};
public:
    std::function<void(juce::File,juce::Array<juce::File>,uint64_t)> completed;
    BrowserSearchIndex():Thread("Slice Groups browser search"){startThread(juce::Thread::Priority::low);}
    ~BrowserSearchIndex()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
    uint64_t request(juce::File folder){const juce::ScopedLock g(mutex);root=folder;auto token=++revision;wake.signal();return token;}
    static juce::Array<juce::File> enumerate(juce::File folder,std::function<bool()> cancelled={},bool includeFolders=false){
        juce::AudioFormatManager formats;formats.registerBasicFormats();juce::Array<juce::File> result;std::vector<juce::File> pending{folder};
        while(!pending.empty()){if(cancelled&&cancelled())return {};auto current=pending.back();pending.pop_back();for(auto entry:juce::RangedDirectoryIterator(current,false,"*",juce::File::findFilesAndDirectories)){if(cancelled&&cancelled())return {};auto file=entry.getFile();if(file.getFileName().startsWithChar('.')||file.isSymbolicLink())continue;if(file.isDirectory()){pending.push_back(file);if(includeFolders)result.add(file);}else if(file.hasFileExtension("sfz")||formats.findFormatForFileExtension(file.getFileExtension()))result.add(file);}}
        struct Sort{int compareElements(const juce::File& a,const juce::File& b)const{int n=a.getFileName().compareIgnoreCase(b.getFileName());return n?n:a.getFullPathName().compareIgnoreCase(b.getFullPathName());}} sort;result.sort(sort);return result;
    }
    static juce::File indexFile(juce::File root){auto folder=LibraryStore::root().getChildFile("search-index");folder.createDirectory();return folder.getChildFile(juce::String::toHexString(root.getFullPathName().hashCode64())+".json");}
    static juce::Array<juce::File> cached(juce::File root){juce::Array<juce::File> files;auto value=juce::JSON::parse(indexFile(root));if(auto* array=value.getArray())for(auto item:*array)files.add(juce::File(item.toString()));return files;}
    void run()override{uint64_t last=0;std::map<juce::String,juce::Time> watched;double lastPoll=0;while(!threadShouldExit()){wake.wait(100);if(threadShouldExit())break;juce::File folder;uint64_t token;{const juce::ScopedLock g(mutex);folder=root;token=revision.load();}if(token==last){double now=juce::Time::getMillisecondCounterHiRes();if(now-lastPoll<500)continue;lastPoll=now;bool changed=false;for(auto& item:watched)if(juce::File(item.first).getLastModificationTime()!=item.second){changed=true;break;}if(!changed)continue;}else watched.clear();last=token;if(!folder.isDirectory())continue;auto cancelled=[&]{return threadShouldExit()||revision.load()!=token;};auto roots=folder==LibraryStore::browserRoot()?LibraryStore::libraryRoots():juce::Array<juce::File>{folder};auto deliver=[&](juce::Array<juce::File> found){auto callback=completed;juce::MessageManager::callAsync([callback,folder,found=std::move(found),token]()mutable{if(callback)callback(folder,std::move(found),token);});};juce::Array<juce::File> stored;bool hasCache=true;for(auto root:roots){hasCache&=indexFile(root).existsAsFile();if(folder==LibraryStore::browserRoot())stored.add(root);stored.addArray(cached(root));}if(hasCache&&watched.empty()&&!cancelled()){struct Sort{int compareElements(const juce::File&a,const juce::File&b)const{int n=a.getFileName().compareIgnoreCase(b.getFileName());return n?n:a.getFullPathName().compareIgnoreCase(b.getFullPathName());}}sort;stored.sort(sort);deliver(stored);}juce::Array<juce::File> fresh;for(auto root:roots){if(cancelled())break;if(folder==LibraryStore::browserRoot())fresh.add(root);auto found=enumerate(root,cancelled,true);if(cancelled())break;juce::Array<juce::var> paths;for(auto file:found)paths.add(file.getFullPathName());juce::TemporaryFile temporary(indexFile(root));if(temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(paths))))temporary.overwriteTargetFileWithTemporary();fresh.addArray(found);}if(!cancelled()){struct Sort{int compareElements(const juce::File&a,const juce::File&b)const{int n=a.getFileName().compareIgnoreCase(b.getFileName());return n?n:a.getFullPathName().compareIgnoreCase(b.getFullPathName());}}sort;fresh.sort(sort);for(int i=fresh.size()-1;i>0;--i)if(fresh[i]==fresh[i-1])fresh.remove(i);watched.clear();for(auto root:roots)watched[root.getFullPathName()]=root.getLastModificationTime();for(auto file:fresh)if(file.isDirectory())watched[file.getFullPathName()]=file.getLastModificationTime();deliver(std::move(fresh));}}}

};

// Finder trash operations may block for seconds; this worker survives editor/engine switches.
class FileTrashWorker final:private juce::Thread {
 juce::CriticalSection mutex;juce::WaitableEvent wake;std::vector<juce::Array<juce::File>> queue;juce::Array<juce::File> hidden;std::set<juce::String> pending;
 std::function<void(juce::Array<juce::File>,juce::Array<juce::File>)> completed;
 std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
 void run()override{while(!threadShouldExit()){juce::Array<juce::File> batch;{const juce::ScopedLock lock(mutex);if(!queue.empty()){batch=std::move(queue.front());queue.erase(queue.begin());}}if(batch.isEmpty()){wake.wait(100);continue;}juce::Array<juce::File> moved,failed;for(auto file:batch){if(file.existsAsFile()&&file.moveToTrash())moved.add(file);else failed.add(file);{const juce::ScopedLock lock(mutex);pending.erase(file.getFullPathName());if(failed.contains(file))hidden.removeAllInstancesOf(file);}}
 // Remove stale cached rows too; live browser results are pruned immediately by the editor.
 for(auto root:LibraryStore::libraryRoots()){bool affected=false;for(auto file:moved)affected|=file.isAChildOf(root);if(!affected)continue;auto stored=BrowserSearchIndex::cached(root);for(auto file:moved)stored.removeAllInstancesOf(file);juce::Array<juce::var> paths;for(auto file:stored)paths.add(file.getFullPathName());juce::TemporaryFile temp(BrowserSearchIndex::indexFile(root));if(temp.getFile().replaceWithText(juce::JSON::toString(juce::var(paths))))temp.overwriteTargetFileWithTemporary();}
 std::function<void(juce::Array<juce::File>,juce::Array<juce::File>)> callback;{const juce::ScopedLock lock(mutex);callback=completed;}auto life=alive;juce::MessageManager::callAsync([life,callback,moved,failed]{if(life->load()&&callback)callback(moved,failed);});}}
public:
 FileTrashWorker():juce::Thread("Slice Groups trash"){startThread(juce::Thread::Priority::low);}
 ~FileTrashWorker()override{*alive=false;signalThreadShouldExit();wake.signal();stopThread(-1);}
 void setCompleted(std::function<void(juce::Array<juce::File>,juce::Array<juce::File>)> callback){const juce::ScopedLock lock(mutex);completed=std::move(callback);}
 void request(juce::Array<juce::File> files){const juce::ScopedLock lock(mutex);juce::Array<juce::File> batch;for(auto file:files)if(pending.insert(file.getFullPathName()).second){hidden.addIfNotAlreadyThere(file);batch.add(file);}if(!batch.isEmpty())queue.push_back(std::move(batch));wake.signal();}
 juce::Array<juce::File> hiddenFiles(bool restored=false){const juce::ScopedLock lock(mutex);if(restored)for(int i=hidden.size();--i>=0;)if(!pending.count(hidden[i].getFullPathName())&&hidden[i].existsAsFile())hidden.remove(i);return hidden;}
};
