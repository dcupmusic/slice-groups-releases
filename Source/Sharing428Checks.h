#pragma once
int runSharing428(){
 juce::ScopedJuceInitialiser_GUI gui;
 auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 auto wait=[](auto ready){auto until=juce::Time::getMillisecondCounterHiRes()+15000;while(!ready()&&juce::Time::getMillisecondCounterHiRes()<until)juce::Thread::sleep(1);return ready();};
 try{
  auto root=LibraryStore::root();root.createDirectory();auto db=LibraryStore::database();
  // Real JSON encoding into a temporary file, interrupted after bytes have
  // actually been written. The previously published document must survive.
  auto transport=root.getChildFile("atomic/snapshot.json");transport.getParentDirectory().createDirectory();transport.replaceWithText("{\"sentinel\":123}");
  juce::var huge(new juce::DynamicObject());auto text=juce::String::repeatedString("payload data ",85000);for(int i=0;i<160;++i)huge.getDynamicObject()->setProperty("item"+juce::String(i),text);
  {OwnedLibrarySharing sharing(false);std::atomic<bool> cancelled{false},completed{false};std::exception_ptr error;
   std::thread writer([&]{try{completed=sharing.atomicJSON(transport,huge);}catch(const OwnedLibrarySharing::Cancelled&){cancelled=true;}catch(...){error=std::current_exception();}});
   bool writing=wait([&]{return sharing.phase==3&&sharing.writtenBytes>65536;});auto start=juce::Time::getMillisecondCounterHiRes();sharing.signalThreadShouldExit();writer.join();double elapsed=juce::Time::getMillisecondCounterHiRes()-start;if(error)std::rethrow_exception(error);
   require(writing&&cancelled&&!completed&&elapsed<500,"streamed publication cancellation");require(juce::JSON::parse(transport).getProperty("sentinel",0)==juce::var(123),"cancel preserves complete published JSON");
   std::cout<<"Cancel ~160 MB JSON publication after writing begins: "<<elapsed<<" ms; previous snapshot intact\n";
  }
  {OwnedLibrarySharing sharing(false);juce::var replacement(new juce::DynamicObject());replacement.getDynamicObject()->setProperty("resumed",true);require(sharing.atomicJSON(transport,replacement)&&juce::JSON::parse(transport).getProperty("resumed",false),"subsequent complete atomic publication");}
  auto samples=root.getChildFile("Owner library");samples.createDirectory();
  db->mutate("library-roots.json",[&](juce::DynamicObject& object){object.setProperty(samples.getFullPathName(),true);});
  {OwnedLibrarySharing sharing(false);require(sharing.claim(samples),"claim test root");sharing.scan();require(sharing.mergePasses==0,"owner-only does not build global overlay");}
  auto owner=juce::JSON::parse(samples.getChildFile(".slicegroups/sharing-v2/owner.json")).getProperty("owner",{}).toString();auto document=samples.getChildFile(".slicegroups/sharing-v2/"+owner+".json");auto previous=document.loadFileAsString();
  constexpr int rows=1200;auto padding=juce::String::repeatedString("x",8192);
  db->mutate("loops.json",[&](juce::DynamicObject& object){for(int i=0;i<rows;++i){auto path=samples.getChildFile("sample"+juce::String(i)+".wav").getFullPathName();juce::XmlElement xml("SliceGroups");xml.setAttribute("file",path);xml.setAttribute("bpm",123.45);xml.setAttribute("loopKey",2);xml.setAttribute("normalise",true);xml.createNewChildElement("padding")->addTextElement(padding);object.setProperty(path,xml.toString());}});
  // Cold database reads also support cooperative cancellation, without caching
  // a partial result or changing any durable metadata.
  LocalDatabase cold(root);bool interrupted=false;int checkpoints=0;try{cold.snapshot("loops.json",[&]{if(++checkpoints==12)throw std::runtime_error("cancel read");});}catch(const std::runtime_error&){interrupted=true;}
  require(interrupted&&cold.snapshot("loops.json").getDynamicObject()->getProperties().size()==rows,"cancelled read leaves SQLite complete");
  {OwnedLibrarySharing sharing(false);sharing.startThread(juce::Thread::Priority::background);bool busy=wait([&]{return sharing.phase==2;});auto start=juce::Time::getMillisecondCounterHiRes();sharing.signalThreadShouldExit();sharing.wake.signal();sharing.stopThread(-1);double elapsed=juce::Time::getMillisecondCounterHiRes()-start;
   require(busy&&elapsed<500&&document.loadFileAsString()==previous,"busy sharing worker stops before publishing partial snapshot");std::cout<<"Cancel real sharing scan with "<<rows<<" saved loops: "<<elapsed<<" ms\n";
  }
  {OwnedLibrarySharing sharing(false);sharing.scan();auto fields=juce::JSON::parse(document).getProperty("fields",{});auto loops=fields.getProperty("loops.json",{});require(loops.isObject()&&loops.getDynamicObject()->getProperties().size()==rows,"restart publishes every loop after cancellation");auto xml=juce::parseXML(loops.getProperty("sample0.wav",{}).toString());require(xml&&xml->getDoubleAttribute("bpm")==123.45&&!xml->hasAttribute("normalise")&&!xml->hasAttribute("file"),"portable timing, key and normalise rules preserved");require(sharing.mergePasses==0&&!sharing.snapshot("loops.json").isObject(),"owner-only skips merged comparison");auto stamp=document.getLastModificationTime();auto bytes=sharing.writtenBytes.load();sharing.scan();sharing.scan();require(sharing.writtenBytes==bytes&&document.getLastModificationTime()==stamp,"unchanged scans do not rewrite snapshots");}
  // A collaborator still receives relative metadata rebased to their own path.
  auto foreign=root.getChildFile("Friend library");auto shared=foreign.getChildFile(".slicegroups/sharing-v2");shared.createDirectory();auto friendId=juce::Uuid().toString();auto friendFile=shared.getChildFile(friendId+".json");auto sample=foreign.getChildFile("nested/friend.wav");
  juce::var marker(new juce::DynamicObject());marker.getDynamicObject()->setProperty("version",2);marker.getDynamicObject()->setProperty("owner",friendId);shared.getChildFile("owner.json").replaceWithText(juce::JSON::toString(marker));
  auto makeRemote=[&](juce::String tag){juce::var doc=marker.clone(),fields(new juce::DynamicObject()),tags(new juce::DynamicObject()),loops(new juce::DynamicObject());tags.getDynamicObject()->setProperty("nested/friend.wav",tag);juce::XmlElement xml("SliceGroups");xml.setAttribute("bpm",131.25);xml.setAttribute("loopKey",7);loops.getDynamicObject()->setProperty("nested/friend.wav",xml.toString());fields.getDynamicObject()->setProperty("tags.json",tags);fields.getDynamicObject()->setProperty("loops.json",loops);doc.getDynamicObject()->setProperty("fields",fields);auto old=friendFile.getLastModificationTime();friendFile.replaceWithText(juce::JSON::toString(doc));friendFile.setLastModificationTime(juce::Time(old.toMilliseconds()+2000));};
  makeRemote("Shared");db->mutate("library-roots.json",[&](juce::DynamicObject& object){object.setProperty(foreign.getFullPathName(),true);});
  {OwnedLibrarySharing sharing(false);sharing.scan();require(!sharing.editable(sample)&&sharing.snapshot("tags.json").getProperty(sample.getFullPathName(),{}).toString()=="Shared","collaborator receives tags and stays read-only");auto loop=sharing.snapshot("loops.json");auto xml=juce::parseXML(loop.getProperty(sample.getFullPathName(),{}).toString());require(xml&&xml->getStringAttribute("file")==sample.getFullPathName()&&xml->getDoubleAttribute("bpm")==131.25,"collaborator timing rebased");auto* unchangedLoopLayer=loop.getDynamicObject();auto merges=sharing.mergePasses.load();
   db->mutate("tags.json",[&](juce::DynamicObject& object){object.setProperty(root.getChildFile("private.wav").getFullPathName(),"Personal");});sharing.scan();require(sharing.mergePasses==merges+1&&sharing.snapshot("loops.json").getDynamicObject()==unchangedLoopLayer,"tag edit reuses unchanged merged layers");
   makeRemote("New tag value");sharing.scan();require(sharing.snapshot("tags.json").getProperty(sample.getFullPathName(),{}).toString()=="New tag value","future collaborator updates remain live");require(!sharing.snapshot("favourites.json").isObject(),"favourites remain private");
  }
  std::cout<<"Resume, unchanged polling, owner-only fast path, per-kind revisions, collaborator updates/rebasing/read-only and private favourites passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 4281;}
}
