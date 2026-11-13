// Included in RackProcessor. Remote work uses the same background loader as browsing.
int phoneHandle=0;
juce::String phoneIdentity=juce::Uuid().toString();
std::array<std::unique_ptr<LoopLoader>,8> phoneLoaders;
juce::ThreadPool phoneWork{1,0,juce::Thread::Priority::background};
std::map<juce::String,juce::File> phoneRecent;
std::shared_ptr<std::atomic<bool>> phoneAlive=std::make_shared<std::atomic<bool>>(true);
static juce::String phoneKey(const juce::String& path){return juce::SHA256(path.toRawUTF8(),path.getNumBytesAsUTF8()).toHexString();}
juce::Array<juce::var> phoneRows(int instance){
 juce::Array<juce::var> rows;auto favourites=LibraryStore::read("favourites.json");
 for(int i=0;i<8;++i){auto& p=*loops[i];juce::String path;bool midi=false,navigate=false;{const juce::ScopedTryLock guard(p.lock);if(!guard.isLocked())continue;midi=p.sequenceEnabled&&p.sequence.path.isNotEmpty();path=midi?p.sequence.path:p.audio?p.audio->path:juce::String();navigate=midi?(bool)p.midiAdvanceOrder:(bool)p.advanceBrowserOrder;}
 if(path.isEmpty())continue;auto key=phoneKey(path);phoneRecent[key]=juce::File(path);auto* row=new juce::DynamicObject();row->setProperty("id",phoneIdentity+":"+juce::String(i));row->setProperty("instance",instance);row->setProperty("group",i);row->setProperty("label","Instance "+juce::String(instance)+" · Group "+juce::String(i+1));row->setProperty("file",juce::File(path).getFileName());row->setProperty("key",key);row->setProperty("favourite",favourites.hasProperty(path));row->setProperty("navigate",navigate);row->setProperty("busy",phoneLoaders[i]&&phoneLoaders[i]->busy.load());rows.add(juce::var(row));}
 while(phoneRecent.size()>256)phoneRecent.erase(phoneRecent.begin());return rows;
}
void phoneAction(int group,juce::String command,juce::String key){
 if(!juce::isPositiveAndBelow(group,8))return;auto found=phoneRecent.find(key);if(found==phoneRecent.end())return;auto file=found->second;
 if(command=="fav"){phoneWork.addJob([file]{LibraryStore::setFavourite(file,file.getFileNameWithoutExtension());});return;}
 auto& p=*loops[group];juce::Array<juce::File> order;bool midi;{const juce::ScopedLock guard(p.lock);midi=p.sequenceEnabled&&p.sequence.path.isNotEmpty();auto current=midi?p.sequence.path:p.audio?p.audio->path:juce::String();if(current!=file.getFullPathName())return;auto snapshot=midi?p.midiAdvanceOrder:p.advanceBrowserOrder;if(!snapshot)return;order=*snapshot;}
 int row=order.indexOf(file),direction=command=="prev"?-1:1;if(row<0)return;
 for(int next=row+direction;juce::isPositiveAndBelow(next,order.size());next+=direction){auto target=order[next];if(midi){if(!SequenceClip::midi(target))continue;uint64_t identity;{const juce::ScopedLock guard(p.lock);identity=p.sequenceIdentity510;p.queueSequenceAutoSave(true);}auto* engine=&p;phoneWork.addJob([engine,target,identity]{SequenceClip clip;if(!clip.load(target))return;const juce::ScopedLock guard(engine->lock);if(engine->sequenceIdentity510!=identity)return;clip.revision=engine->sequence.revision+1;++engine->sequenceIdentity510;engine->sequence=std::move(clip);engine->resetMidiAdvance();engine->sequenceEnabled=true;engine->recordHistory();++engine->midiAdvanced;});return;}
 if(!target.hasFileExtension("wav;aif;aiff;flac;mp3;ogg;sfz;sgmulti"))continue;if(!phoneLoaders[group]){phoneLoaders[group]=std::make_unique<LoopLoader>(p);auto alive=phoneAlive;auto* engine=&p;phoneLoaders[group]->completed=[alive,engine](bool ok,juce::File,uint64_t){if(alive->load()&&ok)++engine->autoAdvanced;};phoneLoaders[group]->startThread();}phoneLoaders[group]->request(target);return;}
}
