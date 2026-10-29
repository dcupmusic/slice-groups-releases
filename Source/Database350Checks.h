#pragma once
int runDatabaseMigration(){
 try{auto db=LibraryStore::database();if(db->integrity()!="ok")return 1;auto loops=LibraryStore::savedKeySummary();std::cout<<"SQLite migration/verification OK; "<<loops.getDynamicObject()->getProperties().size()<<" saved paths; database "<<LibraryStore::root().getChildFile("library.db").getFullPathName()<<"\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
int runDatabase350Checks(){
 juce::ScopedJuceInitialiser_GUI gui;auto root=LibraryStore::root();root.createDirectory();auto sample=root.getChildFile("sample.wav");sample.replaceWithText("fixture audio");// Hash fixture bytes without resolving paths: resolution opens SQLite, which must happen only after legacy fixtures are written.
 auto fixtureStream=sample.createInputStream();auto hash=juce::SHA256(*fixtureStream).toHexString();
 auto old=juce::var(new juce::DynamicObject());juce::XmlElement config("SliceGroups");config.setAttribute("loopKey",2);config.setAttribute("contentHash",hash);config.setAttribute("trimStart",123);config.setAttribute("trimEnd",456);auto original=config.toString();old.getDynamicObject()->setProperty(sample.getFullPathName(),original);old.getDynamicObject()->setProperty("sha256:"+hash,original);root.getChildFile("loops.json").replaceWithText(juce::JSON::toString(old));
 auto index=juce::var(new juce::DynamicObject());index.getDynamicObject()->setProperty(sample.getFullPathName(),8);auto another=root.getChildFile("v2-only.wav");index.getDynamicObject()->setProperty(another.getFullPathName(),5);root.getChildFile("loop-index-v2.json").replaceWithText(juce::JSON::toString(index));
 for(auto file:{sample,another}){auto path=LibraryStore::loopConfigFile("path:"+file.getFullPathName());path.getParentDirectory().createDirectory();config.setAttribute("loopKey",file==sample?8:5);config.setAttribute("contentHash",file==sample?hash:"v2-only-hash");path.replaceWithText(config.toString());}
 auto tags=juce::var(new juce::DynamicObject());tags.getDynamicObject()->setProperty(sample.getFullPathName(),"bass, house");root.getChildFile("tags.json").replaceWithText(juce::JSON::toString(tags));
 auto settings=juce::var(new juce::DynamicObject());settings.getDynamicObject()->setProperty("browserAutoLoad",false);settings.getDynamicObject()->setProperty("uiScale",.9);settings.getDynamicObject()->setProperty("trackExportFolder",root.getFullPathName());root.getChildFile("settings.json").replaceWithText(juce::JSON::toString(settings));
 auto fav=juce::var(new juce::DynamicObject());fav.getDynamicObject()->setProperty(sample.getFullPathName(),"Favourite");root.getChildFile("favourites.json").replaceWithText(juce::JSON::toString(fav));
 auto roots=juce::var(new juce::DynamicObject());roots.getDynamicObject()->setProperty(root.getFullPathName(),true);root.getChildFile("library-roots.json").replaceWithText(juce::JSON::toString(roots));auto cache=BrowserSearchIndex::indexFile(root);cache.getParentDirectory().createDirectory();juce::Array<juce::var> entries;entries.add(sample.getFullPathName());cache.replaceWithText(juce::JSON::toString(juce::var(entries)));
 auto oldHash=LibraryStore::fingerprint(root.getChildFile("loops.json")),oldTags=LibraryStore::fingerprint(root.getChildFile("tags.json"));auto db=LibraryStore::database();if(db->integrity()!="ok")return 3501;
 auto recalled=LibraryStore::savedLoop(sample,hash);if(!recalled||recalled->getIntAttribute("loopKey")!=2||recalled->getIntAttribute("trimStart")!=123)return 3502;
 recalled=LibraryStore::savedLoop(another,"v2-only-hash");if(!recalled||recalled->getIntAttribute("loopKey")!=5)return 3503;
 if(LibraryStore::tagsFor(LibraryStore::read("tags.json"),sample)!="bass, house"||LibraryStore::read("favourites.json").getProperty(sample.getFullPathName(),{}).toString()!="Favourite"||(double)LibraryStore::read("settings.json").getProperty("uiScale",0)!=.9)return 3504;
 if(!BrowserSearchIndex::indexExists(root)||!BrowserSearchIndex::cached(root).contains(sample))return 3505;
 if(!LibraryStore::setFileKey(sample,3,hash))return 3506;LibraryStore::setPlaybackType({root},4);if(LibraryStore::playbackType(sample)!=4||LibraryStore::fileKey(sample,hash)!=3)return 3507;
 config.setAttribute("contentHash",hash);config.setAttribute("loopKey",7);if(!LibraryStore::saveLoop(sample,config.toString(),hash))return 3508;recalled=LibraryStore::savedLoop(root.getChildFile("renamed.wav"),hash);if(!recalled||recalled->getIntAttribute("loopKey")!=7)return 3509;
 if(!LibraryStore::removeTags({sample},"bass")||!LibraryStore::addTag({sample},"new"))return 3510;if(LibraryStore::tagsFor(LibraryStore::read("tags.json"),sample)!="house, new")return 3511;
 if(LibraryStore::fingerprint(root.getChildFile("loops.json"))!=oldHash||LibraryStore::fingerprint(root.getChildFile("tags.json"))!=oldTags)return 3512;
 auto summary=LibraryStore::savedKeySummary();if((int)summary.getProperty(sample.getFullPathName(),-1)!=7)return 3513;
 // A different connection simulates another Ableton/plugin process changing metadata.
 auto revision=LibraryStore::kindRevision("settings.json").load();sqlite3* external=nullptr;sqlite3_open(root.getChildFile("library.db").getFullPathName().toRawUTF8(),&external);auto code=sqlite3_exec(external,"BEGIN IMMEDIATE; INSERT INTO metadata(kind,identity,value) VALUES('settings.json','external', '42'); UPDATE revisions SET revision=revision+1 WHERE kind='settings.json'; COMMIT;",nullptr,nullptr,nullptr);sqlite3_close(external);if(code!=SQLITE_OK)return 3514;
 juce::Thread::sleep(270);if((int)LibraryStore::read("settings.json").getProperty("external",0)!=42||LibraryStore::kindRevision("settings.json")<=revision)return 3515;
 // Uncommitted writes must disappear, including after another connection opens.
 sqlite3_open(root.getChildFile("library.db").getFullPathName().toRawUTF8(),&external);sqlite3_exec(external,"BEGIN IMMEDIATE; DELETE FROM loops;",nullptr,nullptr,nullptr);sqlite3_close(external);if(db->loop(sample.getFullPathName()).isEmpty()||db->integrity()!="ok")return 3516;
 LibraryStore::read("settings.json");std::atomic<bool> held{false};std::thread writer([&]{sqlite3* c=nullptr;sqlite3_open(root.getChildFile("library.db").getFullPathName().toRawUTF8(),&c);sqlite3_exec(c,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr);held=true;juce::Thread::sleep(200);sqlite3_exec(c,"ROLLBACK",nullptr,nullptr,nullptr);sqlite3_close(c);});while(!held)std::this_thread::yield();auto now=juce::Time::getMillisecondCounterHiRes();auto setting=LibraryStore::read("settings.json");double elapsed=juce::Time::getMillisecondCounterHiRes()-now;writer.join();if(elapsed>40)return 3517;

 auto trashFolder=root.getChildFile("trash-folder");trashFolder.createDirectory();auto child=trashFolder.getChildFile("child.wav");child.replaceWithText("restore fixture");BrowserSearchIndex::store(root,{sample,trashFolder,child});
 FileTrashWorker trash;int callbacks=0;bool failure=false;trash.setCompleted([&](juce::Array<juce::File>,juce::Array<juce::File> failed,bool){++callbacks;failure=!failed.isEmpty();});
 auto wait=[&](auto predicate){auto deadline=juce::Time::getMillisecondCounterHiRes()+5000;while(!predicate()&&juce::Time::getMillisecondCounterHiRes()<deadline)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return predicate();};
 trash.request({trashFolder});if(!wait([&]{return callbacks==1;})||failure||child.existsAsFile()||BrowserSearchIndex::cached(root).contains(child))return 3519;
 if(!trash.undoRedo(false)||!wait([&]{return callbacks==2;})||failure||child.loadFileAsString()!="restore fixture"||!BrowserSearchIndex::indexExists(root)||!BrowserSearchIndex::cached(root).contains(child))return 3520;
 std::cout<<"Folder Trash undo and SQLite browser index prune/invalidation passed\n";

 auto big=root.getChildFile("large-catalog");juce::Array<juce::File> many;for(int n=0;n<20000;++n)many.add(big.getChildFile("sample_"+juce::String(n)+".wav"));db->setCatalog(big,{sample});std::atomic<bool> indexing{true};juce::String indexError;
 std::thread indexWriter([&]{try{db->setCatalog(big,many);}catch(const std::exception& e){indexError=e.what();}indexing=false;});double maxWrite=0;bool partial=false;
 while(indexing){auto start=juce::Time::getMillisecondCounterHiRes();LibraryStore::update("settings.json",[](juce::DynamicObject& o){o.setProperty("catalogWriteTest",juce::Time::getMillisecondCounterHiRes());});maxWrite=juce::jmax(maxWrite,juce::Time::getMillisecondCounterHiRes()-start);auto count=db->catalog(big).size();partial|=count!=1&&count!=20000;juce::Thread::sleep(5);}indexWriter.join();
 if(indexError.isNotEmpty()||partial||db->catalog(big).size()!=20000||maxWrite>100)return 3521;
 std::cout<<"20,000-entry atomic catalogue publication: no partial results; concurrent settings-write maximum "<<maxWrite<<" ms\n";
 if(LibraryPreparation::automaticEnabled||LibraryStore::diagnosticSharing)return 3518;
 std::cout<<"SQLite migration: JSON priority/v2 fallback, backup preservation, settings/tags/favourites/types/keys/timing/hash recall, saved-key summary, browser index, external-process invalidation, rollback and integrity passed\nWAL reader during 200 ms write transaction: "<<elapsed<<" ms; preparation/sharing workers remain disabled\n";
 return 0;
}

int runDatabase350Benchmark(){
 auto root=LibraryStore::root();if(!root.getFullPathName().contains("/work/"))return 2;
 auto db=LibraryStore::database();auto summary=db->savedKeys();auto* keys=summary.getDynamicObject();if(keys->getProperties().size()<1)return 3;
 auto xml=db->loop(keys->getProperties().getName(0).toString());auto config=juce::parseXML(xml);if(!config)return 4;
 auto file=root.getChildFile("sqlite-benchmark-fixture.wav");file.replaceWithText("benchmark fixture");auto hash=LibraryStore::fingerprint(file);std::vector<double> saves,loads;
 for(int n=0;n<20;++n){config->setAttribute("pitch",n*.1);auto start=juce::Time::getMillisecondCounterHiRes();if(!LibraryStore::saveLoop(file,config->toString(),hash))return 5;saves.push_back(juce::Time::getMillisecondCounterHiRes()-start);start=juce::Time::getMillisecondCounterHiRes();auto recalled=LibraryStore::savedLoop(file,hash);loads.push_back(juce::Time::getMillisecondCounterHiRes()-start);if(!recalled||std::abs(recalled->getDoubleAttribute("pitch")-n*.1)>.00001)return 6;}
 std::sort(saves.begin(),saves.end());std::sort(loads.begin(),loads.end());std::cout<<"SQLite "<<keys->getProperties().size()<<" saved paths: full save median "<<saves[10]<<" ms, max "<<saves.back()<<" ms; indexed recall median "<<loads[10]<<" ms, max "<<loads.back()<<" ms\n";return 0;
}
