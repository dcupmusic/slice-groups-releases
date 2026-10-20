#pragma once
int runManualRefreshChecks(){
 juce::ScopedJuceInitialiser_GUI gui;LibraryStore::diagnosticSharing=false;
 auto folder=LibraryStore::root().getChildFile("Samples");folder.createDirectory();auto original=folder.getChildFile("original.wav"),added=folder.getChildFile("finder-added.wav");original.replaceWithText("name-only test");added.deleteFile();BrowserSearchIndex::invalidate(folder);
 BrowserSearchIndex index;uint64_t token=0;juce::Array<juce::File> files;int callbacks=0;
 index.completed=[&](juce::File,juce::Array<juce::File> found,uint64_t serial){if(serial==token){files=std::move(found);++callbacks;}};
 auto now=[](){return juce::Time::getMillisecondCounterHiRes();};auto wait=[&](auto ready){double end=now()+5000;while(!ready()&&now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);return ready();};
 token=index.request(folder);if(!wait([&]{return files.contains(original)&&BrowserSearchIndex::indexExists(folder);}))return 3340;
 added.replaceWithText("external Finder change");callbacks=0;token=index.request(folder);if(!wait([&]{return callbacks>0;}))return 3341;
 double end=now()+500;while(now()<end)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.01,true);
 if(files.contains(added)||callbacks!=1)return 3342;
 token=index.request(folder,true);if(!wait([&]{return files.contains(added);}))return 3343;
 original.deleteFile();callbacks=0;token=index.request(folder);if(!wait([&]{return callbacks>0;}))return 3344;if(!files.contains(original))return 3345;
 token=index.request(folder,true);if(!wait([&]{return !files.contains(original)&&files.contains(added);}))return 3346;
 index.completed={};std::cout<<"Initial scan, cached navigation without automatic rescans, explicit Refresh additions/deletions passed\n";return 0;
}
