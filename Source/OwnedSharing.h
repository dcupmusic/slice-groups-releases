#pragma once
// Dropbox transports owner-written, portable snapshots. The live SQLite database
// stays local; collaborators never write metadata into another owner's library.
class OwnedLibrarySharing final:private juce::Thread,public juce::DeletedAtShutdown {
 friend int runSharing428();
 using Layers=std::map<juce::String,juce::var>;
 struct View{Layers merged,remote;std::map<juce::String,juce::String> owners;juce::String me,error;};
 struct Cancelled:std::exception{const char* what()const noexcept override{return "Sharing is stopping";}};
 std::shared_ptr<const View> published=std::make_shared<const View>();
 juce::WaitableEvent wake;juce::String me,scanFingerprint,remoteFingerprint;juce::CriticalSection identityMutex,scanMutex;
 std::map<juce::String,juce::String> stamps,documentStamps;std::map<juce::String,juce::var> documents;
 std::map<juce::String,uint64_t> revisions;
 std::atomic<int> phase{0};std::atomic<uint64_t> writtenBytes{0},mergePasses{0};
 struct Phase {std::atomic<int>& value;int prior;Phase(std::atomic<int>& v,int next):value(v),prior(v.exchange(next)){}~Phase(){value=prior;}};
 static constexpr const char* kinds[]{"tags.json","keys.json","playback-types.json","loops.json","folder-colours.json","musical-keys.json","parameter-edits.json"};
 void checkpoint()const{if(threadShouldExit())throw Cancelled();}
 juce::var local(juce::String kind){checkpoint();return LibraryStore::database()->snapshot(kind,[this]{checkpoint();});}
 static bool inside(juce::String path,juce::File root){return path==root.getFullPathName()||path.startsWith(root.getFullPathName()+juce::File::getSeparatorString());}
 static bool relativeOK(juce::String path){if(path==".")return true;if(path.isEmpty()||juce::File::isAbsolutePath(path)||path.containsChar('\\'))return false;for(auto part:juce::StringArray::fromTokens(path,"/",""))if(part==".."||part=="."||part.isEmpty())return false;return !path.startsWithChar('.');}
 static juce::String fileStamp(juce::File file){return juce::String(file.getLastModificationTime().toMilliseconds())+":"+juce::String(file.getSize());}
 // JSON formatting writes into a temporary file. Cancellation only discards
 // that temporary file; the last complete snapshot stays in place.
 class SharingOutput final:public juce::OutputStream {
  OwnedLibrarySharing& owner;juce::OutputStream& destination;
 public:
  bool good=true;
  SharingOutput(OwnedLibrarySharing& service,juce::OutputStream& output):owner(service),destination(output){}
  void flush()override{destination.flush();}
  bool setPosition(juce::int64 position)override{return destination.setPosition(position);}
  juce::int64 getPosition()override{return destination.getPosition();}
  bool write(const void* source,size_t bytes)override{auto* data=static_cast<const char*>(source);while(bytes){owner.checkpoint();auto count=juce::jmin(bytes,size_t(65536));if(!destination.write(data,count)){good=false;return false;}owner.writtenBytes.fetch_add(count,std::memory_order_relaxed);data+=count;bytes-=count;}return true;}
 };
 bool atomicJSON(juce::File file,const juce::var& data){
  checkpoint();Phase writing(phase,3);if(!file.getParentDirectory().createDirectory())return false;
  juce::TemporaryFile temporary(file);bool good=false;
  {juce::FileOutputStream destination(temporary.getFile(),65536);if(!destination.openedOk())return false;SharingOutput output(*this,destination);juce::JSON::writeToStream(output,data);output.flush();good=output.good&&destination.getStatus().wasOk();}
  checkpoint();return good&&temporary.overwriteTargetFileWithTemporary();
 }
 juce::var readDocument(juce::File file){checkpoint();auto key=file.getFullPathName(),stamp=fileStamp(file);if(!documents.count(key)||documentStamps[key]!=stamp){auto doc=juce::JSON::parse(file);checkpoint();if(doc.isObject()){documents[key]=doc;documentStamps[key]=stamp;}}auto found=documents.find(key);return found==documents.end()?juce::var():found->second;}
 void identity(){const juce::ScopedLock lock(identityMutex);if(me.isNotEmpty())return;auto file=LibraryStore::root().getChildFile("sharing-owner-id.txt");juce::InterProcessLock guard("SliceGroupsSharingIdentity-"+juce::String(LibraryStore::root().getFullPathName().hashCode64()));if(!guard.enter(1000))return;me=file.loadFileAsString().trim();if(me.isEmpty()){me=juce::Uuid().toString();file.getParentDirectory().createDirectory();file.replaceWithText(me);}guard.exit();}

 void scan(){
  const juce::ScopedLock scanGuard(scanMutex);checkpoint();Phase scanning(phase,1);identity();checkpoint();if(me.isEmpty())return;
  auto prior=std::atomic_load(&published);auto next=std::make_shared<View>();next->me=me;
  auto roots=LibraryStore::libraryRoots();juce::String fingerprint;std::map<juce::String,uint64_t> serials;
  for(auto root:roots){checkpoint();auto folder=root.getChildFile(".slicegroups/sharing-v2");auto manifest=folder.getChildFile("owner.json");fingerprint+=root.getFullPathName()+":"+fileStamp(manifest);for(auto file:folder.findChildFiles(juce::File::findFiles,false,"*.json")){checkpoint();fingerprint+=file.getFileName()+fileStamp(file);}}
  for(auto kind:kinds){checkpoint();auto serial=LibraryStore::database()->metadataSerial(kind);serials[kind]=serial;fingerprint+=juce::String((juce::int64)serial)+":";}
  fingerprint+=juce::String((juce::int64)LibraryStore::database()->metadataSerial("shared-root-paths.json"));
  if(fingerprint==scanFingerprint)return;
  Layers locals;bool readOnly=false,retry=false;juce::String remoteToken;
  auto getLocal=[&](juce::String kind)->juce::var{if(!locals.count(kind))locals[kind]=local(kind);return locals[kind];};
  for(auto root:roots){
   checkpoint();auto folder=root.getChildFile(".slicegroups/sharing-v2");auto manifest=juce::JSON::parse(folder.getChildFile("owner.json"));auto owner=manifest.getProperty("owner",{}).toString();
   if(owner.length()<32||owner.length()>36||!owner.containsOnly("0123456789abcdefABCDEF-")||(int)manifest.getProperty("version",0)!=2)continue;
   next->owners[root.getFullPathName()]=owner;auto remembered=local("library-owners.json");
   if(remembered.getProperty(root.getFullPathName(),{}).toString()!=owner)LibraryStore::database()->mutate("library-owners.json",[&](juce::DynamicObject& o){o.setProperty(root.getFullPathName(),owner);});
   auto file=folder.getChildFile(owner+".json");
   if(owner==me){
    auto previous=readDocument(file);auto rootId=previous.getProperty("rootId",{}).toString();auto registry=local("shared-root-paths.json");
    if(rootId.isEmpty()){
     rootId=juce::Uuid().toString();juce::String origin=root.getFullPathName();bool portable=false,localData=false;auto fields=previous.getProperty("fields",{});
     for(auto kind:kinds){checkpoint();auto values=getLocal(kind);if(auto* object=fields.getProperty(kind,{}).getDynamicObject())portable|=!object->getProperties().isEmpty();for(auto& item:values.getDynamicObject()->getProperties()){checkpoint();localData|=inside(item.name.toString(),root);}}
     // A pre-upgrade snapshot at a new path must not be overwritten with emptiness.
     if(portable&&!localData)origin="unresolved";
     checkpoint();LibraryStore::database()->mutate("shared-root-paths.json",[&](juce::DynamicObject& object){object.setProperty(rootId,origin);});
     if(previous.isObject()){juce::var copy(new juce::DynamicObject());for(auto& item:previous.getDynamicObject()->getProperties()){checkpoint();copy.getDynamicObject()->setProperty(item.name,item.value);}previous=copy;previous.getDynamicObject()->setProperty("rootId",rootId);if(!atomicJSON(file,previous)){retry=true;continue;}}
     registry=local("shared-root-paths.json");
    }
    if(registry.getProperty(rootId,{}).toString()!=root.getFullPathName()){next->error="Use Relocate Library to link the moved library: "+root.getFileName();continue;}
    // Bind the publication stamp to the revisions captured before its snapshots.
    // A concurrent local edit therefore always triggers another publication.
    juce::String stamp;for(auto kind:kinds)stamp+=juce::String((juce::int64)serials[kind])+":";
    auto key=root.getFullPathName();
    if(stamps[key]!=stamp||!file.existsAsFile()){
     Phase preparing(phase,2);juce::var document(new juce::DynamicObject()),fields(new juce::DynamicObject());auto* object=document.getDynamicObject();object->setProperty("version",2);object->setProperty("owner",me);object->setProperty("rootId",rootId);
     for(auto kind:kinds){
      checkpoint();auto source=getLocal(kind);juce::var values(new juce::DynamicObject());
      for(auto& item:source.getDynamicObject()->getProperties()){
       checkpoint();auto path=item.name.toString();if(!juce::File::isAbsolutePath(path)||!inside(path,root))continue;
       auto relative=path==root.getFullPathName()?juce::String("."):juce::File(path).getRelativePathFrom(root).replaceCharacter('\\','/');if(!relativeOK(relative))continue;
       auto value=item.value;if(juce::String(kind)=="loops.json")if(auto xml=juce::parseXML(value.toString())){xml->removeAttribute("file");xml->removeAttribute("normalise");xml->removeAttribute("normaliseLufs");xml->removeAttribute("targetLufs");value=xml->toString();}
       values.getDynamicObject()->setProperty(relative,value);
      }
      fields.getDynamicObject()->setProperty(kind,values);
     }
     object->setProperty("fields",fields);
     if(atomicJSON(file,document)){stamps[key]=stamp;documents[file.getFullPathName()]=document;documentStamps[file.getFullPathName()]=fileStamp(file);}else{retry=true;next->error="Could not publish shared metadata: "+root.getFileName();}
    }
   }else{
    readOnly=true;remoteToken+=root.getFullPathName()+"|"+owner+"|"+fileStamp(file)+";";
    auto doc=readDocument(file);if(!doc.isObject()||(int)doc.getProperty("version",0)!=2||doc.getProperty("owner",{}).toString()!=owner)continue;
    auto fields=doc.getProperty("fields",{});if(!fields.isObject())continue;
    for(auto kind:kinds){
     checkpoint();auto values=fields.getProperty(kind,{});if(!values.isObject())continue;auto& layer=next->remote[kind];if(!layer.isObject())layer=juce::var(new juce::DynamicObject());
     for(auto& item:values.getDynamicObject()->getProperties()){
      checkpoint();auto path=item.name.toString();if(!relativeOK(path))continue;auto target=path=="."?root:root.getChildFile(path);auto value=item.value;
      if(juce::String(kind)=="loops.json")if(auto xml=juce::parseXML(value.toString())){xml->setAttribute("file",target.getFullPathName());auto& keys=next->remote["keys.json"];if(!keys.isObject())keys=juce::var(new juce::DynamicObject());if(!keys.getDynamicObject()->hasProperty(target.getFullPathName()))keys.getDynamicObject()->setProperty(target.getFullPathName(),xml->getIntAttribute("loopKey",-1));value=xml->toString();}
      layer.getDynamicObject()->setProperty(target.getFullPathName(),value);
     }
    }
   }
  }
  bool ownersChanged=prior->owners!=next->owners;std::vector<juce::String> changedKinds;
  // Owner-only libraries read SQLite directly: there is no merged overlay to
  // construct, clone, or stringify. Read-only overlays reuse unchanged layers.
  if(readOnly)for(auto kind:kinds){
   checkpoint();auto found=prior->merged.find(kind);
   if(!ownersChanged&&remoteFingerprint==remoteToken&&revisions[kind]==serials[kind]&&found!=prior->merged.end()){next->merged[kind]=found->second;continue;}
   Phase merging(phase,4);++mergePasses;auto source=getLocal(kind);juce::var merged(new juce::DynamicObject());
   for(auto& item:source.getDynamicObject()->getProperties()){checkpoint();bool foreign=false;for(auto& entry:next->owners)if(entry.second!=me&&inside(item.name.toString(),juce::File(entry.first))){foreign=true;break;}if(!foreign)merged.getDynamicObject()->setProperty(item.name,item.value);}
   if(auto* remote=next->remote[kind].getDynamicObject())for(auto& item:remote->getProperties()){checkpoint();merged.getDynamicObject()->setProperty(item.name,item.value);}
   next->merged[kind]=merged;changedKinds.push_back(kind);
  }
  for(auto& item:prior->merged)if(!next->merged.count(item.first))changedKinds.push_back(item.first);
  checkpoint();if(ownersChanged||!changedKinds.empty()||prior->me!=me||prior->error!=next->error)std::atomic_store(&published,std::shared_ptr<const View>(next));
  for(auto kind:changedKinds)LibraryStore::metadataChanged(kind);
  // Do not acknowledge a cancelled or failed scan: the next run retries it.
  revisions=std::move(serials);remoteFingerprint=remoteToken;if(!retry)scanFingerprint=fingerprint;
 }
 void run()override{while(!threadShouldExit()){try{scan();}catch(const Cancelled&){break;}catch(const std::exception& e){std::cerr<<"Sharing: "<<e.what()<<"\n";}if(!threadShouldExit())wake.wait(2000);}}
public:
 explicit OwnedLibrarySharing(bool start=true):Thread("Slice Groups Dropbox sharing"){if(start)startThread(juce::Thread::Priority::background);}
 ~OwnedLibrarySharing()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 static OwnedLibrarySharing& instance(){static auto* service=new OwnedLibrarySharing();return *service;}
 bool editable(juce::File file)const{auto view=std::atomic_load(&published);for(auto& owner:view->owners)if(owner.second!=view->me&&inside(file.getFullPathName(),juce::File(owner.first)))return false;return true;}
 juce::String status(juce::File root)const{auto view=std::atomic_load(&published);auto owner=view->owners.find(root.getFullPathName());return owner==view->owners.end()?"Not shared":owner->second==view->me?"Shared: owner":"Shared: read-only";}
 juce::var snapshot(juce::String kind,bool remote=false)const{auto view=std::atomic_load(&published);if(!remote){bool readOnly=false;for(auto& owner:view->owners)readOnly|=owner.second!=view->me;if(!readOnly)return {};}auto& layers=remote?view->remote:view->merged;auto found=layers.find(kind);return found==layers.end()?juce::var():found->second;}
 bool claim(juce::File root){const juce::ScopedLock scanGuard(scanMutex);if(!editable(root))return false;identity();if(me.isEmpty())return false;auto file=root.getChildFile(".slicegroups/sharing-v2/owner.json");if(file.existsAsFile()){auto current=juce::JSON::parse(file);if(current.getProperty("owner",{}).toString()!=me)return false;}auto* data=new juce::DynamicObject();data->setProperty("version",2);data->setProperty("owner",me);bool ok=atomicJSON(file,juce::var(data));wake.signal();return ok;}
 juce::Result relocate(juce::File oldRoot,juce::File newRoot){const juce::ScopedLock scanGuard(scanMutex);try{
   identity();if(!newRoot.isDirectory())throw std::runtime_error("Choose the library folder at its new location.");
   auto marker=juce::JSON::parse(newRoot.getChildFile(".slicegroups/sharing-v2/owner.json"));auto owner=marker.getProperty("owner",{}).toString();auto oldMarker=juce::JSON::parse(oldRoot.getChildFile(".slicegroups/sharing-v2/owner.json"));auto oldOwner=oldMarker.getProperty("owner",{}).toString();auto view=std::atomic_load(&published);auto found=view->owners.find(oldRoot.getFullPathName());if(oldOwner.isEmpty()&&found!=view->owners.end())oldOwner=found->second;if(oldOwner.isEmpty())oldOwner=LibraryStore::readLocal("library-owners.json").getProperty(oldRoot.getFullPathName(),{}).toString();
   if(oldOwner.isNotEmpty()&&oldOwner!=owner)throw std::runtime_error("The shared library owner does not match. Move the entire folder, including its hidden .slicegroups folder.");
   auto document=owner.isNotEmpty()?juce::JSON::parse(newRoot.getChildFile(".slicegroups/sharing-v2/"+owner+".json")):juce::var();auto rootId=document.getProperty("rootId",{}).toString();
   if(owner.isNotEmpty()&&owner!=me){bool localShared=false;for(auto kind:{"tags.json","keys.json","playback-types.json","loops.json","folder-colours.json","musical-keys.json","parameter-edits.json"}){auto values=LibraryStore::readLocal(kind);for(auto& item:values.getDynamicObject()->getProperties())localShared|=inside(item.name.toString(),oldRoot);}if(localShared)throw std::runtime_error("The destination belongs to another owner. Choose the matching library.");}
   // Reject an empty/wrong folder when none of the known sample paths exist there.
   int known=0,present=0;auto saved=LibraryStore::savedKeySummary();for(auto& item:saved.getDynamicObject()->getProperties()){auto path=item.name.toString();if(inside(path,oldRoot)){++known;auto relative=juce::File(path).getRelativePathFrom(oldRoot);if(newRoot.getChildFile(relative).existsAsFile())++present;}}if(known&&present==0)throw std::runtime_error("No saved samples were found at that location. Choose the moved library folder itself.");
   LibraryStore::database()->relocateLibrary(oldRoot,newRoot,owner==me?rootId:juce::String());LibraryStore::pollDatabase();scanFingerprint.clear();stamps.clear();documentStamps.clear();documents.clear();revisions.clear();remoteFingerprint.clear();scan();wake.signal();return juce::Result::ok();
  }catch(const std::exception& e){return juce::Result::fail(e.what());}}
 void refresh(){wake.signal();}
};
