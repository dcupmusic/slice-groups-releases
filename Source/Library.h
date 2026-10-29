#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include <set>
#include <thread>
#include <iostream>
#include <juce_cryptography/juce_cryptography.h>
#include "LocalDatabase.h"
#include "MusicalKey.h"

// Personal state stays local. Shared sample metadata lives below each library root. Test suites
// can redirect storage into work/ without modifying the user's real library.
class LibraryStore {
public:
    static constexpr bool usingDatabase=true;
    static std::shared_ptr<LocalDatabase> database(){return LocalDatabase::open(root());}
    static juce::String& storageError(){static thread_local juce::String message;return message;}
    static void pollDatabase(){try{for(auto kind:database()->takeChanges())metadataChanged(kind);}catch(const std::exception& e){storageError()=e.what();}}
    static juce::var savedKeySummary(){try{return database()->savedKeys();}catch(const std::exception& e){storageError()=e.what();return juce::var(new juce::DynamicObject());}}
    #if SLICE_GROUPS_LEGACY_TRIAL
    inline static bool diagnosticOldSave=true,diagnosticSharing=false;
#else
    inline static bool diagnosticOldSave=false,diagnosticSharing=false;
#endif
    static juce::File root() {
        auto overridePath = juce::SystemStats::getEnvironmentVariable("SLICE_GROUPS_LIBRARY",{});
        return overridePath.isNotEmpty() ? juce::File(overridePath)
            : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("SliceGroups");
    }
    static std::atomic<uint64_t>& metadataRevision(){static std::atomic<uint64_t> revision{1};return revision;}
    static std::atomic<uint64_t>& kindRevision(const juce::String& name){
        static std::atomic<uint64_t> versions[12]{};
        static const char* names[]{"loops.json","loop-index-v2.json","tags.json","keys.json","playback-types.json","favourites.json","track-prepared-v1.json","settings.json","folder-colours.json","musical-keys.json","parameter-edits.json"};
        for(int i=0;i<11;++i)if(name==names[i])return versions[i];return versions[11];
    }
    static void metadataChanged(const juce::String& name){++kindRevision(name);++metadataRevision();}
    inline static std::atomic<int> diagnosticParseDelayMs{0};

    static uint64_t localRevision(const juce::String& name,bool bump=false){static juce::CriticalSection mutex;static std::map<juce::String,uint64_t> revisions;const juce::ScopedLock guard(mutex);auto& value=revisions[root().getChildFile(name).getFullPathName()];if(bump)++value;return value;}
    static juce::var localSnapshot(const juce::String& name) {
        if(usingDatabase){try{pollDatabase();return database()->snapshot(name);}catch(const std::exception& e){storageError()=e.what();return juce::var(new juce::DynamicObject());}}
        struct Entry{uint64_t revision=0;juce::Time modified;juce::int64 bytes=-1;juce::var value;};static juce::CriticalSection mutex;static std::map<juce::String,Entry> cache;
        auto file=root().getChildFile(name);auto path=file.getFullPathName();
        for(;;){
            auto revision=localRevision(name);auto modified=file.getLastModificationTime();auto bytes=file.getSize();
            {const juce::ScopedLock lock(mutex);auto found=cache.find(path);if(found!=cache.end()){auto& entry=found->second;if(entry.revision==revision&&entry.modified==modified&&entry.bytes==bytes&&entry.value.isObject())return entry.value;}}
            // Parsing must never hold the cache mutex used by unrelated UI settings reads.
            if(name=="slow-cache-test.json"&&diagnosticParseDelayMs>0)juce::Thread::sleep(diagnosticParseDelayMs);
            auto value=juce::JSON::parse(file);if(!value.isObject())value=juce::var(new juce::DynamicObject());
            if(localRevision(name)!=revision||file.getLastModificationTime()!=modified||file.getSize()!=bytes)continue;
            const juce::ScopedLock lock(mutex);auto& entry=cache[path];if(entry.value.isObject()&&entry.revision>revision)return entry.value;
            entry={revision,modified,bytes,value};return entry.value;
        }
    }

    static juce::var readLocal(const juce::String& name){return localSnapshot(name).clone();}
    static void notifySharing();
    static bool editable(juce::File);
    static juce::var sharedSnapshot(const juce::String&);
    static bool sharedKind(const juce::String& name){return name=="tags.json"||name=="keys.json"||name=="loops.json"||name=="playback-types.json"||name=="folder-colours.json"||name=="musical-keys.json"||name=="parameter-edits.json";}
    static void mergeShared(const juce::String&,juce::var&);static void queueShared(const juce::String&,const juce::var&,const juce::var&);
    static juce::var read(const juce::String& name){auto value=readLocal(name);if(sharedKind(name))mergeShared(name,value);return value;}
    static juce::CriticalSection& updateLock(){static juce::CriticalSection mutex;return mutex;}
    static bool setPreparedTrack(const juce::File& file,const juce::var& record){try{bool ok=database()->setPreparedTrack(file.getFullPathName(),record);if(ok){database()->poll(true);pollDatabase();}return ok;}catch(const std::exception& error){storageError()=error.what();return false;}}
    static bool update(const juce::String& name,std::function<void(juce::DynamicObject&)> change) {
        if(usingDatabase){try{bool ok=database()->mutate(name,[&](juce::DynamicObject& object){std::map<juce::String,juce::var> before;if(sharedKind(name))for(auto& item:object.getProperties())if(juce::File::isAbsolutePath(item.name.toString()))before.emplace(item.name.toString(),item.value.clone());change(object);if(sharedKind(name)){for(auto& item:object.getProperties()){auto path=item.name.toString();if(!juce::File::isAbsolutePath(path))continue;auto found=before.find(path);juce::var old=found==before.end()?juce::var():found->second;if(!metadataValuesEqual503(item.value,old)&&!editable(juce::File(path)))throw std::runtime_error("Shared library is read-only");if(found!=before.end())before.erase(found);}for(auto& item:before)if(!item.second.isVoid()&&!editable(juce::File(item.first)))throw std::runtime_error("Shared library is read-only");}});if(ok){database()->poll(true);pollDatabase();if(sharedKind(name))notifySharing();}return ok;}catch(const std::exception& e){storageError()=e.what();return false;}}
        const juce::ScopedLock localGuard(updateLock());
        juce::InterProcessLock lock("SliceGroupsLibrary-"+juce::String(root().getFullPathName().hashCode64()));
        if (!lock.enter(1000)) return false;
        auto folder = root();auto result = folder.createDirectory();
        if (!result.wasOk()) { lock.exit();return false; }
        auto value = read(name);auto before=value.clone();change(*value.getDynamicObject());
        auto destination = folder.getChildFile(name);
        juce::TemporaryFile temporary(destination);
        bool ok = temporary.getFile().replaceWithText(juce::JSON::toString(value)) && temporary.overwriteTargetFileWithTemporary();
        if(ok){localRevision(name,true);metadataChanged(name);}lock.exit();if(ok&&sharedKind(name))queueShared(name,before,value);return ok;
    }
    static juce::File resolveFile(juce::File file){return juce::File(database()->resolvePath(file.getFullPathName()));}
    static juce::File browserRoot(){return root().getChildFile("Library");}
    static juce::Array<juce::File> libraryRoots(){juce::Array<juce::File> roots;auto data=read("library-roots.json");for(auto& item:data.getDynamicObject()->getProperties())if((bool)item.value)roots.add(juce::File(item.name.toString()));return roots;}
    static bool addLibraryRoot(juce::File folder){if(!folder.isDirectory())return false;return update("library-roots.json",[&](juce::DynamicObject& o){o.setProperty(folder.getFullPathName(),true);});}
    static void removeLibraryRoot(juce::File folder){update("library-roots.json",[&](juce::DynamicObject& o){o.removeProperty(folder.getFullPathName());});}
    static bool inLibrary(juce::File file){for(auto root:libraryRoots())if(file==root||file.isAChildOf(root))return true;return false;}
    static bool hasPlaybackType(juce::File file){file=resolveFile(file);auto data=read("playback-types.json");auto current=file;while(current!=juce::File()){if(data.getDynamicObject()->hasProperty(current.getFullPathName()))return true;auto parent=current.getParentDirectory();if(parent==current)break;current=parent;}return false;}
    static int playbackType(juce::File file){file=resolveFile(file);if(file.hasFileExtension("mid;midi"))return 7;if(file.hasFileExtension("sfz;sgmulti"))return 6;auto data=read("playback-types.json");auto* object=data.getDynamicObject();auto current=file;while(current!=juce::File()){if(object->hasProperty(current.getFullPathName()))return (int)object->getProperty(current.getFullPathName());auto parent=current.getParentDirectory();if(parent==current)break;current=parent;}return 0;}
    static int typeFromValues(juce::File file,const juce::var& data){if(file.hasFileExtension("mid;midi"))return 7;if(file.hasFileExtension("sfz;sgmulti"))return 6;auto* object=data.getDynamicObject();for(auto current=file;object&&current!=juce::File();){if(object->hasProperty(current.getFullPathName()))return juce::jlimit(0,6,(int)object->getProperty(current.getFullPathName()));auto parent=current.getParentDirectory();if(parent==current)break;current=parent;}return 0;}
    static juce::String typeName(int type){static const char* names[]{"Slice","One Shot","Track","Slice","Stretch","Warp","Multi","MIDI"};return names[juce::jlimit(0,7,type)];}
    static int modeForType(int type){return type==6?5:type==1?3:type==2?4:type==4?1:type==5?2:0;}
    static bool matchesType(int type,int filter){return filter<0||type==filter||(filter==3&&type==0)||(filter==0&&type>=3&&type<=5);}
    static void setPlaybackType(const juce::Array<juce::File>& targets,int type){
        juce::Array<juce::File> folders;
        if(type>=0)for(auto file:targets){file=resolveFile(file);if(file.isDirectory())folders.add(file);}
        update("playback-types.json",[&](juce::DynamicObject& o){
            // A folder assignment applies to its contents, including old file overrides.
            // Work from stored keys rather than walking the sample collection.
            juce::Array<juce::Identifier> removed;
            for(auto& property:o.getProperties())for(auto folder:folders)
                if(juce::File(property.name.toString()).isAChildOf(folder)){removed.add(property.name);break;}
            for(auto key:removed)o.removeProperty(key);
            for(auto file:targets){file=resolveFile(file);if(type<0)o.removeProperty(file.getFullPathName());else o.setProperty(file.getFullPathName(),type);}
        });
    }
    static juce::String tagsFor(const juce::var& tags,const juce::File& file) {
        if (auto* object = tags.getDynamicObject()) return object->getProperty(file.getFullPathName()).toString();return {};
    }
    static bool addTag(const juce::Array<juce::File>& targets,const juce::String& text) {
        juce::StringArray incoming;incoming.addTokens(text,",","");incoming.trim();incoming.removeEmptyStrings();incoming.removeDuplicates(true);
        if (incoming.isEmpty()) return false;
        return update("tags.json",[&](juce::DynamicObject& object) {
            for (auto file : targets) {file=resolveFile(file);
                juce::StringArray values;values.addTokens(object.getProperty(file.getFullPathName()).toString(),",","");
                values.trim();values.removeEmptyStrings();values.removeDuplicates(true);
                for (auto tag : incoming) if (!values.contains(tag,true)) values.add(tag);
                values.trim();values.removeEmptyStrings();values.removeDuplicates(true);object.setProperty(file.getFullPathName(),values.joinIntoString(", "));
            }
        });
    }
    struct TagRemovalImpact{int files=0,tags=0;bool needsConfirmation()const{return tags>200;}};
    static TagRemovalImpact tagRemovalImpact(const juce::var& metadata,const juce::Array<juce::File>& targets,const juce::String& text,bool all){TagRemovalImpact impact;std::set<juce::String> seen;juce::StringArray remove;remove.addTokens(text,",","");remove.trim();remove.removeEmptyStrings();for(auto file:targets){file=resolveFile(file);if(!seen.insert(file.getFullPathName()).second)continue;juce::StringArray values;values.addTokens(tagsFor(metadata,file),",","");values.trim();values.removeEmptyStrings();values.removeDuplicates(true);int count=0;for(auto tag:values)if(all||remove.contains(tag,true))++count;if(count){++impact.files;impact.tags+=count;}}return impact;}
    static bool removeTags(const juce::Array<juce::File>& targets,const juce::String& text={},bool all=false){juce::StringArray remove;remove.addTokens(text,",","");remove.trim();remove.removeEmptyStrings();if(targets.isEmpty()||(!all&&remove.isEmpty()))return false;return update("tags.json",[&](juce::DynamicObject& object){for(auto file:targets){file=resolveFile(file);auto key=file.getFullPathName();if(all){object.removeProperty(key);continue;}juce::StringArray values;values.addTokens(object.getProperty(key).toString(),",","");values.trim();values.removeEmptyStrings();for(int i=values.size();--i>=0;)if(remove.contains(values[i],true))values.remove(i);if(values.isEmpty())object.removeProperty(key);else object.setProperty(key,values.joinIntoString(", "));}});}
    static bool setFavourite(juce::File folder,juce::String name){folder=resolveFile(folder);return update("favourites.json",[&](juce::DynamicObject& o){if(name.isEmpty())o.removeProperty(folder.getFullPathName());else o.setProperty(folder.getFullPathName(),name);});}
    struct Tag { juce::String name;int count = 0; };
    static std::vector<Tag> rankedTags(const juce::var& value,juce::String prefix = {}) {
        std::map<juce::String,Tag> counts;
        if (auto* object = value.getDynamicObject()) for (auto& property : object->getProperties()) {
            juce::StringArray tags;tags.addTokens(property.value.toString(),",","");tags.trim();tags.removeEmptyStrings();tags.removeDuplicates(true);
            for (auto tag : tags) { auto& item = counts[tag.toLowerCase()];item.name = tag;++item.count; }
        }
        std::vector<Tag> result;
        for (auto& pair : counts) if (pair.second.name.startsWithIgnoreCase(prefix.trim())) result.push_back(pair.second);
        std::sort(result.begin(),result.end(),[](const Tag& a,const Tag& b) {
            return a.count != b.count ? a.count > b.count : a.name.compareIgnoreCase(b.name) < 0;
        });return result;
    }
    static juce::String fingerprint(const juce::File& input){auto file=resolveFile(input);auto stream=file.createInputStream();return stream?juce::SHA256(*stream).toHexString():juce::String();}
    static juce::var musicalKeySnapshot(){static juce::CriticalSection mutex;static juce::String path;static uint64_t seen=~uint64_t{};static juce::var cache;auto revision=kindRevision("musical-keys.json").load();const juce::ScopedLock guard(mutex);auto current=root().getFullPathName();if(path!=current||seen!=revision||!cache.isObject()){cache=read("musical-keys.json");path=current;seen=kindRevision("musical-keys.json").load();}return cache;}
    static MusicalKey musicalKeyOverride(juce::File file){auto values=musicalKeySnapshot();return MusicalKey::read(values.getProperty(file.getFullPathName(),{}));}
    static MusicalKey resolveMusicalKey(juce::File file,MusicalKey fallback={},bool midi=false){auto manual=musicalKeyOverride(file);if(manual.root!=-2)return manual;auto named=MusicalKey::filename(file.getFullPathName());if(midi&&named.known())return named;if(fallback.root!=-2)return {fallback.root,fallback.quality>=0?fallback.quality:(named.root==fallback.root?named.quality:-1)};return named;}
    inline static std::atomic<int> musicalKeyWrites{0};
    static bool setMusicalKeys(const juce::Array<juce::File>& files,MusicalKey key){for(auto file:files)if(!editable(file))return false;if(key.root==-2){std::vector<juce::String> identities;for(auto file:files){identities.push_back(file.getFullPathName());auto hash=fingerprint(file);if(hash.isNotEmpty())identities.push_back("sha256:"+hash);}if(!update("keys.json",[&](juce::DynamicObject& object){for(auto identity:identities)object.removeProperty(identity);}))return false;}return update("musical-keys.json",[&](juce::DynamicObject& object){for(auto file:files){if(key.root==-2)object.removeProperty(file.getFullPathName());else object.setProperty(file.getFullPathName(),key.value());}});}
    static void queueMusicalKeys(juce::Array<juce::File> files,MusicalKey key,std::function<void(bool)> complete={}){static juce::ThreadPool pool{1};++musicalKeyWrites;pool.addJob([files,key,complete]{bool ok=setMusicalKeys(files,key);--musicalKeyWrites;if(complete)juce::MessageManager::callAsync([complete,ok]{complete(ok);});});}
    static int fileKey(const juce::File& input,juce::String hash={}){auto file=resolveFile(input);auto musical=musicalKeyOverride(file);if(musical.root!=-2)return musical.root;auto keys=read("keys.json");auto* object=keys.getDynamicObject();auto path=file.getFullPathName();if(object->hasProperty(path))return juce::jlimit(-1,11,(int)object->getProperty(path));if(hash.isNotEmpty()&&object->hasProperty("sha256:"+hash))return juce::jlimit(-1,11,(int)object->getProperty("sha256:"+hash));return -2;}
    static bool setFileKey(const juce::File& input,int key,juce::String hash={}){auto file=resolveFile(input);if(hash.isEmpty())hash=fingerprint(file);key=juce::jlimit(-1,11,key);return update("keys.json",[&](juce::DynamicObject& object){object.setProperty(file.getFullPathName(),key);if(hash.isNotEmpty())object.setProperty("sha256:"+hash,key);});}
    static juce::File loopConfigFile(juce::String identity){return root().getChildFile("loop-configs-v2").getChildFile(juce::SHA256(identity.toRawUTF8(),identity.getNumBytesAsUTF8()).toHexString()+".xml");}
    static bool saveLoop(const juce::File& input,const juce::String& xml,juce::String hash={}){auto file=resolveFile(input);
        if(!editable(file)){storageError()="Shared library is read-only";return false;}if(hash.isEmpty())hash=fingerprint(file);auto config=juce::parseXML(xml);if(!config)return false;config->removeAttribute("normalise");config->removeAttribute("normaliseLufs");config->removeAttribute("targetLufs");if(hash.isNotEmpty())config->setAttribute("contentHash",hash);auto value=config->toString();if(usingDatabase){try{bool ok=database()->saveLoop(file.getFullPathName(),hash,value);if(ok){if(!database()->metadataEntry("parameter-edits.json",file.getFullPathName()).isVoid())update("parameter-edits.json",[&](juce::DynamicObject& o){o.removeProperty(file.getFullPathName());});database()->poll(true);pollDatabase();notifySharing();}return ok;}catch(const std::exception& e){storageError()=e.what();return false;}}if(diagnosticOldSave)return update("loops.json",[&](juce::DynamicObject& o){o.setProperty(file.getFullPathName(),value);if(hash.isNotEmpty())o.setProperty("sha256:"+hash,value);});
        const juce::ScopedLock guard(updateLock());juce::InterProcessLock fileLock("SliceGroupsLoop-"+juce::String(file.getFullPathName().hashCode64()));if(!fileLock.enter(1000))return false;
        auto persist=[&](juce::String key){auto target=loopConfigFile(key);if(!target.getParentDirectory().createDirectory())return false;juce::TemporaryFile temporary(target);return temporary.getFile().replaceWithText(value)&&temporary.overwriteTargetFileWithTemporary();};
        bool ok=persist("path:"+file.getFullPathName())&&(hash.isEmpty()||persist("sha256:"+hash));fileLock.exit();if(!ok)return false;
        ok=update("loop-index-v2.json",[&](juce::DynamicObject& o){o.setProperty(file.getFullPathName(),config->getIntAttribute("loopKey",-1));});
        if(ok){juce::var before(new juce::DynamicObject()),after(new juce::DynamicObject());after.getDynamicObject()->setProperty(file.getFullPathName(),value);queueShared("loops.json",before,after);}return ok;
    }
    static std::unique_ptr<juce::XmlElement> savedLoop(const juce::File& input,juce::String hash={}) {auto file=resolveFile(input);
        if(hash.isEmpty())hash=fingerprint(file);
        if(usingDatabase){if(!editable(file)){auto shared=sharedSnapshot("loops.json");if(auto* object=shared.getDynamicObject())if(auto config=juce::parseXML(object->getProperty(file.getFullPathName()).toString()))if(hash.isEmpty()||config->getStringAttribute("contentHash").isEmpty()||config->getStringAttribute("contentHash")==hash)return config;return {};}try{for(auto identity:{file.getFullPathName(),"sha256:"+hash})if(auto config=juce::parseXML(database()->loop(identity)))if(hash.isEmpty()||config->getStringAttribute("contentHash").isEmpty()||config->getStringAttribute("contentHash")==hash)return config;}catch(const std::exception& e){storageError()=e.what();}return {};}
        if(diagnosticOldSave){
            auto values=localSnapshot("loops.json");auto* object=values.getDynamicObject();
            for(auto identity:{file.getFullPathName(),"sha256:"+hash})if(auto config=juce::parseXML(object->getProperty(identity).toString()))if(hash.isEmpty()||config->getStringAttribute("contentHash").isEmpty()||config->getStringAttribute("contentHash")==hash)return config;
            auto shared=sharedSnapshot("loops.json");if(auto* sharedObject=shared.getDynamicObject())if(auto config=juce::parseXML(sharedObject->getProperty(file.getFullPathName()).toString()))if(hash.isEmpty()||config->getStringAttribute("contentHash").isEmpty()||config->getStringAttribute("contentHash")==hash)return config;
            return {};
        }
        auto shared=sharedSnapshot("loops.json");if(auto* o=shared.getDynamicObject())if(auto config=juce::parseXML(o->getProperty(file.getFullPathName()).toString()))if(hash.isEmpty()||config->getStringAttribute("contentHash").isEmpty()||config->getStringAttribute("contentHash")==hash)return config;
        for(auto identity:{"path:"+file.getFullPathName(),"sha256:"+hash})if(!identity.endsWithChar(':'))if(auto config=juce::XmlDocument::parse(loopConfigFile(identity)))if(hash.isEmpty()||config->getStringAttribute("contentHash").isEmpty()||config->getStringAttribute("contentHash")==hash)return config;
        auto values=localSnapshot("loops.json");auto* object=values.getDynamicObject();
        auto direct=juce::parseXML(object->getProperty(file.getFullPathName()).toString());
        if(direct&&(hash.isEmpty()||direct->getStringAttribute("contentHash").isEmpty()||direct->getStringAttribute("contentHash")==hash)){
            if(hash.isNotEmpty()&&direct->getStringAttribute("contentHash").isEmpty())saveLoop(file,direct->toString(),hash);
            return direct;
        }
        if(hash.isEmpty())return {};
        if(auto matched=juce::parseXML(object->getProperty("sha256:"+hash).toString())){saveLoop(file,matched->toString(),hash);return matched;}
        // Upgrade old path-only saves once, while their original files exist.
        std::map<juce::String,juce::String> upgrades;std::unique_ptr<juce::XmlElement> match;
        for(const auto& item:object->getProperties()){
            auto key=item.name.toString();if(!key.startsWith("/"))continue;
            auto config=juce::parseXML(item.value.toString());if(!config||config->hasAttribute("contentHash"))continue;
            auto original=juce::File(key);if(!original.existsAsFile())continue;
            auto id=fingerprint(original);if(id.isEmpty())continue;
            config->setAttribute("contentHash",id);auto xml=config->toString();upgrades[key]=xml;upgrades["sha256:"+id]=xml;if(id==hash)match=juce::parseXML(xml);
        }
        if(match)saveLoop(file,match->toString(),hash);
        return match;
    }
    static juce::File presetFolder(){return root().getChildFile("Presets");}
    static juce::String presetName(const juce::File& file){auto xml=juce::XmlDocument::parse(file);return xml?xml->getStringAttribute("presetName",file.getFileNameWithoutExtension()):file.getFileNameWithoutExtension();}
    static juce::File findPreset(juce::String name){for(auto entry:juce::RangedDirectoryIterator(presetFolder(),false,"*.sgpreset",juce::File::findFiles))if(presetName(entry.getFile()).equalsIgnoreCase(name.trim()))return entry.getFile();return {};}
    static bool savePreset(juce::String name,juce::XmlElement& xml,bool overwrite=false){
        name=name.trim().substring(0,120);if(name.isEmpty())return false;
        juce::InterProcessLock guard("SliceGroupsPresets-"+juce::String(root().getFullPathName().hashCode64()));if(!guard.enter(1000))return false;
        auto folder=presetFolder();bool ok=folder.createDirectory().wasOk();auto file=findPreset(name);
        if(file.existsAsFile()&&!overwrite)ok=false;
        if(ok){if(file==juce::File())file=folder.getChildFile(juce::Uuid().toString()+".sgpreset");xml.setAttribute("presetName",name);juce::TemporaryFile temporary(file);ok=temporary.getFile().replaceWithText(xml.toString())&&temporary.overwriteTargetFileWithTemporary();}
        guard.exit();return ok;
    }
    struct Swing { bool enabled = false;double amount = 50; };
    static bool writeSwing(juce::String group,Swing swing) {
        if (group.trim().isEmpty()) return false;
        return update("swing-groups.json",[&](juce::DynamicObject& object) {
            auto value = juce::var(new juce::DynamicObject());value.getDynamicObject()->setProperty("enabled",swing.enabled);
            value.getDynamicObject()->setProperty("amount",swing.amount);object.setProperty(group.trim(),value);
        });
    }
    static bool readSwing(juce::String group,Swing& swing) {
        auto groups = read("swing-groups.json");auto value = groups.getDynamicObject()->getProperty(group.trim());
        if (auto* object = value.getDynamicObject()) {
            swing.enabled = (bool)object->getProperty("enabled");swing.amount = juce::jlimit(50.0,75.0,(double)object->getProperty("amount"));return true;
        }return false;
    }
};

// Per-computer records avoid Dropbox whole-database overwrites. Each sample/field is
// reconciled by timestamp; explicit deletion records stop stale local tags returning.
class SharedLibraryMetadata final:private juce::Thread {
 struct Record{juce::String kind,path,writer;juce::var value;juce::int64 modified=0;bool deleted=false;};
 struct Pending{juce::File root;Record record;};
 juce::CriticalSection mutex;juce::WaitableEvent wake;std::vector<Pending> pending;
 using Layers=std::map<juce::String,juce::var>;std::shared_ptr<const Layers> overlay=std::make_shared<const Layers>();uint64_t pendingVersion=0;juce::String writer;
 struct Cached{juce::int64 time=0,size=-1;juce::var value;};std::map<juce::String,Cached> cache;
 std::set<juce::String> migrated;
 static bool relativeOK(juce::String path){if(path==".")return true;if(path.isEmpty()||juce::File::isAbsolutePath(path)||path.containsChar('\\'))return false;for(auto part:juce::StringArray::fromTokens(path,"/",""))if(part==".."||part=="."||part.isEmpty())return false;return !path.startsWith(".slicegroups/");}
 static juce::Array<juce::File> roots(){juce::Array<juce::File> result;auto data=LibraryStore::readLocal("library-roots.json");for(auto& pair:data.getDynamicObject()->getProperties())if((bool)pair.value){juce::File root(pair.name.toString());if(root.isDirectory())result.add(root);}return result;}
 static juce::File owner(const juce::File& file,const juce::Array<juce::File>& roots){juce::File result;for(auto root:roots)if((file==root||file.isAChildOf(root))&&root.getFullPathName().length()>result.getFullPathName().length())result=root;return result;}
 static juce::String identity(const Record& r){return r.kind+"|"+r.path;}
 static juce::var json(const Record& r){auto* o=new juce::DynamicObject();o->setProperty("version",1);o->setProperty("kind",r.kind);o->setProperty("path",r.path);o->setProperty("writer",r.writer);o->setProperty("modified",r.modified);o->setProperty("deleted",r.deleted);if(!r.deleted)o->setProperty("value",r.value);return juce::var(o);}
 static bool decode(const juce::var& v,Record& r){if(!v.isObject()||(int)v.getProperty("version",0)!=1)return false;r.kind=v.getProperty("kind",{}).toString();r.path=v.getProperty("path",{}).toString();r.writer=v.getProperty("writer",{}).toString();r.modified=(juce::int64)v.getProperty("modified",0);r.deleted=(bool)v.getProperty("deleted",false);r.value=v.getProperty("value",{});return LibraryStore::sharedKind(r.kind)&&relativeOK(r.path)&&r.writer.isNotEmpty();}
 bool write(const Pending& p){juce::InterProcessLock guard("SliceGroupsShared-"+juce::String((p.root.getFullPathName()+identity(p.record)).hashCode64()));if(!guard.enter(1000))return false;auto dir=p.root.getChildFile(".slicegroups/metadata-v1").getChildFile(writer);if(!dir.createDirectory())return false;auto key=identity(p.record);auto file=dir.getChildFile(juce::SHA256(key.toRawUTF8(),key.getNumBytesAsUTF8()).toHexString()+".json");Record existing;if(decode(juce::JSON::parse(file),existing)&&existing.modified>p.record.modified)return true;juce::TemporaryFile tmp(file);return tmp.getFile().replaceWithText(juce::JSON::toString(json(p.record)))&&tmp.overwriteTargetFileWithTemporary();}
 static juce::var portableValue(const juce::String& kind,juce::var value,juce::String path,bool restore){if(kind=="loops.json")if(auto xml=juce::parseXML(value.toString())){if(restore)xml->setAttribute("file",path);else xml->removeAttribute("file");value=xml->toString();}return value;}
 void scan(){auto library=roots();std::map<juce::String,juce::var> next;std::set<juce::String> activeFiles;
 for(auto root:library){if(threadShouldExit())return;std::map<juce::String,Record> latest;
 for(auto entry:juce::RangedDirectoryIterator(root.getChildFile(".slicegroups/metadata-v1"),true,"*.json",juce::File::findFiles)){if(threadShouldExit())return;auto file=entry.getFile();auto path=file.getFullPathName();activeFiles.insert(path);auto& c=cache[path];auto time=file.getLastModificationTime().toMilliseconds(),size=file.getSize();if(c.time!=time||c.size!=size){c.time=time;c.size=size;c.value=juce::JSON::parse(file);}Record r;if(!decode(c.value,r))continue;auto id=identity(r);auto found=latest.find(id);if(found==latest.end()||r.modified>found->second.modified||(r.modified==found->second.modified&&r.writer>found->second.writer))latest[id]=r;}
 // Upgrade local metadata once for each attached root; existing shared records win.
 if(!migrated.count(root.getFullPathName())){bool ok=true;for(auto kind:{"tags.json","keys.json","loops.json","playback-types.json"}){auto local=LibraryStore::readLocal(kind);if(juce::String(kind)=="loops.json"){auto index=LibraryStore::readLocal("loop-index-v2.json");for(auto& item:index.getDynamicObject()->getProperties()){auto path=item.name.toString();auto xml=LibraryStore::loopConfigFile("path:"+path).loadFileAsString();if(xml.isNotEmpty())local.getDynamicObject()->setProperty(path,xml);}}for(auto& pair:local.getDynamicObject()->getProperties()){auto path=pair.name.toString();if(!juce::File::isAbsolutePath(path))continue;juce::File file(path);if(owner(file,library)!=root)continue;Record r{kind,file==root?".":file.getRelativePathFrom(root).replaceCharacter('\\','/'),writer,portableValue(kind,pair.value,{},false),0,false};if(!relativeOK(r.path)||latest.count(identity(r)))continue;if(write({root,r}))latest[identity(r)]=r;else ok=false;}}if(ok)migrated.insert(root.getFullPathName());}
 for(auto& pair:latest){auto& r=pair.second;auto file=r.path=="."?root:root.getChildFile(r.path);if(owner(file,library)!=root)continue;auto& object=next[r.kind];if(!object.isObject())object=juce::var(new juce::DynamicObject());object.getDynamicObject()->setProperty(file.getFullPathName(),r.deleted?juce::var():portableValue(r.kind,r.value,file.getFullPathName(),true));}}
 for(auto it=cache.begin();it!=cache.end();)if(!activeFiles.count(it->first))it=cache.erase(it);else ++it;
 std::vector<Pending> changes;uint64_t version;{const juce::ScopedLock lock(mutex);changes=pending;version=pendingVersion;}for(auto& p:changes){auto& object=next[p.record.kind];if(!object.isObject())object=juce::var(new juce::DynamicObject());auto path=p.record.path=="."?p.root.getFullPathName():p.root.getChildFile(p.record.path).getFullPathName();object.getDynamicObject()->setProperty(path,p.record.deleted?juce::var():portableValue(p.record.kind,p.record.value,path,true));}
 // Build and compare off the reader path. Publication never exposes mutable objects.
 auto previous=std::atomic_load(&overlay);std::set<juce::String> changedKinds;
 for(auto& pair:next){auto old=previous->find(pair.first);if(old==previous->end()||old->second.getDynamicObject()->getProperties()!=pair.second.getDynamicObject()->getProperties())changedKinds.insert(pair.first);}
 for(auto& pair:*previous)if(next.find(pair.first)==next.end())changedKinds.insert(pair.first);
 if(!changedKinds.empty()){auto published=std::make_shared<const Layers>(std::move(next));{const juce::ScopedLock lock(mutex);if(version!=pendingVersion)return;std::atomic_store(&overlay,published);}for(auto& kind:changedKinds)LibraryStore::metadataChanged(kind);}}


 void run()override{while(!threadShouldExit()){auto outbox=LibraryStore::readLocal("shared-outbox.json");std::map<juce::String,juce::String> completed;auto library=roots();for(auto& pair:outbox.getDynamicObject()->getProperties()){Record r;auto v=pair.value;if(!decode(v,r))continue;auto rootPath=v.getProperty("root",{}).toString();if(!juce::File::isAbsolutePath(rootPath))continue;juce::File folder(rootPath);if(!library.contains(folder))continue;if(write({folder,r})){completed[pair.name.toString()]=juce::JSON::toString(v);const juce::ScopedLock lock(mutex);pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const Pending& p){return p.root==folder&&identity(p.record)==identity(r)&&p.record.modified<=r.modified;}),pending.end());}}if(!completed.empty())LibraryStore::update("shared-outbox.json",[&](juce::DynamicObject& o){for(auto& pair:completed)if(juce::JSON::toString(o.getProperty(pair.first))==pair.second)o.removeProperty(pair.first);});scan();wake.wait(1000);}}
public:
 SharedLibraryMetadata():juce::Thread("Slice Groups shared metadata"){auto file=LibraryStore::root().getChildFile("shared-device-id.txt");writer=file.loadFileAsString().trim();if(writer.isEmpty()){writer=juce::Uuid().toString();file.getParentDirectory().createDirectory();file.replaceWithText(writer);}startThread();}
 ~SharedLibraryMetadata()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 static SharedLibraryMetadata& instance(){LibraryStore::updateLock();static SharedLibraryMetadata service;return service;}
 juce::var snapshot(const juce::String& kind){auto view=std::atomic_load(&overlay);auto found=view->find(kind);return found==view->end()?juce::var():found->second;}
 void merge(const juce::String& kind,juce::var& value){auto snapshot=std::atomic_load(&overlay);auto found=snapshot->find(kind);if(found==snapshot->end())return;for(auto& pair:found->second.getDynamicObject()->getProperties())if(pair.value.isVoid())value.getDynamicObject()->removeProperty(pair.name);else value.getDynamicObject()->setProperty(pair.name,pair.value);}
 // Regression: readers must not wait for writer housekeeping on the background thread.
 bool readContentionForCheck(){std::atomic<bool> held{false};std::thread writerThread([&]{const juce::ScopedLock lock(mutex);held=true;juce::Thread::sleep(160);});while(!held)std::this_thread::yield();auto begin=juce::Time::getMillisecondCounterHiRes();auto value=juce::var(new juce::DynamicObject());merge("tags.json",value);double elapsed=juce::Time::getMillisecondCounterHiRes()-begin;writerThread.join();std::cout<<"Shared-metadata read during writer contention: "<<elapsed<<" ms\n";return elapsed<40;}

 void enqueue(const juce::String& kind,const juce::var& before,const juce::var& after){auto library=roots();std::set<juce::String> paths;for(auto* o:{before.getDynamicObject(),after.getDynamicObject()})for(auto& pair:o->getProperties())if(juce::File::isAbsolutePath(pair.name.toString()))paths.insert(pair.name.toString());std::vector<std::pair<juce::String,juce::var>> durable;
 {const juce::ScopedLock lock(mutex);auto snapshot=std::atomic_load(&overlay);auto updated=std::make_shared<Layers>(*snapshot);auto layerIt=updated->find(kind);if(layerIt!=updated->end())layerIt->second=layerIt->second.clone();for(auto path:paths){auto old=before.getDynamicObject()->getProperty(path),value=after.getDynamicObject()->getProperty(path);if(old==value)continue;juce::File file(path);auto root=owner(file,library);if(root==juce::File())continue;Record r{kind,file==root?".":file.getRelativePathFrom(root).replaceCharacter('\\','/'),writer,portableValue(kind,value,{},false),juce::Time::currentTimeMillis(),!after.getDynamicObject()->hasProperty(path)};static juce::int64 lastStamp=0;r.modified=juce::jmax(r.modified,lastStamp+1);lastStamp=r.modified;auto record=json(r);record.getDynamicObject()->setProperty("root",root.getFullPathName());durable.push_back({root.getFullPathName()+"|"+identity(r),record});pending.push_back({root,r});auto& layer=(*updated)[kind];if(!layer.isObject())layer=juce::var(new juce::DynamicObject());layer.getDynamicObject()->setProperty(path,r.deleted?juce::var():value);}++pendingVersion;std::atomic_store(&overlay,std::shared_ptr<const Layers>(updated));}
 if(!durable.empty())LibraryStore::metadataChanged(kind);
 if(!durable.empty())LibraryStore::update("shared-outbox.json",[&](juce::DynamicObject& o){for(auto& pair:durable){Record old,next;decode(pair.second,next);if(!decode(o.getProperty(pair.first),old)||old.modified<=next.modified)o.setProperty(pair.first,pair.second);}});wake.signal();}

};
#include "OwnedSharing.h"
inline void LibraryStore::notifySharing(){OwnedLibrarySharing::instance().refresh();}
inline bool LibraryStore::editable(juce::File file){return OwnedLibrarySharing::instance().editable(resolveFile(file));}
inline void LibraryStore::mergeShared(const juce::String& name,juce::var& value){if(usingDatabase){auto merged=OwnedLibrarySharing::instance().snapshot(name);if(merged.isObject())value=merged;}else if(diagnosticSharing)SharedLibraryMetadata::instance().merge(name,value);}
inline void LibraryStore::queueShared(const juce::String& name,const juce::var& before,const juce::var& after){if(!usingDatabase&&diagnosticSharing)SharedLibraryMetadata::instance().enqueue(name,before,after);}

inline juce::var LibraryStore::sharedSnapshot(const juce::String& name){return usingDatabase?OwnedLibrarySharing::instance().snapshot(name,true):diagnosticSharing?SharedLibraryMetadata::instance().snapshot(name):juce::var();}
