#pragma once
int runJson341Checks(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryStore::diagnosticSharing=false;
 if(!LibraryStore::diagnosticOldSave||!LibraryPreparation::checksPaused)return 3441;
 auto source=LibraryStore::root().getChildFile("sample.wav");source.getParentDirectory().createDirectory();source.replaceWithText("isolated fixture");
 auto hash=LibraryStore::fingerprint(source);juce::XmlElement first("SliceGroups");first.setAttribute("loopKey",2);first.setAttribute("contentHash",hash);if(!LibraryStore::saveLoop(source,first.toString(),hash))return 3442;
 if(LibraryStore::loopConfigFile("path:"+source.getFullPathName()).existsAsFile()||LibraryStore::root().getChildFile("loop-index-v2.json").existsAsFile())return 3443;
 auto oldBytes=LibraryStore::root().getChildFile("loops.json").getSize();auto stale=LibraryStore::loopConfigFile("path:"+source.getFullPathName());stale.getParentDirectory().createDirectory();first.setAttribute("loopKey",8);stale.replaceWithText(first.toString());
 auto recalled=LibraryStore::savedLoop(source,hash);if(!recalled||recalled->getIntAttribute("loopKey")!=2)return 3444;
 first.setAttribute("loopKey",5);if(!LibraryStore::saveLoop(source,first.toString(),hash))return 3445;recalled=LibraryStore::savedLoop(source,hash);if(!recalled||recalled->getIntAttribute("loopKey")!=5)return 3446;
 auto renamed=LibraryStore::root().getChildFile("renamed.wav");renamed.replaceWithText("isolated fixture");recalled=LibraryStore::savedLoop(renamed,hash);if(!recalled||recalled->getIntAttribute("loopKey")!=5)return 3447;
 LibraryPreparation::requestScope(LibraryStore::root());if(LibraryPreparation::status().isNotEmpty())return 3448;
 std::cout<<"JSON-only save writes/recall, priority over stale v2, hash recall after rename and disabled background preparation passed; fixture JSON "<<oldBytes<<" bytes\n";return 0;
}
