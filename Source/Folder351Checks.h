#pragma once
int runFolder351Checks(){
 juce::ScopedJuceInitialiser_GUI gui;auto root=LibraryStore::root().getChildFile("Folder fixture");root.createDirectory();auto target=root.getChildFile("Confirm me");target.createDirectory();auto child=target.getChildFile("audio.wav");child.replaceWithText("fixture contents");LibraryStore::addLibraryRoot(root);
 Processor p(false);p.trackOffline=true;Editor editor(p);editor.diagnosticQuietAnalysis();auto now=[](){return juce::Time::getMillisecondCounterHiRes();};auto wait=[&](auto condition){auto until=now()+5000;while(!condition()&&now()<until)CFRunLoopRunInMode(kCFRunLoopDefaultMode,.005,true);return condition();};
 editor.confirmTrash351ForCheck(target);auto* dialog=dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());if(!dialog||!child.existsAsFile()||p.trashWorker->nextOrder(false))return 3531;
 // Return is deliberately bound to Cancel, rather than the destructive action.
 static_cast<juce::Component*>(dialog)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));if(!wait([&]{return juce::Component::getCurrentlyModalComponent()==nullptr;})||!child.existsAsFile()||p.trashWorker->nextOrder(false))return 3532;
 editor.confirmTrash351ForCheck(target);dialog=dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());if(!dialog)return 3533;dialog->triggerButtonClick("Move to Trash");if(!wait([&]{return !target.exists();}))return 3534;
 editor.undoForCheck(false);if(!wait([&]{return !p.trashWorker->historyPending()&&child.existsAsFile();})||child.loadFileAsString()!="fixture contents")return 3535;
 std::cout<<"Folder confirmation blocks Trash until explicit confirmation; Return cancels; confirmed Trash remains undoable\n";return 0;
}
