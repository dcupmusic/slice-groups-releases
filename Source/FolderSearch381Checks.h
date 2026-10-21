#pragma once
int runFolderSearch381(){
 juce::ScopedJuceInitialiser_GUI gui;auto root=LibraryStore::root().getChildFile("Samples"),a=root.getChildFile("PackA"),deep=a.getChildFile("Deep"),empty=a.getChildFile("Empty"),b=root.getChildFile("PackB");for(auto folder:{deep,empty,b})folder.createDirectory();
 juce::AudioBuffer<float> audio(1,1200);audio.clear();auto one=deep.getChildFile("top_loop.wav"),two=empty.getChildFile("chord.wav"),three=b.getChildFile("top_other.wav");for(auto file:{one,two,three})writeRenderedAudio(file,audio,12000);
 LibraryStore::addLibraryRoot(root);LibraryStore::addTag({one},"top, warm");LibraryStore::addTag({three},"top");
 Processor p(false);p.trackOffline=true;Editor editor(p);editor.diagnosticQuietAnalysis();editor.navigateForCheck(root);
 auto wait=[](auto predicate){auto until=juce::Time::getMillisecondCounterHiRes()+10000;while(!predicate()&&juce::Time::getMillisecondCounterHiRes()<until)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return predicate();};
 if(!wait([&]{return editor.indexReadyForCheck()&&editor.browserRowsForCheck()==3;}))return 3811;
 editor.expand381ForCheck(a);if(!editor.folder381Visible(empty)||!editor.folder381Visible(deep)||!editor.folder381Visible(b))return 3812;
 editor.search381ForCheck("",{"top"});if(!wait([&]{return editor.browserRowsForCheck()==2&&!editor.folder381Visible(empty);} ))return 3813;
 if(!editor.folder381Visible(a)||!editor.folder381Visible(deep)||!editor.folder381Visible(b))return 3814;
 // Selecting a matching branch must not hide matching siblings elsewhere in the library.
 editor.select381ForCheck(deep);if(!wait([&]{return editor.browserRowsForCheck()==1;}))return 3815;if(!editor.folder381Visible(b))return 3816;
 editor.search381ForCheck("",{"top","warm"});if(!wait([&]{return !editor.folder381Visible(b)&&editor.folder381Visible(deep);}))return 3817;
 editor.search381ForCheck("",{"top","warm"},{},true);if(!wait([&]{return editor.folder381Visible(b);} ))return 3818;
 editor.clearSearchForCheck();if(!wait([&]{return editor.folder381Visible(empty);} ))return 3819;
 editor.search381ForCheck("chord");if(!wait([&]{return editor.folder381Visible(empty)&&!editor.folder381Visible(deep)&&!editor.folder381Visible(b);} ))return 3820;
 editor.search381ForCheck("no matches");if(!wait([&]{return !editor.folder381Visible(a)&&!editor.folder381Visible(b);} ))return 3821;
 editor.search381ForCheck("",{}, {"top"});if(!wait([&]{return editor.folder381Visible(empty)&&!editor.folder381Visible(deep)&&!editor.folder381Visible(b);} ))return 3822;
 editor.clearSearchForCheck();if(!wait([&]{return editor.folder381Visible(empty)&&editor.folder381Visible(deep)&&editor.folder381Visible(b);} ))return 3823;
 std::cout<<"Folder pruning: tags, keywords, AND/OR, exclusions, zero results, clearing, nested parents and matching siblings outside selected scope passed\n";return 0;
}
