#pragma once
int runPreparation340Checks(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryStore::diagnosticSharing=false;
 auto root=LibraryStore::root().getChildFile("fixtures");root.createDirectory();LibraryStore::addLibraryRoot(root);
 auto now=[](){return juce::Time::getMillisecondCounterHiRes();};
 auto wait=[&](auto condition){auto end=now()+20000;while(!condition()&&now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return condition();};
 juce::AudioBuffer<float> tone(1,48000);for(int n=0;n<48000;++n)tone.setSample(0,n,n%12000<2000?.2f*std::sin(n*.08):0.f);
 auto source=root.getChildFile("loop_120_C.wav");if(!writeRenderedAudio(source,tone,48000))return 3401;
 {LibraryPreparation::Foreground foreground;LibraryPreparation::requestScope(root);auto end=now()+150;while(now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);if(AnalysisStore::exists(source))return 3402;}
 if(!wait([&]{return AnalysisStore::exists(source)&&LoopLoudness::exists(source)&&DecodedSampleCache::instance().get(source)!=nullptr;}))return 3403;
 std::cout<<"Background persistent analysis, loudness, decoded preparation and foreground priority passed\n";
 Processor p(false);p.autoSave=false;p.trackOffline=true;p.stretchMode=true;if(!p.load(source)||p.playbackMode()!=0)return 3404;
 LibraryStore::setPlaybackType({root},4);if(!p.load(source)||p.playbackMode()!=1)return 3405;
 auto a=DecodedSampleCache::instance().get(source);if(!a)return 3406;auto stamp=source.getLastModificationTime();source.setLastModificationTime(stamp+juce::RelativeTime::seconds(2));if(DecodedSampleCache::instance().get(source))return 3407;
 DecodedSampleCache::instance().put(source,a,stamp,source.getSize());if(DecodedSampleCache::instance().get(source))return 3408;
 for(int i=0;i<30;++i){auto f=root.getChildFile("cache"+juce::String(i)+".wav");f.replaceWithText("fixture");DecodedSampleCache::instance().put(f,a,f.getLastModificationTime(),f.getSize());}if(DecodedSampleCache::instance().memory()>DecodedSampleCache::budget)return 3409;
 std::cout<<"Default Slice, inherited Stretch, changed-file invalidation, stale insert rejection and bounded cache passed\n";
 // Every native Trash operation below is limited to these fresh, isolated fixtures.
 FileTrashWorker trash;int callbacks=0;bool failed=false;trash.setCompleted([&](juce::Array<juce::File>,juce::Array<juce::File> errors,bool){++callbacks;failed=!errors.isEmpty();});
 auto folder=root.getChildFile("trash-folder");folder.createDirectory();auto child=folder.getChildFile("sample.wav");child.replaceWithText("audio fixture");folder.getChildFile(".slicegroups").createDirectory();folder.getChildFile(".slicegroups/metadata.json").replaceWithText("shared metadata");
 trash.request({folder,child});if(!wait([&]{return callbacks==1;})||failed||folder.exists())return 3410;
 if(!trash.undoRedo(false)||!wait([&]{return callbacks==2;})||failed||child.loadFileAsString()!="audio fixture"||folder.getChildFile(".slicegroups/metadata.json").loadFileAsString()!="shared metadata")return 3411;
 if(!trash.undoRedo(true)||!wait([&]{return callbacks==3;})||failed||folder.exists())return 3412;
 folder.createDirectory();folder.getChildFile("collision").replaceWithText("keep");if(!trash.undoRedo(false)||!wait([&]{return callbacks==4;})||!failed||folder.getChildFile("collision").loadFileAsString()!="keep")return 3413;
 folder.getChildFile("collision").deleteFile();folder.deleteFile();if(!trash.undoRedo(false)||!wait([&]{return callbacks==5;})||failed||!child.existsAsFile())return 3414;
 auto file=root.getChildFile("trash-file.txt");file.replaceWithText("restore me");trash.request({file});if(!trash.undoRedo(false)||!wait([&]{return callbacks==7;})||failed||file.loadFileAsString()!="restore me")return 3415;
 if(!trash.nextOrder(true))return 3416;trash.discardRedo();if(trash.nextOrder(true))return 3417;
 std::cout<<"Native file/folder Trash, nested selection, shared metadata restore, redo, collision refusal/retry, queued immediate undo and redo invalidation passed\n";
 // Cached load timing includes install/detection; callback must finish, not merely enqueue.
 LibraryStore::setPlaybackType({root},4);auto loaded=p.readAudio(source);DecodedSampleCache::instance().put(source,loaded,source.getLastModificationTime(),source.getSize());LoopLoader loader(p);bool complete=false;loader.completed=[&](bool ok,juce::File,uint64_t){complete=ok;loader.busy=false;};loader.startThread();auto start=now();loader.request(source);if(!wait([&]{return complete;}))return 3418;std::cout<<"Cached Stretch load including install: "<<loader.lastLoadMs.load()<<" ms; callback delivered "<<now()-start<<" ms\n";

 // Prove speculative Stretch output matches the eventual foreground request.
 auto nearby=root.getChildFile("nearby_120_D.wav");writeRenderedAudio(nearby,tone,48000);p.hasHost=true;p.hostBpm=132;p.pitchSemitones=2;
 loader.prepareUpcoming({nearby},"warm-at-132-pitch-2");std::shared_ptr<const AudioData> nearbyData;
 if(!wait([&]{nearbyData=DecodedSampleCache::instance().get(nearby);return nearbyData!=nullptr;}))return 3428;
 Processor expected(false);expected.audio=p.audio;auto settings=p.configurationXml(true);expected.applyConfiguration(*settings,true);expected.autoSave=expected.autoCategories=false;expected.hasHost=true;expected.hostBpm=132;
 if(!expected.installAudio(nearbyData,true,0,false))return 3429;auto expectedRequest=expected.stretchRequest();if(!wait([&]{return p.stretchWorker.cached(expectedRequest)!=nullptr;}))return 3430;
 complete=false;start=now();loader.request(nearby);if(!wait([&]{return complete;}))return 3431;auto ready=p.stretchWorker.result();if(!ready||!(ready->request==p.stretchRequest())||p.stretchWorker.busy)return 3432;
 std::cout<<"Prepared neighbouring Stretch render reused at 132 BPM / +2 semitones; installed and ready in "<<now()-start<<" ms\n";
 p.pitchSemitones=0;
 // Exercise the actual editor undo dispatcher across parameter and filesystem edits.
 auto browser=root.getChildFile("browser"),sub=browser.getChildFile("Nested");sub.createDirectory();auto victim=sub.getChildFile("victim.wav"),keep=browser.getChildFile("keep.wav");writeRenderedAudio(victim,tone,48000);writeRenderedAudio(keep,tone,48000);
 LibraryStore::update("settings.json",[](juce::DynamicObject& o){o.setProperty("browserAutoLoad",false);});
 p.audio=loaded;p.split(4,false);p.resetHistory();Editor editor(p);editor.diagnosticQuietAnalysis();editor.navigateForCheck(browser);editor.scopeSearchForCheck("wav",false);
 if(!wait([&]{return editor.indexReadyForCheck()&&editor.visible340ForCheck(victim)&&editor.visible340ForCheck(keep);}))return 3419;
 editor.trash340ForCheck(sub);if(editor.visible340ForCheck(victim))return 3420;
 editor.search340ForCheck("victim");auto settle=now()+300;while(now()<settle)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);if(editor.visible340ForCheck(victim)||sub.exists())return 3421;
 p.pitchSemitones=2;p.edited();editor.undoForCheck(false);if(p.pitchSemitones!=0||sub.exists())return 3422;
 editor.undoForCheck(false);if(!wait([&]{return !p.trashWorker->historyPending()&&victim.existsAsFile();}))return 3423;
 if(!wait([&]{return editor.visible340ForCheck(victim);}))return 3424;
 editor.undoForCheck(true);if(!wait([&]{return !p.trashWorker->historyPending()&&!sub.exists();}))return 3425;editor.undoForCheck(true);if(p.pitchSemitones!=2)return 3426;
 editor.undoForCheck(false);editor.undoForCheck(false);if(!wait([&]{return !p.trashWorker->historyPending()&&victim.existsAsFile();}))return 3427;
 std::cout<<"Folder browser immediate subtree removal, stale search rejection, restore refresh and mixed parameter/Trash undo-redo ordering passed\n";
 return 0;
}
