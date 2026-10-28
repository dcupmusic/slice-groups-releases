#pragma once
#include "Processor.h"
#include "SamplePreparation.h"
#include "NativeTrash.h"

class LoopLoader final : public juce::Thread {
 Processor& p;juce::CriticalSection queueLock;juce::WaitableEvent wake;juce::File next;uint64_t serial=0,warmSerial=0;juce::Array<juce::File> upcoming;juce::String warmSignature;
 bool canWarm(juce::File file){if(LibraryStore::playbackType(file)!=4||file.hasFileExtension("sfz;sgpreset"))return false;std::unique_ptr<juce::AudioFormatReader> reader(p.formats.createReaderFor(file));return reader&&reader->sampleRate>0&&reader->lengthInSamples/reader->sampleRate<=120&&(double)reader->lengthInSamples*reader->numChannels*4*1.15<=DecodedSampleCache::budget/3;}
 std::shared_ptr<const AudioData> readCached(juce::File file,std::function<bool()> cancel={}){if(!file.hasFileExtension("sfz;sgmulti"))if(auto data=DecodedSampleCache::instance().get(file))return data;auto modified=file.getLastModificationTime();auto size=file.getSize();auto data=p.readAudio(file,cancel);if(data&&!file.hasFileExtension("sfz;sgmulti")&&(!cancel||!cancel()))DecodedSampleCache::instance().put(file,data,modified,size);return data;}

public:
 std::atomic<bool> busy{false};std::atomic<double> lastLoadMs{0};std::function<void(bool,juce::File,uint64_t)> completed;
 explicit LoopLoader(Processor& processor):Thread("Slice Groups file loader"),p(processor){}
 ~LoopLoader()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 void request(juce::File file){const juce::ScopedLock g(queueLock);next=file;serial=++p.loadGeneration;busy=true;++LibraryPreparation::epoch;wake.signal();}
 void cancel(){const juce::ScopedLock g(queueLock);next=juce::File();serial=++p.loadGeneration;upcoming.clear();++warmSerial;busy=false;wake.signal();}
 void prepareUpcoming(juce::Array<juce::File> files,juce::String signature){const juce::ScopedLock g(queueLock);if(files==upcoming&&signature==warmSignature)return;upcoming=std::move(files);warmSignature=signature;++warmSerial;wake.signal();}
 void run()override{uint64_t warmed=0;std::unique_ptr<Processor> previewEngine;while(!threadShouldExit()){
  bool queued;{const juce::ScopedLock g(queueLock);queued=next!=juce::File();}if(!queued)wake.wait(50);if(threadShouldExit())break;juce::File file;uint64_t token,warmToken;juce::Array<juce::File> candidates;
  {const juce::ScopedLock g(queueLock);file=next;token=serial;next=juce::File();warmToken=warmSerial;candidates=upcoming;}
  if(file!=juce::File()){setPriority(juce::Thread::Priority::normal);LibraryPreparation::Foreground priority;auto started=juce::Time::getMillisecondCounterHiRes();auto preset=file.hasFileExtension("sgpreset")?juce::XmlDocument::parse(file):nullptr;auto source=preset?juce::File(preset->getStringAttribute("file")):file;auto data=preset&&preset->hasTagName("SliceGroupsRack")?std::shared_ptr<const AudioData>{}:preset?p.readAudioForConfiguration(source,*preset,[&]{return threadShouldExit()||token!=p.loadGeneration.load();}):readCached(source,[&]{return threadShouldExit()||token!=p.loadGeneration.load();});if(threadShouldExit()||token!=p.loadGeneration.load())continue;bool ok=file.hasFileExtension("sgpreset")?(preset&&p.installPreset(data,*preset,token)):p.installAudio(data,true,token);lastLoadMs=juce::Time::getMillisecondCounterHiRes()-started;auto callback=completed;juce::MessageManager::callAsync([token,callback,ok,file]{if(callback)callback(ok,file,token);});}
  if(warmed==warmToken||candidates.isEmpty()||LibraryPreparation::checksPaused||LibraryPreparation::suspended||LibraryPreparation::playbackBusy())continue;setPriority(juce::Thread::Priority::background);LibraryPreparation::WarmJob warmProgress;if(!warmProgress)continue;LibraryPreparation::Foreground warmingPriority;
  auto cancelled=[&]{const juce::ScopedLock g(queueLock);return threadShouldExit()||LibraryPreparation::checksPaused||LibraryPreparation::suspended||LibraryPreparation::playbackBusy()||next!=juce::File()||warmSerial!=warmToken||serial!=token;};
  for(int i=candidates.size();--i>=0;)if(!canWarm(candidates[i]))candidates.remove(i);if(candidates.isEmpty()){warmed=warmToken;continue;}
  // Decode upcoming files before waiting for the current stretch render. A click
  // can then use decoded audio even when speculative stretching is still pending.
  for(auto candidate:candidates){if(cancelled())break;if(LibraryStore::playbackType(candidate)==4&&!candidate.hasFileExtension("sgpreset"))readCached(candidate,cancelled);}
  while(p.stretchWorker.busy&&!cancelled())wake.wait(20);if(cancelled())continue;
  std::shared_ptr<const AudioData> current;std::unique_ptr<juce::XmlElement> settings;double tempo;bool host,locked,stretch;double bars;{const juce::ScopedLock g(p.lock);stretch=p.stretchMode&&!p.trackMode;current=p.audio;settings=p.configurationXml(true);tempo=p.hostBpm;host=p.hasHost;locked=p.loopLengthLocked;bars=p.lockedBars;}
  if(!previewEngine){previewEngine=std::make_unique<Processor>(false);previewEngine->trackOffline=true;previewEngine->historySuppressed=1;previewEngine->stretchWorker.signalThreadShouldExit();previewEngine->stretchWorker.stopThread(-1);}auto& preview=*previewEngine;preview.audio=current;if(current)preview.applyConfiguration(*settings,true);else{preview.pitchSemitones=settings->getDoubleAttribute("pitch");preview.keyLocked=settings->getBoolAttribute("keyLocked");preview.targetKey=settings->getIntAttribute("targetKey");}preview.swingLinked=false;preview.autoSave=false;preview.autoCategories=false;preview.loopLengthLocked=locked;preview.lockedBars=bars;
  for(auto candidate:candidates){if(cancelled())break;if(LibraryStore::playbackType(candidate)!=4)continue;auto preset=candidate.hasFileExtension("sgpreset")?juce::XmlDocument::parse(candidate):nullptr;auto source=preset?juce::File(preset->getStringAttribute("file")):candidate;auto data=readCached(source,cancelled);if(!data||cancelled())continue;
   preview.audio=current;if(current)preview.applyConfiguration(*settings,true);preview.swingLinked=false;preview.autoSave=false;preview.autoCategories=false;preview.hasHost=host;preview.hostBpm=tempo;
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
public:
 FolderAnalyser(Processor&,LoopLoader&):Thread("Slice Groups preparation dispatcher"){}
 void run()override{}
 void request(juce::Array<juce::File> list){if(!threadShouldExit()&&!list.isEmpty())LibraryPreparation::request(std::move(list));}
};

// Enumerates names only. No decoding, hashing, or transient analysis for search.
class BrowserSearchIndex final:public juce::Thread {
    juce::CriticalSection mutex;juce::WaitableEvent wake;juce::File root;bool forceRefresh=false;juce::StringArray changedPaths;std::atomic<uint64_t> revision{0};
public:
    struct PreparedView{juce::Array<juce::File> files,folders,direct;std::set<juce::String> folderPaths;std::map<juce::String,int> nameCounts;};
    std::function<void(juce::File,std::shared_ptr<LocalDatabase::CatalogDelta>,uint64_t)> completedDelta;
    std::function<void(juce::File,std::shared_ptr<PreparedView>,uint64_t)> completedPrepared;
    std::function<void(juce::File,juce::Array<juce::File>,uint64_t)> completed;
    BrowserSearchIndex():Thread("Slice Groups browser search"){startThread(juce::Thread::Priority::low);}
    ~BrowserSearchIndex()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
    uint64_t request(juce::File folder,bool refresh=false){const juce::ScopedLock g(mutex);root=folder;forceRefresh=refresh;auto token=++revision;wake.signal();return token;}
    uint64_t requestChanges(juce::File folder,juce::StringArray paths){const juce::ScopedLock g(mutex);root=folder;forceRefresh=false;for(auto path:paths)changedPaths.addIfNotAlreadyThere(path);auto token=++revision;wake.signal();return token;}
    static std::shared_ptr<LocalDatabase::CatalogDelta> changesFor(const juce::StringArray& paths){
      auto delta=std::make_shared<LocalDatabase::CatalogDelta>();auto db=LibraryStore::database();juce::AudioFormatManager formats;formats.registerBasicFormats();std::set<juce::String> added;
      auto add=[&](juce::File file,bool folder){auto path=file.getFullPathName();if(added.insert(path).second)delta->added.push_back({path,file.getParentDirectory().getFullPathName(),file.getFileName(),folder});};
      auto supported=[&](juce::File f){return f.hasFileExtension("sfz;sgmulti;mid;midi")||formats.findFormatForFileExtension(f.getFileExtension());};
      for(auto path:paths){juce::File item(path);if(!item.exists()){delta->removed.addIfNotAlreadyThere(path);continue;}if(item.isSymbolicLink()||item.getFileName().startsWithChar('.'))continue;
       if(!item.isDirectory()){if(supported(item)){delta->removed.addIfNotAlreadyThere(path);add(item,false);}continue;}
       bool known=db->catalogContains(item);add(item,true);if(!known){for(auto file:enumerate(item,{},true))add(file,file.isDirectory());continue;}
       auto old=db->catalogChildren(item);std::set<juce::String> present,knownChildren;for(auto file:old)knownChildren.insert(file.getFullPathName());for(auto entry:juce::RangedDirectoryIterator(item,false,"*",juce::File::findFilesAndDirectories)){auto file=entry.getFile();present.insert(file.getFullPathName());if(file.getFileName().startsWithChar('.')||file.isSymbolicLink())continue;if(knownChildren.count(file.getFullPathName()))continue;if(file.isDirectory()){add(file,true);for(auto child:enumerate(file,{},true))add(child,child.isDirectory());}else if(supported(file))add(file,false);}
       for(auto file:old)if(!present.count(file.getFullPathName()))delta->removed.addIfNotAlreadyThere(file.getFullPathName());
      }return delta;
    }
    static juce::Array<juce::File> enumerate(juce::File folder,std::function<bool()> cancelled={},bool includeFolders=false){
        juce::AudioFormatManager formats;formats.registerBasicFormats();juce::Array<juce::File> result;std::vector<juce::File> pending{folder};
        while(!pending.empty()){if(cancelled&&cancelled())return {};auto current=pending.back();pending.pop_back();for(auto entry:juce::RangedDirectoryIterator(current,false,"*",juce::File::findFilesAndDirectories)){if(cancelled&&cancelled())return {};auto file=entry.getFile();if(file.getFileName().startsWithChar('.')||file.isSymbolicLink())continue;if(file.isDirectory()){pending.push_back(file);if(includeFolders)result.add(file);}else if(file.hasFileExtension("sfz;sgmulti;mid;midi")||formats.findFormatForFileExtension(file.getFileExtension()))result.add(file);}}
        struct Sort{int compareElements(const juce::File& a,const juce::File& b)const{int n=a.getFileName().compareIgnoreCase(b.getFileName());return n?n:a.getFullPathName().compareIgnoreCase(b.getFullPathName());}} sort;result.sort(sort);return result;
    }
    static juce::File indexFile(juce::File root){return LibraryStore::root().getChildFile("search-index").getChildFile(juce::String::toHexString(root.getFullPathName().hashCode64())+".json");}
    static bool indexExists(juce::File root){try{return LibraryStore::database()->hasCatalog(root);}catch(...){return false;}}
    static void invalidate(juce::File root){LibraryStore::database()->invalidateCatalog(root);}
    static void store(juce::File root,const juce::Array<juce::File>& files){LibraryStore::database()->setCatalog(root,files);}
    static juce::Array<juce::File> cached(juce::File root){try{return LibraryStore::database()->catalog(root);}catch(...){return {};}}
    void run()override{uint64_t last=0;while(!threadShouldExit()){wake.wait(100);if(threadShouldExit())break;juce::File folder;uint64_t token;bool refresh;juce::StringArray changes;{const juce::ScopedLock g(mutex);folder=root;token=revision.load();refresh=forceRefresh;changes.swapWith(changedPaths);}if(token==last)continue;last=token;if(!changes.isEmpty()&&!changes.contains("*")&&completedDelta){try{auto delta=changesFor(changes);auto callback=completedDelta;juce::MessageManager::callAsync([callback,folder,delta,token]{callback(folder,delta,token);});LibraryStore::database()->applyCatalogDelta(*delta);}catch(const std::exception& e){std::cerr<<"Browser delta: "<<e.what()<<"\n";}continue;}if(!folder.isDirectory())continue;auto cancelled=[&]{return threadShouldExit()||revision.load()!=token;};auto roots=folder==LibraryStore::browserRoot()?LibraryStore::libraryRoots():juce::Array<juce::File>{folder};auto deliver=[&](juce::Array<juce::File> found){auto callback=completed;auto preparedCallback=completedPrepared;
            if(preparedCallback){auto view=std::make_shared<PreparedView>();for(auto file:found){if(cancelled())return;if(file.getParentDirectory()==folder)view->direct.add(file);if(file.isDirectory()){view->folders.add(file);view->folderPaths.insert(file.getFullPathName());}else{view->files.add(file);++view->nameCounts[file.getFileName().toLowerCase()];}}
                juce::MessageManager::callAsync([preparedCallback,folder,view,token]{preparedCallback(folder,view,token);});
            }else juce::MessageManager::callAsync([callback,folder,found=std::move(found),token]()mutable{if(callback)callback(folder,std::move(found),token);});};juce::Array<juce::File> stored;bool hasCache=true;for(auto root:roots){hasCache&=indexExists(root);if(folder==LibraryStore::browserRoot())stored.add(root);stored.addArray(cached(root));}
if(hasCache&&!cancelled()){struct Sort{int compareElements(const juce::File&a,const juce::File&b)const{int n=a.getFileName().compareIgnoreCase(b.getFileName());return n?n:a.getFullPathName().compareIgnoreCase(b.getFullPathName());}}sort;stored.sort(sort);for(int i=stored.size()-1;i>0;--i)if(stored[i]==stored[i-1])stored.remove(i);deliver(stored);if(!refresh&&!changes.contains("*"))continue;}juce::Array<juce::File> fresh;for(auto root:roots){if(cancelled())break;if(folder==LibraryStore::browserRoot())fresh.add(root);auto found=enumerate(root,cancelled,true);if(cancelled())break;try{store(root,found);}catch(const std::exception& e){std::cerr<<"Browser index: "<<e.what()<<"\n";}fresh.addArray(found);}if(!cancelled()){struct Sort{int compareElements(const juce::File&a,const juce::File&b)const{int n=a.getFileName().compareIgnoreCase(b.getFileName());return n?n:a.getFullPathName().compareIgnoreCase(b.getFullPathName());}}sort;fresh.sort(sort);for(int i=fresh.size()-1;i>0;--i)if(fresh[i]==fresh[i-1])fresh.remove(i);deliver(std::move(fresh));}}}

};

namespace LibraryPreparation {
class Worker final:private juce::Thread,public juce::DeletedAtShutdown {
 struct Job{juce::File scope;uint64_t cancellation=0;};
 juce::CriticalSection mutex;juce::WaitableEvent wake;std::vector<Job> queue;std::atomic<uint64_t> cancellation{0};std::atomic<int> queued{0},done{0},total{0};std::atomic<bool> active{false};
 void run()override{Processor decoder(false);decoder.trackOffline=true;decoder.autoSave=false;decoder.stretchWorker.signalThreadShouldExit();decoder.stretchWorker.stopThread(-1);
  while(!threadShouldExit()){
   if(checksPaused||suspended||foreground>0||playbackBusy()){wake.wait(30);continue;}
   Job job;{const juce::ScopedLock guard(mutex);if(!queue.empty()){job=queue.front();queue.erase(queue.begin());queued=(int)queue.size();}}
   if(job.scope==juce::File()){active=false;wake.wait(100);continue;}active=true;done=0;total=0;
   auto aborted=[&]{return threadShouldExit()||checksPaused||suspended||cancellation.load()!=job.cancellation;};
   auto files=job.scope.isDirectory()?BrowserSearchIndex::enumerate(job.scope,[&]{while(!aborted()&&(foreground>0||playbackBusy()))wake.wait(30);return aborted();},false):juce::Array<juce::File>{job.scope};
   if(aborted()){active=false;continue;}
   auto types=LibraryStore::read("playback-types.json");for(int i=files.size();--i>=0;)if(LibraryStore::typeFromValues(files[i],types)!=4||files[i].hasFileExtension("sfz;sgmulti;mid;midi"))files.remove(i);total=files.size();
   for(int i=0;i<files.size()&&!aborted();){
    if(foreground>0||playbackBusy()){wake.wait(30);continue;}auto file=files[i];auto token=epoch.load();auto cancelled=[&]{return aborted()||foreground>0||playbackBusy()||epoch.load()!=token;};
    if(!file.existsAsFile()||LibraryStore::playbackType(file)!=4||(AnalysisStore::exists(file)&&LoopLoudness::exists(file))){++i;++done;continue;}
    std::unique_ptr<juce::AudioFormatReader> header(decoder.formats.createReaderFor(file));if(!header||header->sampleRate<=0||header->lengthInSamples/header->sampleRate>120||(double)header->lengthInSamples*header->numChannels*4>DecodedSampleCache::budget){++i;++done;continue;}header.reset();
    // Whole-folder preparation persists source analysis only. It must not evict
    // nearby decoded/rendered samples from the bounded audition caches.
    auto data=decoder.readAudio(file,cancelled);if(cancelled()){wake.wait(20);continue;}++i;++done;wake.wait(10);
   }active=false;
  }
 }
public:
 inline static std::atomic<Worker*> instance{nullptr};
 Worker():juce::Thread("Slice Groups selected Stretch preparation"){startThread(juce::Thread::Priority::background);}
 ~Worker()override{signalThreadShouldExit();wake.signal();stopThread(-1);instance.store(nullptr);}
 void enqueue(juce::File scope){if(checksPaused||scope==juce::File()||scope==LibraryStore::browserRoot())return;suspended=false;const juce::ScopedLock guard(mutex);for(auto& job:queue)if(job.scope==scope)return;queue.push_back({scope,cancellation.load()});queued=(int)queue.size();wake.signal();}
 void cancelAll(){suspended=true;++cancellation;++epoch;const juce::ScopedLock guard(mutex);queue.clear();queued=0;active=false;wake.signal();}
 bool isBusy()const{return active||queued>0;}
 juce::String statusText(){if(!isBusy())return {};if(playbackBusy())return "STRETCH PREP: PAUSED (PLAYBACK)";if(foreground>0)return "STRETCH PREP: WAITING";return total>0?"STRETCH PREP "+juce::String(done.load())+" / "+juce::String(total.load()):"STRETCH PREP: SCANNING...";}
};
inline Worker& worker(){static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);auto* instance=Worker::instance.load();if(!instance){instance=new Worker;Worker::instance=instance;}return *instance;}
// Browsing/importing/Refresh never starts library preparation.
inline void request(juce::Array<juce::File>){}
inline void requestScope(juce::File){}
inline void requestStretch(juce::File folder){if(!checksPaused)worker().enqueue(folder);}
inline void cancel(){suspended=true;++epoch;if(auto* instance=Worker::instance.load())instance->cancelAll();}
inline bool busy(){auto* instance=Worker::instance.load();return !checksPaused&&!suspended&&(warmJobs>0||(instance&&instance->isBusy()));}
inline juce::String status(){if(checksPaused||suspended)return {};if(auto* instance=Worker::instance.load()){auto text=instance->statusText();if(text.isNotEmpty())return text;}return warmJobs>0?"PREPARING NEARBY STRETCH...":juce::String();}
}

// Undo records retain the actual macOS Trash URL, never an inferred filename.
class FileTrashWorker final:private juce::Thread {
 struct Item{juce::File original,location;bool trashed=false;};
 struct Batch{uint64_t order=0;std::vector<Item> items;};
 struct Job{std::shared_ptr<Batch> batch;bool restore=false,initial=false;uint64_t epoch=0;};
 juce::CriticalSection mutex;juce::WaitableEvent wake;std::vector<Job> queue;std::vector<std::shared_ptr<Batch>> undo,redo;
 juce::Array<juce::File> hidden;std::set<juce::String> pending;bool historyBusy=false;uint64_t redoEpoch=0;
 using Callback=std::function<void(juce::Array<juce::File>,juce::Array<juce::File>,bool)>;Callback completed;
 std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);juce::String errorText;
 static bool covered(juce::File file,const juce::Array<juce::File>& roots){for(auto root:roots)if(file==root||file.isAChildOf(root))return true;return false;}
 void updateIndexes(const juce::Array<juce::File>& changed,bool){juce::StringArray paths;for(auto file:changed)paths.add(file.getFullPathName());auto delta=BrowserSearchIndex::changesFor(paths);LibraryStore::database()->applyCatalogDelta(*delta);}
 void run()override{while(!threadShouldExit()){
  Job job;{const juce::ScopedLock guard(mutex);if(!queue.empty()){job=queue.front();queue.erase(queue.begin());}}if(!job.batch){wake.wait(100);continue;}
  juce::Array<juce::File> changed,failed;juce::String error;
  for(auto& item:job.batch->items){if(job.restore&&!item.trashed||!job.restore&&item.trashed)continue;bool ok=job.restore?NativeTrash::restore(item.location,item.original,error):NativeTrash::move(item.original,item.location,error);
   if(ok){item.trashed=!job.restore;changed.add(item.original);}else failed.add(item.original);
   {const juce::ScopedLock guard(mutex);pending.erase(item.original.getFullPathName());if(job.restore&&ok||!job.restore&&!ok)hidden.removeAllInstancesOf(item.original);else if(!job.restore&&ok)hidden.addIfNotAlreadyThere(item.original);}
  }
  try{updateIndexes(changed,job.restore);}catch(const std::exception& e){std::cerr<<"Trash index refresh: "<<e.what()<<"\n";}
  Callback callback;{const juce::ScopedLock guard(mutex);errorText=error;for(auto& item:job.batch->items)pending.erase(item.original.getFullPathName());
   auto erase=[&](auto& list){list.erase(std::remove(list.begin(),list.end(),job.batch),list.end());};
   if(job.initial){if(std::none_of(job.batch->items.begin(),job.batch->items.end(),[](const Item& i){return i.trashed;}))erase(undo);}
   else if(failed.isEmpty()){erase(job.restore?undo:redo);if(job.restore){if(job.epoch==redoEpoch)redo.push_back(job.batch);}else undo.push_back(job.batch);}
   if(!job.initial)historyBusy=false;callback=completed;
  }
  auto life=alive;juce::MessageManager::callAsync([life,callback,changed,failed,restoring=job.restore]{if(life->load()&&callback)callback(changed,failed,restoring);});
 }}
public:
 FileTrashWorker():juce::Thread("Slice Groups trash"){startThread(juce::Thread::Priority::low);}
 ~FileTrashWorker()override{*alive=false;signalThreadShouldExit();wake.signal();stopThread(-1);}
 void setCompleted(Callback callback){const juce::ScopedLock guard(mutex);completed=std::move(callback);}
 void discardRedo(){const juce::ScopedLock guard(mutex);redo.clear();++redoEpoch;}
 void request(juce::Array<juce::File> files){auto batch=std::make_shared<Batch>();batch->order=Processor::nextHistoryOrder();
  // Collapse parent/child selections so one folder is never trashed twice.
  for(auto file:files){bool nested=false;for(auto parent:files)if(parent!=file&&file.isAChildOf(parent)){nested=true;break;}if(!nested)batch->items.push_back({file,{ },false});}
  const juce::ScopedLock guard(mutex);batch->items.erase(std::remove_if(batch->items.begin(),batch->items.end(),[&](const Item& item){return !pending.insert(item.original.getFullPathName()).second;}),batch->items.end());
  if(batch->items.empty())return;for(auto& item:batch->items)hidden.addIfNotAlreadyThere(item.original);redo.clear();++redoEpoch;undo.push_back(batch);while(undo.size()>100)undo.erase(undo.begin());queue.push_back({batch,false,true,redoEpoch});wake.signal();
 }
 uint64_t nextOrder(bool redoAction){const juce::ScopedLock guard(mutex);const auto& entries=redoAction?redo:undo;return historyBusy||entries.empty()?0:entries.back()->order;}
 bool undoRedo(bool redoAction){const juce::ScopedLock guard(mutex);auto& entries=redoAction?redo:undo;if(historyBusy||entries.empty())return false;historyBusy=true;auto batch=entries.back();for(auto& item:batch->items)pending.insert(item.original.getFullPathName());queue.push_back({batch,!redoAction,false,redoEpoch});wake.signal();return true;}
 bool historyPending(){const juce::ScopedLock guard(mutex);return historyBusy;}
 juce::String lastError(){const juce::ScopedLock guard(mutex);return errorText;}
 juce::Array<juce::File> hiddenFiles(bool restored=false){const juce::ScopedLock guard(mutex);if(restored)for(int i=hidden.size();--i>=0;)if(!pending.count(hidden[i].getFullPathName())&&hidden[i].exists())hidden.remove(i);return hidden;}
};
