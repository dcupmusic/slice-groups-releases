#include "Rack.h"
#include <iostream>
#include <thread>
#include "RackChecks.h"
#include "TrackChecks.h"
#include "SharedLibraryChecks.h"
#include "StemChecks.h"
#include "RawStemChecks.h"
#include "HQStemChecks.h"
#include "TrackWorkflowChecks.h"
#include "WaveformVisualChecks.h"

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RackProcessor(); }

void splitForLegacyCheck(Processor& p,int count){p.split(count,false);for(int i=0;i<(int)p.slices.size();++i)p.slices[i].note=36+i;}

int runSliceGroupsChecks() {
    LibraryStore::writeSwing("A",{false,50});
    Processor p;p.chromaticMapping=false;p.combinedTiming=false;p.setPlayMode(false); auto data = std::make_shared<AudioData>();data->buffer.setSize(1,4000);data->buffer.clear();p.audio = data;splitForLegacyCheck(p,4);
    for (auto& s : p.slices) s.note = 36;
    for (int i = 0; i < 12; ++i) { p.trigger(36,1);if (p.last[36] != i%4) return 1; }
    p.randomOther = true;int previous = p.last[36];
    for (int i = 0; i < 10000; ++i) { p.trigger(36,1);if (p.last[36] == previous) return 2;previous = p.last[36]; }
    splitForLegacyCheck(p,1);for (int i = 0; i < 20; ++i) { p.trigger(36,1);if (p.last[36] != 0) return 3; }
    splitForLegacyCheck(p,4);p.prepareToPlay(44100,512);juce::AudioBuffer<float> out(2,512);juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)100),128);p.processBlock(out,midi);
    if (!p.voices[0].active || p.voices[0].pos != 384) return 4;
    splitForLegacyCheck(p,4);p.toggleMember(2,36);p.toggleMember(3,36);
    if (!p.addBoundary(1500) || p.slices[3].note != 36 || p.slices[4].note != 36 || p.slices[2].note == 36) return 5;
    if (!p.removeBoundary(2) || p.slices.size() != 4 || p.slices[1].end != 2000 || p.slices[2].note != 36) return 6;
    p.toggleMember(2,36);if (p.slices[2].note != -1) return 7;p.toggleMember(2,36);
    auto exported = p.makeMidi(120);juce::MemoryOutputStream stream;
    if (!exported.writeTo(stream,0)) return 8;
    juce::MemoryInputStream input(stream.getData(),stream.getDataSize(),false);juce::MidiFile parsed;
    if (!parsed.readFrom(input) || parsed.getNumTracks() != 1) return 9;
    int events = 0;std::array<int,4> expectedNotes {36,37,36,36};
    for (int i = 0; i < parsed.getTrack(0)->getNumEvents(); ++i) {
        auto msg = parsed.getTrack(0)->getEventPointer(i)->message;
        if (msg.isTempoMetaEvent() || msg.isTimeSignatureMetaEvent()) return 17;
        if (msg.isNoteOn()) {
            if (events >= 4 || msg.getNoteNumber() != expectedNotes[events] || std::abs(msg.getTimeStamp()-events*1000.0/44100*2*960) > 1) return 10;
            ++events;
        }
    }
    if (events != 4 || std::abs(parsed.getLastTimestamp()-4000.0/44100*2*960) > 1) return 11;
    // Sensitivity check: weak repeated transients should emerge at high strength.
    auto detected = std::make_shared<AudioData>();detected->buffer.setSize(1,44100);detected->buffer.clear();
    for (int onset : {5000,12000,19000,26000,33000})
        for (int n = onset; n < onset+500; ++n) detected->buffer.setSample(0,n,.035f);
    p.audio = detected;p.detect(0);int low = (int)p.slices.size();p.detect(100);
    if ((int)p.slices.size() <= low) return 12;
    for (int i = 0; i < (int)p.slices.size(); ++i) if (p.slices[i].start != (i == 0 ? 0 : p.slices[i-1].end) || p.slices[i].end <= p.slices[i].start) return 13;
    if (p.slices.back().end != 44100) return 14;
    // Real output must respect MIDI sample offsets.
    detected->buffer.clear();for (int n = 0; n < 44100; ++n) detected->buffer.setSample(0,n,.5f);
    splitForLegacyCheck(p,16);midi.clear();midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),128);p.processBlock(out,midi);
    for (int n = 0; n < 128; ++n) if (out.getSample(0,n) != 0) return 15;
    if (out.getSample(0,160) < .4f) return 16;
    if (((const unsigned char*)stream.getData())[9] != 0) return 18;
    std::cout << "Sequential, random-other, single-member, MIDI timing/export, marker editing, group preservation and detect-strength checks passed.\n";
    return 0;
}

int runSliceGroupsGuiCheck(const char* path) {
    LibraryStore::writeSwing("A",{false,50});
    juce::ScopedJuceInitialiser_GUI init;
    Processor p;p.chromaticMapping=false;p.combinedTiming=false;auto data = std::make_shared<AudioData>();data->buffer.setSize(1,88200);data->buffer.clear();
    for (int n = 0; n < 88200; ++n) {
        double phase = (n%5512)/44100.0;
        float amplitude = (float)(std::sin(n*.09)*std::exp(-phase*45));data->buffer.setSample(0,n,amplitude*.8f);
    }
    data->peaks.resize((88200+127)/128);
    for (int n = 0; n < 88200; ++n) data->peaks[n/128] = juce::jmax(data->peaks[n/128],std::abs(data->buffer.getSample(0,n)));
    auto demoFolder = juce::File::getCurrentWorkingDirectory().getChildFile("work/browser-demo");demoFolder.createDirectory();
    for(auto name : {"120_Clap_loop.wav","120_Hat_loop.wav","120_Perc_loop.wav","125_Break_loop.wav"}) {
        auto file=demoFolder.getChildFile(name);juce::WavAudioFormat wav;auto fileStream=file.createOutputStream();fileStream->setPosition(0);fileStream->truncate();std::unique_ptr<juce::OutputStream> stream=std::move(fileStream);
        auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(44100).withNumChannels(1).withBitsPerSample(16));if(!writer || !writer->writeFromAudioSampleBuffer(data->buffer,0,88200))return 33;
    }
    data->path = demoFolder.getChildFile("120_Clap_loop.wav").getFullPathName();
    LibraryStore::addTag({juce::File(data->path)},"clap, house, sample packs, tops maschine, favourites");
    LibraryStore::addTag({demoFolder.getChildFile("120_Perc_loop.wav")},"perc, house");p.audio = data;splitForLegacyCheck(p,16);p.toggleMember(4,36);p.toggleMember(8,36);p.toggleMember(12,36);p.audition(4);
    {p.setStretchMode(true);Editor stretchEditor(p);if(!stretchEditor.stretchUiForCheck())return 1174;auto image=stretchEditor.createComponentSnapshot(stretchEditor.getLocalBounds());juce::FileOutputStream out(juce::String(path)+"-stretch.png");juce::PNGImageFormat png;png.writeImageToStream(image,out);p.setStretchMode(false);}
    Waveform check(p);check.setSize(1000,235);check.state.audio = p.audio;check.state.slices = p.slices;
    check.changed = [&] { check.state.slices = p.slices;check.state.voices = p.voices; };
    int opened=-1;check.categoryPopup=[&](int index,juce::Rectangle<int>){opened=index;};int voicesBefore=0;for(auto v:p.voices)voicesBefore+=v.active;check.openFooter(20);int voicesAfter=0;for(auto v:p.voices)voicesAfter+=v.active;if(opened!=0||voicesBefore!=voicesAfter)return 617;
    auto gestureEvent=[&](float x,float y,int mods,int clicks=1){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,y},juce::ModifierKeys(mods),1,0,0,0,0,&check,&check,juce::Time::getCurrentTime(),{x,y},juce::Time::getCurrentTime(),clicks,false);};
    p.assignCategory({0},39);check.changed();check.mouseDown(gestureEvent(20,220,juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::altModifier));if(p.categoryOf(p.slices[0])!=-1)return 1160;
    p.assignCategory({0},39);check.changed();check.mouseDoubleClick(gestureEvent(20,220,juce::ModifierKeys::leftButtonModifier,2));if(p.categoryOf(p.slices[0])!=-1)return 1161;
    int boundary=p.slices[1].start;float bx=(float)check.sampleToX(boundary);p.boundarySnap=0;check.mouseDown(gestureEvent(bx,100,juce::ModifierKeys::leftButtonModifier));check.mouseDrag(gestureEvent(bx+10,100,juce::ModifierKeys::leftButtonModifier));int normal=p.slices[1].start;check.mouseDrag(gestureEvent(bx+20,100,juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier));int fine=p.slices[1].start;if(normal<=boundary||fine<=normal||std::abs((fine-normal)*10-(normal-boundary))>12)return 1162;check.mouseUp(gestureEvent(bx+20,100,0));p.moveBoundary(1,boundary,false);check.changed();
    TimingField bpmCheck;bpmCheck.formatter=[](double value){return juce::String(value,2);};bpmCheck.display(123.456789,true);if(bpmCheck.getText()!="123.46")return 1163;
    p.audition(4);check.activateAt(20,false,false);
    if (!check.selected.count(0) || std::count_if(p.voices.begin(),p.voices.end(),[](const auto& v){return v.active;})!=1) return 20;
    check.activateAt(20,false,false);
    if (!check.selected.count(0)) return 21;
    p.assignCategory(check.selected,39);if(p.slices[0].note!=39)return 22;
    check.activateAt(20,true,false);if(!check.selected.empty())return 23;
    check.activateAt(20,false,false,true);
    if (p.slices.size() != 17 || p.slices[1].start != 1764) return 24;
    check.activateAt(20,false,true);
    if (p.slices.size() != 16 || p.slices[0].end != 5512) return 25;
    Pad checkPad;checkPad.setSize(168,80);int heard = -1,edited = -1;
    checkPad.audition = [&](int note) { heard = note; };checkPad.edit = [&](int note) { edited = note; };
    checkPad.activateAt(checkPad.iconBounds().getCentre());checkPad.activateAt({20,60});
    if (heard != 36 || edited != 36) return 26;
    splitForLegacyCheck(p,16);p.toggleMember(4,36);p.toggleMember(8,36);p.toggleMember(12,36);p.audition(4);
    check.setView(1,0);check.state.slices=p.slices;check.selectAt(156.25f);check.selectAt(343.75f,false,true,false);if(check.selected!=std::set<int>({2,3,4,5}))return 901;check.selectAt(468.75f,true,false,false);if(check.selected!=std::set<int>({2,3,4,5,7}))return 902;check.selectAt(281.25f,true,false,false);if(check.selected.count(4)||check.selected.size()!=4)return 903;check.selectAt(343.75f);check.selectAt(156.25f,false,true,false);if(check.selected!=std::set<int>({2,3,4,5}))return 904;check.clearSelection();if(!check.selected.empty()||check.selectionAnchor!=-1)return 905;
    check.setView(1,0);check.wheelAt(500,0,.5f,false);
    if (check.zoom <= 1 || std::abs(check.offset+.5/check.zoom-.5) > 1e-6) return 27;
    double oldZoom = check.zoom,oldOffset = check.offset;check.wheelAt(500,0,.1f,true);
    if (check.zoom != oldZoom || check.offset == oldOffset) return 28;
    checkPad.renamed = [&](int note,juce::String name) { p.setPadName(note,name); };
    checkPad.nameLabel.setText("Clap",juce::sendNotificationSync);
    if (p.padNames[36] != "Clap") return 29;
    p.setPadName(36,"Clap");p.setPadName(37,"Hat");p.setPadName(38,"Kick");p.sourceBeats = 4;
    TagPopup tagCheck(LibraryStore::read("tags.json"));juce::String applied;int nextCount=0,closed=0;
    tagCheck.apply=[&](juce::String value){applied=value;};tagCheck.next=[&]{++nextCount;};tagCheck.close=[&]{++closed;};
    for(int i=0;i<tagCheck.getNumChildComponents();++i)if(auto* input=dynamic_cast<juce::TextEditor*>(tagCheck.getChildComponent(i)))input->setText("cl",true);
    tagCheck.refreshTags(tagCheck.tags);tagCheck.handle(juce::KeyPress(juce::KeyPress::returnKey));tagCheck.handle(juce::KeyPress(juce::KeyPress::returnKey,juce::ModifierKeys::shiftModifier,0));tagCheck.handle(juce::KeyPress(juce::KeyPress::escapeKey));
    if(applied!="clap"||nextCount!=1||closed!=1)return 34;
    { Processor workerProcessor;workerProcessor.chromaticMapping=false;workerProcessor.combinedTiming=false;LoopLoader loader(workerProcessor);loader.startThread();loader.request(juce::File(data->path));loader.request(demoFolder.getChildFile("120_Hat_loop.wav"));
      for(int n=0;n<300;++n){auto loaded=workerProcessor.getAudio();if(loaded&&loaded->path.endsWith("120_Hat_loop.wav"))break;juce::Thread::sleep(10);}
      auto loaded=workerProcessor.getAudio();if(!loaded||!loaded->path.endsWith("120_Hat_loop.wav"))return 35;
      LiveDetector detector(workerProcessor);detector.startThread();detector.request(20);detector.request(95);for(int n=0;n<300&&detector.busy;++n)juce::Thread::sleep(10);
      if(detector.busy||workerProcessor.detectStrength!=95)return 36;
    }
    p.padNames=p.defaultNames;for(int i=0;i<16;++i)p.slices[i].note=-1;for(int i:{0,4,8,12})p.slices[i].note=39;p.slices[1].note=40;p.slices[2].note=36;
    LibraryStore::setFavourite(demoFolder,"Sample Packs");
    MidiDrag dragCheck;p.setPlayMode(true);dragCheck.started=[&]{p.setPlayMode(false);};dragCheck.notifyStarted(false);if(!p.playMode)return 612;dragCheck.notifyStarted(true);if(p.playMode)return 613;
    CategoryPicker picker(p.padNames);picker.resized();int picked=-2;picker.assign=[&](int note){picked=note;};picker.choose(39);if(picked!=39||picker.buttons[0].getY()<=picker.buttons[12].getY())return 614;picker.choose(-1);if(picked!=-1)return 615;
    auto pickerImage=picker.createComponentSnapshot(picker.getLocalBounds());juce::FileOutputStream pickerOut(juce::File(path).getSiblingFile("SliceGroups-category-picker.png"));pickerOut.setPosition(0);pickerOut.truncate();juce::PNGImageFormat pickerPng;pickerPng.writeImageToStream(pickerImage,pickerOut);
    p.chromaticMapping=true;p.combinedTiming=true;for(auto& slice:p.slices)slice.category=slice.note;p.remapNotes();
    Editor editor(p);if(!editor.tagRemovalForCheck())return 1710;if(!editor.padLockClickForCheck()||!p.categoryStateLocked)return 1330;if(!editor.padLockClickForCheck()||p.categoryStateLocked)return 1331;editor.selectCategoryForCheck(39);editor.envelopeFieldForCheck(1,13);for(int i:{0,4,8,12})if(p.slices[i].envelope.attack!=13)return 616;
    editor.padForCheck(3);if(editor.selectionForCheck()!=std::set<int>({0,4,8,12}))return 906;editor.envelopeClickForCheck();if(editor.selectionForCheck().size()!=4)return 907;editor.backgroundForCheck();if(!editor.selectionForCheck().empty())return 908;
    editor.selectSliceForCheck(4);editor.refreshForCheck();
    editor.assignForCheck(39);if(p.slices[4].note!=39)return 37;
    editor.selectAllForCheck();editor.envelopeForCheck({true,1,5,120});for(const auto& slice:p.slices)if(!slice.envelope.enabled||slice.envelope.decay!=120)return 38;
    editor.envelopeForCheck({false,0,0,150});editor.selectSliceForCheck(4);
    auto hidden=demoFolder.getChildFile(".cache");hidden.createDirectory();
    TimingField timingCheck;timingCheck.display(2.5);if(timingCheck.getText()!="2"&&timingCheck.getText()!="3")return 711;if(timingCheck.findColour(juce::TextEditor::textColourId)!=juce::Colour(0xffffac52))return 712;timingCheck.display(2);if(timingCheck.getText()!="2"||timingCheck.findColour(juce::TextEditor::textColourId)!=juce::Colours::white)return 713;
    double fieldValue=2;int gestures=0;timingCheck.readValue=[&]{return fieldValue;};timingCheck.writeValue=[&](double value){fieldValue=value;};timingCheck.begin=[&]{++gestures;};timingCheck.end=[&]{++gestures;};timingCheck.setSize(60,28);
    auto mouse=[&](float y,bool moved){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{10,y},juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,&timingCheck,&timingCheck,juce::Time::getCurrentTime(),{10,20},juce::Time::getCurrentTime(),1,moved);};timingCheck.mouseDown(mouse(20,false));timingCheck.mouseDrag(mouse(4,true));timingCheck.mouseUp(mouse(4,true));if(fieldValue!=4||gestures!=2)return 718;timingCheck.mouseDown(mouse(20,false));timingCheck.mouseDrag(mouse(28,true));timingCheck.mouseUp(mouse(28,true));if(fieldValue!=3)return 719;
    fieldValue=120;timingCheck.fine=true;timingCheck.mouseDown(mouse(20,false));juce::MouseEvent fineDrag(juce::Desktop::getInstance().getMainMouseSource(),{10,4},juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier,1,0,0,0,0,&timingCheck,&timingCheck,juce::Time::getCurrentTime(),{10,20},juce::Time::getCurrentTime(),1,true);timingCheck.mouseDrag(fineDrag);timingCheck.mouseUp(fineDrag);if(std::abs(fieldValue-120.2)>1e-8)return 826;
    auto subfolder=demoFolder.getChildFile("Subfolder");subfolder.createDirectory();editor.navigateForCheck(demoFolder);if(editor.folderNameForCheck(0)!="Subfolder")return 39;editor.navigateForCheck(subfolder);if(editor.browserRowsForCheck()!=0)return 85;editor.navigateForCheck(demoFolder);
    if(!editor.autoLoadForCheck())return 2033;if(editor.categoryNameForCheck(3)!="Clap")return 86;for(int row=0;row<editor.browserRowsForCheck();++row)if(editor.browserNameForCheck(row).startsWithChar('.'))return 714;
    if (editor.getWidth() != 1360 || editor.getHeight() != 1200 || !editor.isResizable()) return 32;
    p.assignCategory({12},40);editor.selectSliceForCheck(4);editor.assignForCheck(39);if(!editor.suggestionsForCheck().empty())return 715;editor.rptToggleForCheck();if(p.slices[12].note!=39)return 716;editor.undoForCheck(false);if(p.slices[12].category!=40||p.slices[4].note!=39)return 717;editor.selectSliceForCheck(4);editor.assignForCheck(39);editor.rptToggleForCheck();
    ZoomBar barCheck;barCheck.setSize(1000,16);double barStart=0,barSpan=1;barCheck.changed=[&](double a,double b){barStart=a;barSpan=b;};auto barMouse=[&](float x){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,8},juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,&barCheck,&barCheck,juce::Time::getCurrentTime(),{x,8},juce::Time::getCurrentTime(),1,true);};barCheck.setCurrentRange(0,1,juce::dontSendNotification);barCheck.mouseDown(barMouse(999));barCheck.dragTo(.5);barCheck.mouseUp(barMouse(500));if(barStart!=0||barSpan!=.5)return 822;barCheck.mouseDown(barMouse(250));barCheck.dragTo(.5);barCheck.mouseUp(barMouse(500));if(barStart!=.25||barSpan!=.5)return 823;barCheck.mouseDown(barMouse(250));barCheck.dragTo(.375);barCheck.mouseUp(barMouse(375));if(barStart!=.375||barSpan!=.375)return 824;
    int widthBefore=editor.getWidth(),waveBefore=editor.waveWidthForCheck();editor.toggleBrowserForCheck();if(editor.getWidth()!=widthBefore-(p.browserWidth+16)||editor.waveWidthForCheck()!=waveBefore)return 817;{auto image=editor.createComponentSnapshot(editor.getLocalBounds());juce::FileOutputStream out(juce::String(path)+"-browser-hidden.png");out.setPosition(0);out.truncate();juce::PNGImageFormat png;png.writeImageToStream(image,out);}editor.toggleBrowserForCheck();if(editor.getWidth()!=widthBefore||editor.waveWidthForCheck()!=waveBefore)return 818;
    editor.setSize(1000,900);p.browserWidth=350;editor.resized();int edgeWidth=editor.waveWidthForCheck();editor.toggleBrowserForCheck();if(editor.getWidth()!=634||editor.waveWidthForCheck()!=edgeWidth)return 829;editor.toggleBrowserForCheck();p.browserWidth=320;editor.setSize(1360,1200);editor.resized();
    double bpmBefore=p.sourceBpm;editor.barsForCheck(.5);if(p.sourceBpm!=bpmBefore||std::abs(editor.zoomForCheck()-2)>1e-5)return 819;editor.doubleForCheck();if(std::abs(editor.zoomForCheck()-1)>1e-5)return 820;editor.halfForCheck();if(std::abs(editor.zoomForCheck()-2)>1e-5)return 821;editor.doubleForCheck();
    {double tempo=p.sourceBpm,beats=p.loopBeats();if(!editor.scaleLabelsForCheck())return 1090;editor.bpmDoubleForCheck();if(p.sourceBpm!=tempo*2||p.loopBeats()!=beats)return 1091;editor.bpmHalfForCheck();if(p.sourceBpm!=tempo||p.loopBeats()!=beats)return 1092;editor.bpmHalfForCheck();if(p.sourceBpm!=tempo*.5)return 1093;editor.bpmDoubleForCheck();if(p.sourceBpm!=tempo)return 1094;editor.bpmDoubleForCheck();if(!p.undoRedo(false)||p.sourceBpm!=tempo||!p.undoRedo(true)||p.sourceBpm!=tempo*2)return 1095;editor.bpmHalfForCheck();p.setSourceBpm(400);editor.bpmDoubleForCheck();if(p.sourceBpm!=800)return 1096;p.setSourceBpm(tempo);editor.refreshForCheck();}
    if(p.slices[4].category!=39)return 825;
    NudgeButton fineArrow(true);int nudgeCount=0,beginCount=0,endCount=0;fineArrow.step=[&]{++nudgeCount;};fineArrow.begin=[&]{++beginCount;};fineArrow.finish=[&]{++endCount;};fineArrow.setSize(20,14);
    auto arrowEvent=juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{5,5},juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,&fineArrow,&fineArrow,juce::Time::getCurrentTime(),{5,5},juce::Time::getCurrentTime(),1,false);
    fineArrow.mouseDown(arrowEvent);fineArrow.mouseUp(arrowEvent);if(nudgeCount!=1||beginCount!=1||endCount!=1||fineArrow.repeatMs!=30)return 974;
    fineArrow.mouseDown(arrowEvent);if(fineArrow.timerIntervalForCheck()!=500)return 1011;fineArrow.repeatForCheck();if(fineArrow.timerIntervalForCheck()!=30||nudgeCount!=3)return 1012;fineArrow.repeatForCheck();fineArrow.mouseUp(arrowEvent);if(nudgeCount!=4||beginCount!=2||endCount!=2)return 1013;
    PitchKnob pitchTest;pitchTest.setRange(-24,24,0);pitchTest.setValue(7.25);pitchTest.setSize(50,60);
    auto pitchEvent=[&](juce::ModifierKeys mods){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{20,20},mods,1,0,0,0,0,&pitchTest,&pitchTest,juce::Time::getCurrentTime(),{20,20},juce::Time::getCurrentTime(),1,false);};
    auto resetEvent=pitchEvent(juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::altModifier);pitchTest.mouseDown(resetEvent);pitchTest.mouseUp(resetEvent);if(pitchTest.getValue()!=0)return 975;
    auto fineEvent=pitchEvent(juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::shiftModifier);pitchTest.mouseDown(fineEvent);if(pitchTest.getMouseDragSensitivity()!=3000)return 976;pitchTest.mouseUp(fineEvent);
    int browserBefore=p.browserWidth;editor.browserDragForCheck(20);if(p.browserWidth!=browserBefore+20)return 966;editor.browserDragForCheck(-20);if(p.browserWidth!=browserBefore)return 967;
    {int width=editor.getWidth(),height=editor.getHeight();editor.setUiScale(1.5);if(std::abs(editor.getTransform().mat00-1.5f)>.0001||editor.getWidth()!=width||editor.getHeight()!=height)return 1189;Processor preferences(false);if(std::abs(preferences.uiScale-1.5)>.0001)return 1190;editor.setScaleFactor(1.25);if(std::abs(editor.getTransform().mat00-1.875f)>.0001)return 1191;editor.browserDragForCheck(30);editor.browserDragForCheck(-30);if(p.browserWidth!=browserBefore)return 1192;editor.setUiScale(1);editor.setScaleFactor(1);}
    {auto parent=juce::File::getCurrentWorkingDirectory().getChildFile("work/v0836-navigation");parent.createDirectory();for(int i=0;i<90;++i)parent.getChildFile("A"+juce::String(i).paddedLeft('0',3)).createDirectory();auto child=parent.getChildFile("T_target");child.createDirectory();auto generation=p.loadGeneration.load();editor.navigateForCheck(child);editor.browserParentForCheck();if(editor.browserSelectedForCheck()!=child||!editor.browserSelectionVisibleForCheck()||p.loadGeneration.load()!=generation)return 1193;editor.navigateForCheck(demoFolder);editor.browserKeyEditForCheck(juce::File(data->path),6);if(p.loopKey!=6||editor.browserKeyForCheck(juce::File(data->path))!=6||LibraryStore::fileKey(juce::File(data->path))!=6)return 1194;editor.browserKeyEditForCheck(juce::File(data->path),-1);}
    editor.presetSavedForCheck();if(editor.presetViewForCheck())return 968;
    if(editor.envelopeIntervalForCheck()!=0)return 960;
    editor.selectSliceForCheck(4);editor.replaceForCheck(0);if(p.slices[4].replacement!=0)return 961;
    editor.envelopeFieldForCheck(1,.007);if(std::abs(p.slices[0].envelope.attack-.007)>1e-9)return 962;
    auto wideFolder=juce::File::getCurrentWorkingDirectory().getChildFile("work/browser-width-check/WideNames");wideFolder.createDirectory();wideFolder.getChildFile("A very long sample filename that exceeds the width of the browser comfortably.wav").replaceWithText("");
    editor.navigateForCheck(wideFolder);if(editor.browserColumnForCheck()<220)return 963;
    auto compactFolder=juce::File::getCurrentWorkingDirectory().getChildFile("work/browser-width-check/CompactNames");compactFolder.createDirectory();compactFolder.getChildFile("a.wav").replaceWithText("");editor.navigateForCheck(compactFolder);if(editor.browserColumnForCheck()>100)return 964;
    editor.navigateForCheck(demoFolder);

    if(!p.savePreset("Zebra GUI",true)||!p.savePreset("Alpha GUI",true))return 940;
    editor.presetsForCheck();if(editor.browserRowsForCheck()<2||editor.browserNameForCheck(0)!="Alpha GUI")return 941;
    auto savedPreset=LibraryStore::findPreset("Alpha GUI");auto target=juce::XmlDocument::parse(savedPreset);p.setSliceGain({4},-9);
    {LoopLoader presetLoader(p);presetLoader.startThread();presetLoader.request(savedPreset);
        for(int n=0;n<300;++n){bool ready;{const juce::ScopedLock g(p.lock);ready=p.slices[4].gainDb==0;}if(ready)break;juce::Thread::sleep(10);}
        if(p.slices[4].gainDb!=0)return 942;
    }
    editor.refreshForCheck();{auto img=editor.createComponentSnapshot(editor.getLocalBounds());juce::FileOutputStream out{juce::File(juce::String(path)+"-presets.png")};juce::PNGImageFormat png;if(!png.writeImageToStream(img,out))return 943;}
    editor.navigateForCheck(demoFolder);
    if(editor.windowHasValueForCheck())return 1014;
    editor.navigateForCheck(demoFolder);editor.clickFolderForCheck(0);if(editor.browserRowsForCheck()!=0)return 1015;editor.navigateForCheck(demoFolder);
    auto favouriteSample=demoFolder.getChildFile("120_Clap_loop.wav");LibraryStore::setFavourite(favouriteSample,{});editor.favouriteFileForCheck(favouriteSample);if(!LibraryStore::read("favourites.json").getDynamicObject()->hasProperty(favouriteSample.getFullPathName()))return 1016;
    {auto generation=p.loadGeneration.load();int chips=editor.favouriteChipsForCheck();if(!editor.inlineClickForCheck(favouriteSample,false))return 1020;if(editor.favouriteChipsForCheck()!=chips||p.loadGeneration.load()!=generation)return 1021;if(LibraryStore::read("favourites.json").getDynamicObject()->hasProperty(favouriteSample.getFullPathName()))return 1022;editor.inlineClickForCheck(favouriteSample,false);if(!editor.inlineClickForCheck(favouriteSample,true)||editor.searchForCheck()!="clap"||p.loadGeneration.load()!=generation)return 1023;if(editor.browserRowsForCheck()!=1)return 1024;editor.clearSearchForCheck();editor.navigateForCheck(demoFolder);}
    editor.toggleFavouriteFilterForCheck();if(editor.browserRowsForCheck()!=1||editor.browserNameForCheck(0)!="120_Clap_loop.wav")return 1030;
    editor.inlineClickForCheck(favouriteSample,false);if(editor.browserRowsForCheck()!=0)return 1031;editor.toggleFavouriteFilterForCheck();editor.favouriteFileForCheck(favouriteSample);
    int beforeEnd=p.regionEnd();double beforeBars=p.loopBeats()/p.sourceBeatsPerBar;editor.declareBarsForCheck(beforeBars*2);if(std::abs(p.loopBeats()/p.sourceBeatsPerBar-beforeBars*2)>1e-4||p.regionEnd()==beforeEnd)return 1017;editor.declareBarsForCheck(beforeBars);
    auto save = [&](const juce::String& outputPath) {
        auto img = editor.createComponentSnapshot(editor.getLocalBounds());juce::FileOutputStream out {juce::File(outputPath)};
        out.setPosition(0);out.truncate();juce::PNGImageFormat png;return out.openedOk() && png.writeImageToStream(img,out);
    };
    {Processor dragProcessor;dragProcessor.audio=data;dragProcessor.snapMode=0;dragProcessor.split(8,false);Waveform dragWave(dragProcessor);dragWave.setSize(1000,200);dragWave.state.audio=data;dragWave.state.slices=dragProcessor.slices;dragWave.state.trimEnd=88200;
        auto event=[&](float x){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,10},juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,&dragWave,&dragWave,juce::Time::getCurrentTime(),{1000,10},juce::Time::getCurrentTime(),1,true);};
        dragWave.mouseDown(event(1000));dragWave.mouseDrag(event(1100));dragWave.state.trimEnd=dragProcessor.regionEnd();dragWave.mouseDrag(event(1200));dragWave.mouseUp(event(1200));if(dragProcessor.regionEnd()!=110250)return 993;
    }
    editor.barsForCheck(2);if(p.regionEnd()<=p.getAudio()->buffer.getNumSamples()||editor.zoomForCheck()!=1)return 991;
    if(!save(juce::String(path)+"-padded.png"))return 992;editor.barsForCheck(1);
    {auto previous=p.configurationXml(true);p.alignGridBeat(4,95000,4);editor.refreshForCheck();if(!save(juce::String(path)+"-grid.png"))return 1068;p.applyConfiguration(*previous,true);editor.refreshForCheck();}
    editor.setSize(1000,900);if (!save(juce::String(path)+"-small.png")) return 30;
    editor.setSize(1600,1100);if (!save(juce::String(path)+"-large.png")) return 31;
    editor.setSize(1360,1000);auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    juce::FileOutputStream output {juce::File(path)};juce::PNGImageFormat format;
    output.setPosition(0);output.truncate();
    bool written=output.openedOk()&&format.writeImageToStream(image,output);
    editor.presetsForCheck();p.setSliceGain({4},-9);editor.recallPresetForCheck(0);
    bool recalled=false;for(int n=0;n<300;++n){{const juce::ScopedLock g(p.lock);recalled=p.slices[4].gainDb==0;}if(recalled)break;juce::Thread::sleep(10);}
    if(!recalled)return 944;
    editor.browserBulkKeyForCheck({favouriteSample,juce::File(data->path)},7);if(LibraryStore::fileKey(favouriteSample)!=7||LibraryStore::fileKey(juce::File(data->path))!=7)return 1224;editor.warpModeForCheck();p.cutWarp(0,1);p.cutWarp(1,2);p.addWarpPoint(0,.5);p.moveWarpPoint(0,1,.75);editor.refreshForCheck();if(!save(juce::String(path)+"-warp.png"))return 1209;
    {Processor firstFile(false);firstFile.audio=data;firstFile.split(4);Editor firstEditor(firstFile);auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/first-file-click-fixture");folder.createDirectory();auto sample=folder.getChildFile("A.wav");juce::File(data->path).copyFileTo(sample);firstEditor.navigateForCheck(folder);auto serial=firstFile.loadGeneration.load();if(firstEditor.browserSelectedForCheck()!=sample)return 1270;firstEditor.clickDirectoryForCheck(0);if(firstFile.loadGeneration.load()!=serial+1)return 1271;bool loaded=false;for(int n=0;n<300;++n){auto current=firstFile.getAudio();if(current&&current->path==sample.getFullPathName()){loaded=true;break;}juce::Thread::sleep(10);}if(!loaded)return 1272;}
    return written?0:1;
}

class CheckPlayHead final : public juce::AudioPlayHead {
public:
    PositionInfo position;
    juce::Optional<PositionInfo> getPosition() const override { return position; }
};
int runSliceGroupsTransportChecks() {
    LibraryStore::writeSwing("A",{false,50});
    Processor p;p.chromaticMapping=false;p.combinedTiming=false;CheckPlayHead host;p.setPlayHead(&host);p.prepareToPlay(48000,256);
    auto data = std::make_shared<AudioData>();data->rate = 48000;data->buffer.setSize(1,96000);
    for (int n = 0; n < 96000; ++n) data->buffer.setSample(0,n,(float)(n/24000+1)*.1f);
    p.audio = data;splitForLegacyCheck(p,4);p.sourceBeats = 4;p.sourceBpm = 120;
    host.position.setBpm(120);host.position.setPpqPosition(0);host.position.setIsPlaying(true);host.position.setTimeSignature(juce::AudioPlayHead::TimeSignature {4,4});
    juce::AudioBuffer<float> out(2,256);juce::MidiBuffer midi;
    auto render = [&](double ppq) { host.position.setPpqPosition(ppq);midi.clear();p.processBlock(out,midi); };
    render(0);if (std::abs(out.getSample(0,220)-.1f) > .001f) return 40;
    // Every slice is on the same pad, but Play must still reproduce source order.
    p.toggleMember(1,36);p.toggleMember(2,36);p.toggleMember(3,36);p.randomOther = true;
    render(1);if (std::abs(out.getSample(0,220)-.2f) > .001f) return 41;
    render(2);if (std::abs(out.getSample(0,220)-.3f) > .001f) return 42;
    render(3);if (std::abs(out.getSample(0,220)-.4f) > .001f) return 43;
    render(4);if (std::abs(out.getSample(0,220)-.1f) > .001f) return 44;
    // Boundary falls inside this block, not at its beginning.
    render(.995);if (out.getSample(0,64)<=0 || std::abs(out.getSample(0,220)-.2f) > .001f) return 45;
    bool accurate=false;for(const auto& voice:p.voices)if(voice.active&&voice.start==24000&&voice.pos==24136)accurate=true;if(!accurate)return 45;
    host.position.setBpm(240);render(.995);
    if(std::abs(out.getSample(0,220)-.2f)>.001f)return 46;accurate=false;for(const auto& voice:p.voices)if(voice.active&&voice.start==24000&&voice.pos==24196)accurate=true;if(!accurate)return 46;
    // Host looping backwards must resume the correct slice, including mid-slice seeks.
    render(2.5);if (std::abs(out.getSample(0,220)-.3f) > .001f) return 47;
    host.position.setIsPlaying(false);render(2.5);if (out.getSample(0,220) != 0) return 48;
    // Play also accepts incoming MIDI while the host is stopped.
    midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);p.processBlock(out,midi);
    if (out.getMagnitude(0,out.getNumSamples()) <= .05f) return 49;p.releaseResources();
    p.setPlayMode(false);p.randomOther = false;host.position.setIsPlaying(true);render(0);
    if (out.getMagnitude(0,out.getNumSamples()) != 0) return 50;
    midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),64);p.processBlock(out,midi);
    if (out.getSample(0,32) != 0 || std::abs(out.getSample(0,100)-.1f) > .001f) return 51;
    auto named = estimateLoopTiming(data->buffer,48000,"break_120bpm",{},120,4);
    if (std::abs(named.bpm-120) > .001 || named.bars != 1 || named.method != "Filename") return 52;
    auto plain = estimateLoopTiming(data->buffer,48000,"break",{},120,4);
    if (std::abs(plain.bpm-120) > .001 || plain.bars != 1) return 53;
    juce::StringPairArray metadata;metadata.set(juce::WavAudioFormat::acidBeats,"8");metadata.set(juce::WavAudioFormat::acidOneShot,"0");
    auto acid = estimateLoopTiming(data->buffer,48000,"break",metadata,120,4);
    if (acid.bpm != 240 || acid.bars != 2 || acid.method != "Audio metadata") return 54;
    p.setSourceBars(2);if (p.sourceBpm != 240 || p.sourceBeats != 8) return 55;
    p.setSourceBpm(120);if (p.sourceBeats != 4) return 56;
    // Host meter updates are reported without adding signature events to MIDI.
    host.position.setTimeSignature(juce::AudioPlayHead::TimeSignature {3,4});render(0);
    if (p.hostNumerator != 3 || p.hostDenominator != 4) return 57;
    // Persist names, timing and mode along with a real source file.
    auto testDirectory = juce::File::getCurrentWorkingDirectory().getChildFile("work");testDirectory.createDirectory();
    auto fixture = testDirectory.getNonexistentChildFile("SliceGroups-check-120bpm",".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> output = fixture.createOutputStream();
        auto writer = wav.createWriterFor(output,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));
        if (!writer || !writer->writeFromAudioSampleBuffer(data->buffer,0,96000)) return 58;
    }
    data->path = fixture.getFullPathName();p.setPadName(36,"Clap");p.setPadName(37,"Hat");p.editorWidth = 1400;p.editorHeight = 1000;
    juce::MemoryBlock block;p.getStateInformation(block);Processor restored;restored.chromaticMapping=false;restored.combinedTiming=false;restored.setStateInformation(block.getData(),(int)block.getSize());
    fixture.deleteFile();
    if (!restored.audio || restored.padNames[36] != "Clap" || restored.padNames[37] != "Hat" || restored.playMode || restored.sourceBeats != 4 || restored.editorWidth != 1400 || restored.slices[2].note != 36) return 59;
    std::cout << "Host start/stop/seek/loop/tempo, Play/MIDI modes, BPM/bar estimates, metadata-free MIDI and state recall checks passed.\n";
    return 0;
}

int runSliceGroupsLibraryRhythmChecks() {
    LibraryStore::writeSwing("A",{false,50});
    Processor p;p.chromaticMapping=false;p.combinedTiming=false;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);data->buffer.clear();
    auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/library-check");folder.createDirectory();auto file=folder.getChildFile("120bpm.wav");
    { juce::WavAudioFormat wav;auto fileStream=file.createOutputStream();fileStream->setPosition(0);fileStream->truncate();std::unique_ptr<juce::OutputStream> stream=std::move(fileStream);auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));if(!writer||!writer->writeFromAudioSampleBuffer(data->buffer,0,96000))return 60; }
    data->path=file.getFullPathName();p.audio=data;splitForLegacyCheck(p,16);
    if(p.autoSave || p.snapMode!=1)return 61;
    p.setRhythm(true,.25,.45);
    if(std::abs(p.adjustedBeat(.30)-.25)>1e-8 || p.adjustedBeat(.24)!=.24 || p.adjustedBeat(.37)!=.37 || p.adjustedBeat(.30,true)!=.30)return 62;
    p.setSwing(true,66.6666667);if(std::abs(p.adjustedBeat(.30)-1.0/3)>1e-7 || p.adjustedBeat(.5)!=.5)return 63;
    p.setSwing(false,50);p.slices={{0,7200,36},{7200,12000,37},{12000,96000,38}};p.edited();p.rebuildTimeline();
    if(p.eventCount!=3 || std::abs(p.events[1].beat-.25)>1e-8)return 64;
    auto midi=p.makeMidi(120);int count=0;for(int i=0;i<midi.getTrack(0)->getNumEvents();++i){auto msg=midi.getTrack(0)->getEventPointer(i)->message;if(msg.isNoteOn()){if(count==1&&msg.getTimeStamp()!=240)return 65;++count;}}if(count!=3)return 66;
    for(int n=0;n<96000;++n)data->buffer.setSample(0,n,n<7200?.1f:(n<12000?.2f:.3f));
    CheckPlayHead rhythmHost;p.setPlayHead(&rhythmHost);p.prepareToPlay(48000,256);rhythmHost.position.setBpm(120);rhythmHost.position.setPpqPosition(.249);rhythmHost.position.setIsPlaying(true);
    juce::AudioBuffer<float> rhythmAudio(2,256);juce::MidiBuffer rhythmInput;p.processBlock(rhythmAudio,rhythmInput);
    if(std::abs(rhythmAudio.getSample(0,128)-.2f)>.001f)return 83;
    p.setPlayHead(nullptr);
    p.setTrimLength(2);if(p.regionEnd()!=48000 || p.loopBeats()!=2 || std::abs(p.makeMidi(120).getLastTimestamp()-1920)>1)return 67;
    p.setPadSolo(37,true);auto soloMidi=p.makeMidi(120);count=0;for(int i=0;i<soloMidi.getTrack(0)->getNumEvents();++i){auto msg=soloMidi.getTrack(0)->getEventPointer(i)->message;if(msg.isNoteOn()){if(msg.getNoteNumber()!=37)return 68;++count;}}if(count!=1)return 69;
    p.setPadMuted(37,true);if(p.audible(37)||p.audible(36))return 70;p.setPadMuted(37,false);p.setPadSolo(37,false);
    p.setPadName(36,"Saved Clap");if(!p.saveLoop() || p.libraryDirty)return 71;
    p.setPadName(36,"Unsaved Clap");juce::MemoryBlock project;p.getStateInformation(project);
    Processor reloaded;reloaded.chromaticMapping=false;reloaded.combinedTiming=false;if(!reloaded.load(file)||reloaded.padNames[36]!="Saved Clap"||reloaded.regionEnd()!=48000)return 72;
    Processor restored;restored.chromaticMapping=false;restored.combinedTiming=false;restored.setStateInformation(project.getData(),(int)project.getSize());if(restored.padNames[36]!="Unsaved Clap"||!restored.libraryDirty)return 73;
    Processor another;another.chromaticMapping=false;another.combinedTiming=false;another.load(file);if(another.padNames[36]!="Saved Clap")return 74;
    p.autoSave=true;p.setPadName(36,"Auto Clap");p.lastEditTime-=1000;p.timerCallback();Processor autoRecall;autoRecall.chromaticMapping=false;autoRecall.combinedTiming=false;autoRecall.load(file);if(autoRecall.padNames[36]!="Auto Clap"||p.libraryDirty)return 84;p.autoSave=false;
    auto other=folder.getChildFile("other.wav");if(!LibraryStore::addTag({file,other},"check-perc, house")||!LibraryStore::addTag({file},"check-piano, CHECK-PERC"))return 75;
    auto tags=LibraryStore::read("tags.json");juce::StringArray uniqueTags;uniqueTags.addTokens(LibraryStore::tagsFor(tags,file),",","");uniqueTags.trim();auto before=uniqueTags.size();uniqueTags.removeDuplicates(true);if(uniqueTags.size()!=before)return 118;auto ranked=LibraryStore::rankedTags(tags,"check-p");if(ranked.size()!=2||!ranked[0].name.equalsIgnoreCase("check-perc")||ranked[0].count!=2)return 76;
    p.setSwingLink(true,"C");another.setSwingLink(true,"C");p.setSwing(true,69);another.syncLinkedSwing();if(!another.swingEnabled||another.swingAmount!=69)return 77;
    Processor unlinked;unlinked.chromaticMapping=false;unlinked.combinedTiming=false;unlinked.setSwingLink(false,{});unlinked.setSwing(false,50);unlinked.syncLinkedSwing();if(unlinked.swingEnabled)return 78;
    p.getStateInformation(project);restored.setStateInformation(project.getData(),(int)project.getSize());if(!restored.swingLinked||restored.swingGroup!="C")return 79;
    Processor perFile;perFile.chromaticMapping=false;perFile.combinedTiming=false;perFile.load(file);if(!perFile.swingLinked||perFile.swingGroup!="A")return 80;
    // Muting gates real MIDI audio, and soloing another pad suppresses it.
    data->buffer.clear();for(int n=0;n<96000;++n)data->buffer.setSample(0,n,.5f);p.audio=data;p.setSwingLink(false,{});p.setPlayMode(false);splitForLegacyCheck(p,4);p.prepareToPlay(48000,256);juce::AudioBuffer<float> out(2,256);juce::MidiBuffer incoming;
    p.setPadMuted(36,true);incoming.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);p.processBlock(out,incoming);if(out.getSample(0,220)!=0)return 81;
    p.setPadMuted(36,false);p.clearVoices();incoming.clear();incoming.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);p.processBlock(out,incoming);if(out.getSample(0,128)<.4f)return 82;
    std::cout<<"Late-only quantize, swing, trimmed MIDI length, mute/solo, explicit save versus project recall, bulk tag ranking and linked swing checks passed.\n";return 0;
}

int runSliceGroupsV05Checks() {
    LibraryStore::writeSwing("A",{false,50});
    Processor p;p.chromaticMapping=false;p.combinedTiming=false;p.setPlayMode(false);p.prepareToPlay(48000,2400);auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(2,240000);
    for(int n=0;n<240000;++n){data->buffer.setSample(0,n,.5f);data->buffer.setSample(1,n,-.5f);}p.audio=data;p.sourceBpm=120;p.split(1,false);
    if(p.slices[0].note!=-1||p.slices[0].envelope.enabled||p.padNames[39]!="Clap")return 90;
    p.assignCategory({0},39);p.setEnvelope({0},{true,10,10,20});juce::AudioBuffer<float> out(2,2400);juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1,39,(juce::uint8)127),0);midi.addEvent(juce::MidiMessage::noteOff(1,39),480);p.processBlock(out,midi);
    if(std::abs(out.getSample(0,240)-.25f)>.002f||std::abs(out.getSample(0,720)-.5f)>.002f||std::abs(out.getSample(0,1440)-.25f)>.002f||out.getSample(0,2000)!=0)return 91;
    for(auto& voice:p.voices)voice={};p.setEnvelope({0},{false,10,10,20});midi.clear();midi.addEvent(juce::MidiMessage::noteOn(1,39,(juce::uint8)127),0);p.processBlock(out,midi);if(out.getSample(0,2000)!=.5f)return 92;
    std::atomic<bool> held{false},release{false};std::thread ui([&]{const juce::ScopedLock lock(p.lock);held=true;while(!release)juce::Thread::sleep(1);});while(!held)juce::Thread::sleep(1);midi.clear();p.processBlock(out,midi);release=true;ui.join();if(out.getSample(0,0)<.49f||std::abs(out.getSample(0,200)-.5f)>.001f)return 116;
    p.processBlock(out,midi);if(std::abs(out.getSample(0,0)-.5f)>.001f||out.getSample(0,2000)!=.5f)return 117;
    p.clearVoices();midi.clear();p.processBlock(out,midi);if(std::abs(out.getSample(0,0)-.5f)>.001f||out.getSample(0,200)!=0)return 93;
    for(int n=1;n<2400;++n)for(int ch=0;ch<2;++ch)if(std::abs(out.getSample(ch,n)-out.getSample(ch,n-1))>.005f)return 94;
    midi.addEvent(juce::MidiMessage::noteOn(1,39,(juce::uint8)127),0);p.processBlock(out,midi);p.setPadMuted(39,true);midi.clear();p.processBlock(out,midi);
    if(out.getSample(0,0)<.49f||out.getSample(0,200)!=0)return 95;for(int n=1;n<2400;++n)if(std::abs(out.getSample(0,n)-out.getSample(0,n-1))>.005f)return 96;
    p.setPadMuted(39,false);for(auto& voice:p.voices)voice={};
    // Region 3..7 (1-based source beats), start on source beat 5 / beat 1 of bar 2.
    p.split(10,false);p.sourceBeats=10;for(int i=0;i<10;++i)p.slices[i].note=36+i;
    p.setTrim(48000,144000);if(p.startMarker!=96000||p.loopBeats()!=4)return 97;p.rebuildTimeline();
    std::array<int,4> starts{96000,120000,48000,72000};if(p.eventCount!=4)return 98;for(int i=0;i<4;++i)if(p.events[i].start!=starts[i]||p.events[i].beat!=i)return 99;
    auto exported=p.makeMidi(120);int count=0;std::array<int,4> notes{40,41,38,39};for(int i=0;i<exported.getTrack(0)->getNumEvents();++i){auto msg=exported.getTrack(0)->getEventPointer(i)->message;if(msg.isNoteOn()){if(count>=4||msg.getNoteNumber()!=notes[count]||msg.getTimeStamp()!=count*960)return 100;++count;}}if(count!=4||exported.getLastTimestamp()!=3840)return 101;
    p.setTrim(24000,96000);if(p.startMarker!=48000)return 102;p.setTrim(30000,40000);if(p.startMarker!=30000)return 103;
    p.setTrim(48000,144000);p.moveTrim(24000);if(p.regionStart()!=72000||p.regionEnd()!=168000||p.startMarker!=96000)return 104;
    p.setStart(120000);p.moveTrim(-24000);if(p.startAuto||p.startMarker!=120000)return 105;
    p.setStart(p.startMarker,true);if(!p.startAuto||p.startMarker!=96000)return 106;
    // A start inside a slice splits playback into two segments without losing samples.
    p.setTrim(0,96000);p.split(1,false);p.slices[0].note=39;p.setStart(24000);p.rebuildTimeline();if(p.eventCount!=2||p.events[0].start!=24000||p.events[0].end!=96000||p.events[1].start!=0||p.events[1].end!=24000)return 107;
    auto partial=p.makeMidi(120);int realOnsets=0;for(int i=0;i<partial.getTrack(0)->getNumEvents();++i){auto msg=partial.getTrack(0)->getEventPointer(i)->message;if(msg.isNoteOn()){++realOnsets;if(msg.getTimeStamp()!=2880)return 119;}}if(realOnsets!=1)return 120;
    // Play uses the same rotation as the exported MIDI.
    for(int n=0;n<240000;++n)data->buffer.setSample(0,n,(float)(n/24000+1)*.05f);
    p.setTrim(48000,144000);p.setStart(96000);p.split(4,false);CheckPlayHead host;p.setPlayHead(&host);p.setPlayMode(true);host.position.setBpm(120);host.position.setIsPlaying(true);host.position.setPpqPosition(0);p.processBlock(out,midi);if(std::abs(out.getSample(0,220)-.25f)>.001f)return 108;
    host.position.setPpqPosition(2);p.processBlock(out,midi);if(std::abs(out.getSample(0,220)-.15f)>.001f)return 109;
    // Save/restore envelopes, phase and category assignments; saved rhythm versus keep-groove.
    auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/v05-check");folder.createDirectory();auto file=folder.getChildFile("120bpm.wav");
    {juce::WavAudioFormat wav;auto fs=file.createOutputStream();fs->setPosition(0);fs->truncate();std::unique_ptr<juce::OutputStream> stream=std::move(fs);auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(16));if(!writer||!writer->writeFromAudioSampleBuffer(data->buffer,0,240000))return 110;}
    data->path=file.getFullPathName();p.assignCategory({1,2},39);p.setEnvelope({1,2},{true,2,30,240});p.setSwing(true,68);p.saveLoop();Processor restore;restore.chromaticMapping=false;restore.combinedTiming=false;if(!restore.load(file)||restore.startMarker!=96000||restore.startAuto||!restore.slices[1].envelope.enabled||restore.slices[1].envelope.hold!=30||restore.slices[2].note!=39||restore.swingAmount!=68)return 111;
    restore.keepGroove=true;restore.setSwing(true,59);restore.load(file);if(restore.swingAmount!=59||restore.slices[2].note!=39)return 112;
    juce::MemoryBlock state;p.getStateInformation(state);Processor project;project.chromaticMapping=false;project.combinedTiming=false;project.setStateInformation(state.getData(),(int)state.getSize());if(project.startMarker!=96000||project.slices[1].envelope.decay!=240)return 113;
    auto fresh=folder.getChildFile("fresh-120bpm.wav");file.copyFileTo(fresh);{auto stream=fresh.createOutputStream();stream->setPosition(fresh.getSize());stream->writeByte(1);}Processor detection;detection.chromaticMapping=false;detection.combinedTiming=false;detection.detectStrength=91;detection.importDetectStrength=91;detection.preferredSliceMethod=1;if(!detection.load(fresh)||detection.sliceMethod!=1||detection.detectStrength!=91)return 114;
    for(const auto& slice:detection.slices)if(slice.note!=-1||slice.envelope.enabled)return 115;
    std::cout<<"AHD one-shot/off, stereo stop/mute ramps, unassigned categories, beat-anchored START, rotated audio/MIDI, region moves, phase/envelope recall and persistent detection checks passed.\n";return 0;
}

int runSliceGroupsV06Checks(){
    LibraryStore::writeSwing("A",{false,50});
    Processor p;p.chromaticMapping=false;p.combinedTiming=false;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);data->buffer.clear();data->path=juce::File::getCurrentWorkingDirectory().getChildFile("work/v06-fixture.wav").getFullPathName();data->transients={12000,26000,48000};p.audio=data;p.split(4,false);p.resetHistory();
    p.assignCategory({0,2},39);if(p.slices[0].note!=39||!p.canUndo())return 601;
    if(!p.undoRedo(false)||p.slices[0].note!=-1||!p.undoRedo(true)||p.slices[2].note!=39)return 602;
    p.setEnvelope({0},{true,5,20,300});p.setEnvelope({2},{false,9,30,400});p.setEnvelopeField({0,2},1,12);
    if(p.slices[0].envelope.attack!=12||p.slices[2].envelope.attack!=12||p.slices[0].envelope.hold!=20||p.slices[2].envelope.decay!=400||p.slices[2].envelope.enabled)return 603;
    p.resetHistory();int original=p.slices[1].start;p.beginEdit();for(int i=0;i<100;++i)p.moveBoundary(1,original+100+i,false);p.endEdit();if(p.undoStates.size()!=1||!p.undoRedo(false)||p.slices[1].start!=original)return 604;
    p.boundarySnap=2;p.moveBoundary(1,25500);if(p.slices[1].start!=26000||p.slices[0].end!=26000)return 605;
    p.boundarySnap=0;p.moveBoundary(1,90000);if(p.slices[1].start!=p.slices[1].end-1)return 606;
    p.assignCategory({0,2},-1);if(p.slices[0].note!=-1||p.canRedo())return 607;
    p.resetHistory();p.setTrim(12000,72000);p.setStart(48000);if(!p.undoRedo(false)||!p.startAuto||!p.undoRedo(false)||p.regionStart()!=0)return 608;
    auto dir=juce::File::getCurrentWorkingDirectory().getChildFile("work/browser-demo");if(!LibraryStore::setFavourite(dir,"Samples")||LibraryStore::read("favourites.json").getDynamicObject()->getProperty(dir.getFullPathName()).toString()!="Samples")return 609;
    LibraryStore::setFavourite(dir,"Drums");if(LibraryStore::read("favourites.json").getDynamicObject()->getProperty(dir.getFullPathName()).toString()!="Drums")return 610;
    LibraryStore::setFavourite(dir,{});if(LibraryStore::read("favourites.json").getDynamicObject()->hasProperty(dir.getFullPathName()))return 611;
    std::cout<<"v0.6 checks passed: category edits, mixed AHD preservation, undo/redo gestures, boundary constraints/snapping and favourites\n";return 0;
}

int runSliceGroupsV07Checks(){
    LibraryStore::writeSwing("A",{false,50});
    Processor p;p.chromaticMapping=false;p.combinedTiming=false;p.setSwingLink(false,{});p.setSwing(false,50);auto one=std::make_shared<AudioData>();one->buffer.setSize(1,192000);one->buffer.clear();one->rate=48000;one->path=juce::File::getCurrentWorkingDirectory().getChildFile("work/one-120.wav").getFullPathName();
    if(!p.installAudio(one,false))return 701;p.sourceBpm=120;p.sourceBeatsPerBar=4;p.split(8,false);p.assignCategory({1},39);p.setEnvelope({1},{true,3,10,200});p.resetHistory();auto candidates=p.repeatCandidates({1});if(candidates!=std::set<int>({3,5,7}))return 702;
    p.assignCategory({5},40);if(p.repeatCandidates({1})!=std::set<int>({3,7}))return 703;if(p.repeatCandidates({1},39)!=std::set<int>({3,5,7}))return 1136;p.assignCategory({3,7},39);if(!p.undoRedo(false)||p.slices[3].note!=-1||p.slices[1].note!=39||p.slices[5].note!=40)return 704;
    p.moveBoundary(3,72000+3000,false);p.moveBoundary(7,168000+3000,false);if(p.repeatCandidates({1})!=std::set<int>({3,7}))return 705;
    auto two=std::make_shared<AudioData>();two->buffer.setSize(1,96000);two->buffer.clear();two->rate=48000;two->path=juce::File::getCurrentWorkingDirectory().getChildFile("work/two-120.wav").getFullPathName();
    auto before=p.historyState();if(!p.installAudio(two,true)||p.audio!=two||!p.undoRedo(false)||p.audio!=one||p.historyState()!=before||p.slices[1].envelope.attack!=3)return 706;
    if(!p.undoRedo(true)||p.audio!=two)return 707;
    p.setSwingLink(true,"B");p.setSwing(true,63);Processor other;other.chromaticMapping=false;other.combinedTiming=false;other.setSwingLink(true,"B");other.syncLinkedSwing();if(!other.swingEnabled||other.swingAmount!=63)return 708;other.setSwingLink(false,"B");p.setSwing(true,68);other.syncLinkedSwing();if(other.swingAmount!=63)return 709;
    Processor defaults;defaults.chromaticMapping=false;defaults.combinedTiming=false;if(!defaults.swingLinked||defaults.swingGroup!="A"||!std::isinf(defaults.getTailLengthSeconds()))return 710;
    CheckPlayHead host;host.position.setBpm(120);host.position.setPpqPosition(0);host.position.setIsPlaying(true);Processor first,second;for(auto* instance:{&first,&second}){instance->setSwingLink(false,{});instance->audio=one;instance->sourceBpm=120;instance->split(8);instance->setPlayMode(true);instance->prepareToPlay(48000,256);instance->setPlayHead(&host);}for(int n=0;n<one->buffer.getNumSamples();++n)one->buffer.setSample(0,n,.4f);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer notes;for(auto* instance:{&first,&second}){instance->processBlock(block,notes);if(block.getSample(0,220)<.35f)return 720;}
    std::cout<<"v0.7 checks passed: repeat suggestions/swung hits, category protection, sample-selection undo/redo, fixed swing groups and continuous processing declaration\n";return 0;
}

int runSliceGroupsV08Checks(){
    LibraryStore::writeSwing("A",{false,50});Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->path=juce::File::getCurrentWorkingDirectory().getChildFile("work/v08-loop-120.wav").getFullPathName();data->buffer.setSize(1,192000);for(int n=0;n<192000;++n)data->buffer.setSample(0,n,.4f);p.audio=data;p.sourceBpm=120;p.split(8,false);
    for(int i=0;i<8;++i)if(p.slices[i].note!=36+i||p.slices[i].category!=-1)return 801;if(p.padHasMembers(39))return 802;
    p.assignCategory({0,2},39);if(p.slices[0].note!=39||p.slices[2].note!=39||p.slices[1].note!=36||p.slices[3].note!=37||p.slices[4].note!=38||p.slices[5].note!=40)return 803;
    p.assignCategory({0,2},-1);for(int i=0;i<8;++i)if(p.slices[i].note!=36+i)return 804;
    p.assignCategory({0,2},39);p.setSliceGain({0},-3);p.setPadGain(39,2);p.setPlayMode(false);p.prepareToPlay(48000,256);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer notes;notes.addEvent(juce::MidiMessage::noteOn(1,39,(juce::uint8)127),0);p.processBlock(block,notes);if(std::abs(block.getSample(0,220)-.4*juce::Decibels::decibelsToGain(-1.))>.001)return 805;
    p.setSliceMuted(0,true);p.releaseResources();p.resetCycles();p.trigger(39,1);int playing=-1;for(auto& v:p.voices)if(v.active)playing=v.sliceIndex;if(playing!=2)return 806;
    auto midi=p.makeMidi(120);int hits=0;for(int i=0;i<midi.getTrack(0)->getNumEvents();++i)hits+=midi.getTrack(0)->getEventPointer(i)->message.isNoteOn();if(hits!=7)return 807;
    p.setSliceMuted(0,false);p.releaseResources();p.audition(0);notes.clear();p.processBlock(block,notes);if(std::abs(block.getSample(0,220)-.85*.4*juce::Decibels::decibelsToGain(-1.))>.001)return 827;
    p.releaseResources();CheckPlayHead gainHost;gainHost.position.setBpm(120);gainHost.position.setPpqPosition(0);gainHost.position.setIsPlaying(true);p.setPlayHead(&gainHost);p.setPlayMode(true);p.processBlock(block,notes);if(std::abs(block.getSample(0,220)-.4*juce::Decibels::decibelsToGain(-1.))>.001)return 828;p.setPlayHead(nullptr);p.setSliceMuted(0,true);
    auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||!restored.slices[0].muted||restored.slices[0].gainDb!=-3||restored.padGainDb[39]!=2||restored.slices[1].note!=36)return 808;
    p.setSwing(true,75);p.setRhythm(false,.25,.1);if(p.adjustedBeat(.30)!=.30)return 809;p.setRhythm(true,.25,.1);if(p.adjustedBeat(.30)!=.30)return 965;p.setRhythm(true,.25,.49);if(std::abs(p.adjustedBeat(.30)-.375)>1e-8)return 810;p.setSwing(true,50);if(std::abs(p.adjustedBeat(.30)-.25)>1e-8)return 811;
    p.setLoopBars(1);if(p.sourceBpm!=120||p.regionEnd()!=96000)return 812;p.setLoopLock(true);auto longer=std::make_shared<AudioData>(*data);longer->path=data->path+"-long";longer->buffer.setSize(1,384000);longer->buffer.clear();p.installAudio(longer,true);if(p.regionEnd()!=96000||p.lockedBars!=1)return 813;auto shorter=std::make_shared<AudioData>(*data);shorter->path=data->path+"-short";shorter->buffer.setSize(1,48000);p.installAudio(shorter,true);if(p.regionEnd()!=96000||p.lockedBars!=1)return 814;
    p.audio=data;p.setTrim(0,192000);p.sourceBpm=120;p.split(8,false);p.assignCategory({0},41);if(p.repeatCandidates({0},41)!=std::set<int>({1,2,3,4,5,6,7}))return 815;
    p.resetHistory();p.setSliceGain({0},-8);p.setPadGain(41,4);if(!p.undoRedo(false)||p.padGainDb[41]!=0||!p.undoRedo(false)||p.slices[0].gainDb!=0)return 816;
    std::cout<<"v0.8 checks passed: chromatic note reservations/release, additive gains, slice mute/round robin/MIDI, state recall, combined quantize/swing, locked loop sizes and hat suggestions\n";return 0;
}

int runSliceGroupsV081Checks(){AudioData data;data.buffer.setSize(2,19031);juce::Random random(91);for(int n=0;n<data.buffer.getNumSamples();++n)for(int ch=0;ch<2;++ch)data.buffer.setSample(ch,n,random.nextFloat()*2-1);data.buildWaveformPeaks();for(int i=0;i<500;++i){int a=random.nextInt(19031),b=juce::jmin(19031,a+random.nextInt(5000));float low=0,high=0;for(int n=a;n<b;++n)for(int ch=0;ch<2;++ch){auto v=data.buffer.getSample(ch,n);low=juce::jmin(low,v);high=juce::jmax(high,v);}auto result=data.waveformRange(a,b);if(result.low!=low||result.high!=high)return 909;}std::cout<<"v0.8.1 signed waveform range checks passed\n";return 0;}

int runSliceGroupsV082Checks(){
    Processor p;p.setSwingLink(false,{});p.prepareToPlay(48000,256);
    auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,192000);
    for(int n=0;n<192000;++n)data->buffer.setSample(0,n,.4f*std::sin(n*.02));
    p.audio=data;p.sourceBpm=120;p.split(4,false);p.setPlayMode(true);p.setRhythm(true,.25,.49);
    p.setEnvelope({0},{true,0,0,3000});
    CheckPlayHead host;host.position.setBpm(120);host.position.setIsPlaying(true);p.setPlayHead(&host);
    juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
    double previousAge=0;
    // Continuous knob changes must preserve the active voice's age and source position.
    for(int b=0;b<120;++b){
        host.position.setPpqPosition(b*256.*2/48000);
        if(b){p.setEnvelopeField({0},3,500+b*10);p.setSwing(true,50+(b%26));}
        p.processBlock(block,midi);
        int active=0;for(auto& v:p.voices)if(v.active&&v.automatic){
            ++active;if(v.age!=previousAge+256||std::abs(v.pos-v.age)>1e-6)return 920;
            previousAge=v.age;if(b&&v.envelope.decay!=3000)return 921;
        }
        if(active!=1)return 922;
    }
    // The following trigger picks up the latest edited envelope.
    p.setPlayMode(false);p.releaseResources();midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)127),0);
    p.processBlock(block,midi);bool found=false;for(auto& v:p.voices)if(v.active){found=true;if(v.envelope.decay!=1690)return 923;}
    if(!found)return 924;
    p.setPlayMode(true);p.split(32,false);p.setEnvelope({0},{false,0,0,150});
    std::array<double,32> ages{};double cycle=-1;int triggered=0;
    for(int b=0;b<900;++b){
        double ppq=b*256.*2/48000,currentCycle=std::floor(ppq/8);
        if(cycle!=currentCycle){ages.fill(0);cycle=currentCycle;}
        host.position.setPpqPosition(ppq);p.setSwing(true,50+(b%26));p.processBlock(block,midi);
        for(auto& v:p.voices)if(v.active&&v.automatic){
            if(v.age<ages[v.sliceIndex])return 925;
            if(ages[v.sliceIndex]==0)++triggered;ages[v.sliceIndex]=v.age;
        }
    }
    if(triggered<20)return 926;
    std::cout<<"v0.8.2 continuous Decay/Swing playback and next-trigger envelope checks passed\n";return 0;
}

int runSliceGroupsV083Checks(){
    auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/preset-check");folder.createDirectory();
    auto file=folder.getChildFile("source.wav");juce::AudioBuffer<float> buffer(1,48000);buffer.clear();
    juce::WavAudioFormat wav;auto stream=file.createOutputStream();stream->setPosition(0);stream->truncate();
    std::unique_ptr<juce::OutputStream> output=std::move(stream);
    {auto writer=wav.createWriterFor(output,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));if(!writer||!writer->writeFromAudioSampleBuffer(buffer,0,48000))return 930;}
    Processor original;original.setSwingLink(false,{});if(!original.load(file,false))return 931;
    original.split(8,false);original.assignCategory({1,3,5},39);original.setEnvelope({1,3,5},{true,2,10,230});
    original.setSliceGain({3},-3);original.setPadGain(39,2);original.setSliceMuted(2,true);original.setPadName(39,"Clap");
    original.setRhythm(true,.125,.49);original.setSwing(true,65);original.setTrim(100,47000);original.setStart(1000);original.setLoopLock(true);original.setPlayMode(true);
    auto expected=original.configurationXml()->toString();
    if(!original.savePreset("Zulu test",true)||!original.savePreset("Alpha test",true)||original.savePreset("ALPHA TEST"))return 932;
    auto preset=LibraryStore::findPreset("alpha TEST");auto xml=juce::XmlDocument::parse(preset);if(!xml||LibraryStore::presetName(preset)!="Alpha test")return 933;
    Processor restored;auto next=restored.readAudio(file);if(!restored.installPreset(next,*xml)||restored.configurationXml()->toString()!=expected||restored.swingLinked||!restored.loopLengthLocked)return 934;
    restored.setSliceGain({3},-12);if(!restored.undoRedo(false)||restored.slices[3].gainDb!=-3)return 935;
    auto before=restored.configurationXml()->toString();xml->setAttribute("samples",100);
    if(restored.installPreset(next,*xml)||restored.configurationXml()->toString()!=before)return 936;
    xml=juce::XmlDocument::parse(preset);if(restored.installPreset(next,*xml,restored.loadGeneration+1))return 937;
    original.setSliceGain({3},-7);if(!original.savePreset("Alpha test",true))return 938;
    xml=juce::XmlDocument::parse(preset);if(!restored.installPreset(next,*xml)||restored.slices[3].gainDb!=-7)return 939;
    std::cout<<"v0.8.3 named preset storage/overwrite, exact loop recall, undo and invalid/stale load checks passed\n";return 0;
}

int runSliceGroupsV084Checks(){
    Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);
    for(int n=0;n<96000;++n)data->buffer.setSample(0,n,n<48000?.3f:.1f);p.audio=data;p.sourceBpm=120;p.split(4,false);
    p.moveBoundary(1,40000,false); // first slice is longer than each destination
    p.setEnvelope({0},{true,0,0,2000});p.setSliceGain({0},-6);p.assignCategory({2},39);
    p.replaceSlices({1,2},0);int note=p.slices[2].note;auto xml=p.configurationXml();
    Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||restored.slices[2].replacement!=0)return 950;
    p.prepareToPlay(48000,256);p.startVoice(p.slices[2],1);bool found=false;
    for(auto& v:p.voices)if(v.active){found=true;if(v.start!=0||v.end!=40000||v.note!=note||v.destinationIndex!=2||v.envelope.decay!=2000||!v.fullTail)return 951;}
    if(!found)return 952;
    p.releaseResources();auto continuation=p.slices[2];continuation.start+=2000;p.startVoice(continuation,1);
    for(auto& v:p.voices)if(v.active&&v.pos!=2000)return 969;
    p.releaseResources();p.setPlayMode(true);CheckPlayHead host;host.position.setBpm(120);host.position.setIsPlaying(true);p.setPlayHead(&host);
    juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
    for(int b=0;b<270;++b){host.position.setPpqPosition(b*256.*2/48000);p.processBlock(block,midi);}
    bool tail=false,later=false;for(auto& v:p.voices)if(v.active){if(v.destinationIndex==1&&v.fullTail)tail=true;if(v.destinationIndex==2)later=true;}
    if(!tail||!later)return 953;
    p.replaceSlices({3},2);if(p.slices[3].replacement!=0)return 1401;p.replaceSlices({3},-1);p.clearVoices();p.replaceSlices({0},2);p.startVoice(p.slices[1],1);
    for(auto& v:p.voices)if(v.active&&v.start!=0)return 954; // direct source, no replacement chains
    p.addBoundary(1000);if(p.slices[3].replacement!=0)return 955;p.removeBoundary(1);if(p.slices[2].replacement!=0)return 956;
    p.replaceSlices({2},-1);if(p.slices[2].replacement!=-1)return 957;
    p.setRhythm(true,.25,0);if(std::abs(p.adjustedBeat(.28,false)-.28)>1e-7)return 958;
    p.setRhythm(true,.25,.49);p.setSwing(false,50);if(std::abs(p.adjustedBeat(.28,false)-.25)>1e-7)return 959;
    {Processor q(false);q.audio=data;q.sourceBpm=120;q.independentGrid=q.regionTiming=true;q.gridStart=0;q.setTrim(24000,72000);q.setStart(48000);q.setLoopLock(true);auto next=std::make_shared<AudioData>(*data);next->path="/tmp/120_lock_test.wav";if(!q.installAudio(next,false))return 1403;double spb=q.audio->rate*60/q.sourceBpm;if(std::abs((q.regionStart()-q.gridOrigin())/spb-1)>0.001||std::abs(q.loopBeats()-2)>0.001||std::abs((q.startMarker-q.regionStart())/spb-1)>0.001)return 1404;}
    {Processor q(false);q.audio=data;q.sourceBpm=120;q.independentGrid=q.regionTiming=true;q.gridStart=0;q.setTrim(24000,72000);q.setLoopLock(true);q.preferredSliceMethod=1;q.importDetectStrength=65;q.divisions=64;auto next=std::make_shared<AudioData>(*data);next->path="/tmp/120_locked_detection_test.wav";if(!q.installAudio(next,true))return 1406;auto points=Processor::detectedPoints(*next,0,next->buffer.getNumSamples(),65,q.sourceBpm,q.gridOrigin(),q.rhythmGrid);if(q.slices.size()+1!=points.size())return 1407;for(int n=0;n<(int)q.slices.size();++n)if(q.slices[n].start!=points[n]||q.slices[n].end!=points[n+1])return 1408;auto before=q.slices;q.detect(90);for(auto slice:before)if(slice.end<=q.regionStart()||slice.start>=q.regionEnd()){bool kept=false;for(auto current:q.slices)if(current.start==slice.start&&current.end==slice.end)kept=true;if(!kept)return 1409;}}
    std::cout<<"v0.8.4 replacement persistence/source sound/destination MIDI/overlapping tails/direct references/boundary edits and Window checks passed\n";return 0;
}

int runSliceGroupsV085Checks(){
    Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);data->buffer.clear();p.audio=data;p.split(4,false);
    juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
    for(double semitones:{12.,-12.,0.}){p.releaseResources();p.setPitch(semitones);p.prepareToPlay(48000,256);p.startVoice(p.slices[0],1);p.processBlock(block,midi);for(auto& v:p.voices)if(v.active&&std::abs(v.pos-256*std::pow(2.,semitones/12))>1e-6)return 970;}
    p.setPitch(7.25);auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||restored.pitchSemitones!=7.25)return 971;
    p.prepareToPlay(48000,256);p.startVoice(p.slices[0],1);double initial=p.currentPitchRatio;p.setPitch(-12);p.processBlock(block,midi);
    if(!(p.currentPitchRatio<initial&&p.currentPitchRatio>.5))return 972;
    p.setSourceBpm(120);p.beginEdit();for(int i=0;i<100;++i)p.setSourceBpm(std::round((p.sourceBpm+.01)*100)/100);p.endEdit();if(p.sourceBpm!=121||!p.undoRedo(false)||p.sourceBpm!=120)return 973;
    std::cout<<"v0.8.5 global pitch ratios/smoothing/recall and fine BPM nudge gesture undo checks passed\n";return 0;
}

int runSliceGroupsV086Checks(){
    Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(2,48000);
    for(int n=0;n<48000;++n){data->buffer.setSample(0,n,n<12000?.25f:.5f);data->buffer.setSample(1,n,n<12000?.125f:.25f);}data->buildWaveformPeaks();
    p.audio=data;p.sourceBpm=120;p.split(4,false);p.setNormalise(true);p.prepareToPlay(48000,256);p.startVoice(p.slices[0],1);
    juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;p.processBlock(block,midi);
    if(std::abs(block.getSample(0,220)-.5f)>1e-5||std::abs(block.getSample(1,220)-.25f)>1e-5||p.slices[0].gainDb!=0||p.slices[1].gainDb!=0)return 980;
    p.setNormalise(false);p.processBlock(block,midi);if(!(p.currentNormaliseGain>1&&p.currentNormaliseGain<2))return 981;p.setNormalise(true);
    p.setLoopBars(2);if(p.regionEnd()!=192000||p.loopBeats()!=8)return 982;
    auto exported=p.makeMidi(120);if(exported.getLastTimestamp()!=7680)return 983;
    for(int t=0;t<exported.getNumTracks();++t)for(int i=0;i<exported.getTrack(t)->getNumEvents();++i){auto message=exported.getTrack(t)->getEventPointer(i)->message;if(message.isNoteOn()&&message.getTimeStamp()>=1920)return 984;}
    p.split(16,false);if(p.slices.back().end!=48000)return 985;p.detect(100);if(p.slices.back().end!=48000)return 986;
    auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||restored.regionEnd()!=192000||!restored.normaliseEnabled)return 987;
    restored.prepareToPlay(48000,256);restored.setPlayMode(true);CheckPlayHead host;host.position.setBpm(120);host.position.setIsPlaying(true);restored.setPlayHead(&host);host.position.setPpqPosition(3);restored.processBlock(block,midi);host.position.setPpqPosition(3+256.*2/48000);restored.processBlock(block,midi);
    if(block.getMagnitude(0,256)!=0)return 988;
    restored.setLoopLock(true);auto shortData=std::make_shared<AudioData>(*data);shortData->path="/preset-padding-check.wav";shortData->buffer.setSize(2,24000);restored.installAudio(shortData,true);
    if(restored.regionEnd()!=192000)return 989;
    auto silent=std::make_shared<AudioData>();silent->buffer.setSize(1,100);silent->buffer.clear();silent->buildWaveformPeaks();restored.audio=silent;restored.setNormalise(true);if(restored.normaliseGain()!=1)return 990;
    std::cout<<"v0.8.6 whole-file linked stereo normalization/smoothing, padded loop slicing/recall/silence/MIDI length and locked short imports passed\n";return 0;
}

int runSliceGroupsV087Checks(){
    Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);data->buffer.clear();p.audio=data;p.sourceBpm=120;
    p.slices={{0,6000,36},{6000,12300,37},{12300,30000,38},{30000,96000,39}};
    p.setTrim(6000,90000);p.setRegionBars(1);if(p.regionStart()!=6000||p.regionEnd()!=90000||p.loopBeats()!=4||std::abs(p.sourceBpm-137.142857142857)>1e-7)return 1000;
    p.setStart(20000,false);p.setRhythm(true,.25,.49);p.setSwing(false,50);p.rebuildTimeline();
    bool found=false;for(int i=0;i<p.eventCount;++i)if(p.events[i].start==12300){found=true;double absolute=std::fmod(p.events[i].beat+(20000-6000)*4./84000,4.);if(std::abs(absolute-.25)>1e-7)return 1001;}
    if(!found)return 1002;
    p.setStart(21000,false);p.rebuildTimeline();for(int i=0;i<p.eventCount;++i)if(p.events[i].start==12300&&std::abs(std::fmod(p.events[i].beat+(21000-6000)*4./84000,4.)-.25)>1e-7)return 1003;
    p.setRegionBars(2);if(p.regionEnd()!=90000||p.loopBeats()!=8)return 1004;
    p.setRegionBars(1);p.setSourceBpm(120);if(p.regionEnd()!=102000||p.loopBeats()!=4)return 1005;
    if(p.makeMidi(97).getLastTimestamp()!=3840)return 1006;
    auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||!restored.regionTiming||restored.regionEnd()!=102000||restored.regionBeatCount!=4)return 1007;
    restored.setTrim(6000,126000);if(restored.loopBeats()!=4||restored.sourceBpm!=96)return 1008;
    p.setLoopBars(.5);if(p.loopBeats()!=2||p.regionEnd()!=54000)return 1009;p.setLoopBars(1);if(p.regionEnd()!=102000)return 1010;
    std::cout<<"v0.8.7 independent region bars/derived BPM, IN-anchored quantize with rotated START, padding via BPM and region state/MIDI checks passed\n";return 0;
}

int runSliceGroupsV089Checks(){
    juce::ScopedJuceInitialiser_GUI init;
    Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);data->buffer.clear();p.audio=data;p.sourceBpm=120;p.split(16,false);p.setTrim(6000,90000);p.useIndependentGrid();p.resetHistory();
    p.beginEdit();p.alignGridBeat(4,102000);p.endEdit();if(p.regionStart()!=6000||p.regionEnd()!=102000||p.sourceBpm!=120||p.loopBeats()!=4)return 1040;
    p.beginEdit();p.alignGridBeat(4,90000);p.endEdit();if(p.regionEnd()!=90000||std::abs(p.sourceBpm-137.142857142857)>1e-7||std::abs(p.loopBeats()-4)>1e-8)return 1041;
    if(!p.undoRedo(false)||p.sourceBpm!=120||p.regionEnd()!=102000)return 1042;if(!p.undoRedo(true)||std::abs(p.loopBeats()-4)>1e-8)return 1043;
    auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||!restored.independentGrid||std::abs(restored.sourceBpm-p.sourceBpm)>1e-8)return 1044;
    p.setTrim(6000,111000);if(std::abs(p.sourceBpm-137.142857142857)>1e-7||std::abs(p.loopBeats()-5)>1e-8)return 1045;
    if(p.snapPosition(90200)!=90000||p.snapPosition(88000)!=90000)return 1046;
    p.setRegionBars(1);if(p.regionEnd()!=90000||std::abs(p.loopBeats()-4)>1e-8)return 1047;
    p.setSourceBpm(120);if(p.regionEnd()!=102000||p.loopBeats()!=4)return 1048;
    p.setRegionBars(2);if(p.regionEnd()!=198000||p.makeMidi(97).getLastTimestamp()!=7680)return 1049;
    p.slices={{0,6000,36},{6000,12300,37},{12300,30000,38},{30000,96000,39}};p.setTrim(6000,102000);p.setStart(20000,false);p.setRhythm(true,.25,.49);p.setSwing(false,50);p.rebuildTimeline();
    bool found=false;for(int i=0;i<p.eventCount;++i)if(p.events[i].start==12300){found=true;double absolute=std::fmod(p.events[i].beat+(20000-6000)/24000.,4.);if(std::abs(absolute-.25)>1e-7)return 1050;}if(!found)return 1051;
    Waveform wave(p);wave.setSize(1000,240);auto update=[&]{wave.state.audio=data;wave.state.slices=p.slices;wave.state.trimStart=p.regionStart();wave.state.trimEnd=p.regionEnd();wave.state.startMarker=p.startMarker;wave.state.bpm=p.sourceBpm;};update();wave.changed=update;
    auto event=[&](float x,float y,bool command){auto mods=juce::ModifierKeys::leftButtonModifier|(command?juce::ModifierKeys::commandModifier:0);return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,y},mods,1,0,0,0,0,&wave,&wave,juce::Time::getCurrentTime(),{x,y},juce::Time::getCurrentTime(),1,true);};
    p.setTrim(6000,110000);update();float line=(float)wave.sampleToX(102000);int out=p.regionEnd();wave.mouseDown(event(line+5,10,false));wave.mouseDrag(event(line+5-100,10,true));wave.mouseUp(event(line+5-100,10,true));if(p.regionEnd()!=91000||p.loopBeats()!=4||p.sourceBpm<=120)return 1052;out=p.regionEnd();
    float outX=(float)wave.sampleToX(p.regionEnd());double bpm=p.sourceBpm;wave.mouseDown(event(outX,25,true));wave.mouseDrag(event(outX-100,25,true));wave.mouseUp(event(outX-100,25,true));if(p.regionEnd()!=out-9600||p.sourceBpm!=bpm)return 1053;
    int start=p.regionStart();float inX=(float)wave.sampleToX(start);wave.mouseDown(event(inX,25,true));wave.mouseDrag(event(inX+100,25,true));wave.mouseUp(event(inX+100,25,true));if(p.regionStart()!=start+9600||p.sourceBpm!=bpm)return 1054;
    int marker=p.startMarker;float markerX=(float)wave.sampleToX(marker);int extent=wave.displayLength();wave.mouseDown(event(markerX,34,true));wave.mouseDrag(event(markerX+100,34,true));wave.mouseUp(event(markerX+100,34,true));if(p.startMarker!=marker+(int)std::round(extent*.1)||p.sourceBpm!=bpm)return 1055;
    std::cout<<"v0.8.9 independent grid drag/BPM, marker snapping/fine movement, bars resizing, padding, quantize, MIDI, undo and state checks passed\n";return 0;
}

int runSliceGroupsV0810Checks(){
    Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);for(int n=0;n<96000;++n)data->buffer.setSample(0,n,.1f*(n/24000+1));p.audio=data;p.sourceBpm=120;p.split(4,false);p.useIndependentGrid();p.resetHistory();
    p.beginEdit();p.alignGridBeat(4,90123,4);p.alignGridBeat(4,91357,4);p.endEdit();if(p.regionEnd()!=91357||p.loopBeats()!=4)return 1060;
    if(!p.undoRedo(false)||p.regionEnd()!=96000||p.sourceBpm!=120)return 1061;if(!p.undoRedo(true)||p.regionEnd()!=91357||p.loopBeats()!=4)return 1062;
    if(p.snapPosition(91356,1)!=91357||p.makeMidi(120).getLastTimestamp()!=3840)return 1063;
    p.alignGridBeat(4,100003,8);if(p.regionEnd()!=200006||p.loopBeats()!=8)return 1064;
    auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||restored.loopBeats()!=8||restored.regionEnd()!=200006)return 1065;
    p.setSourceBpm(121);if(p.loopBeats()!=8||p.regionEnd()!=190413)return 1067;
    p.alignGridBeat(4,90123,4);p.setStart(0,false);p.setRhythm(true,.25,.49);p.setSwing(false,50);p.setPlayMode(true);p.prepareToPlay(48000,256);
    CheckPlayHead host;host.position.setBpm(120);host.position.setIsPlaying(true);p.setPlayHead(&host);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;
    for(int frame=0;frame<384000;frame+=256){host.position.setPpqPosition(frame/24000.);p.processBlock(block,midi);if(frame%96000==0){bool restarted=false;for(auto& v:p.voices)if(v.active&&v.automatic&&v.destinationIndex==0&&v.age==256)restarted=true;if(!restarted||std::abs(block.getSample(0,220)-.1f)>.001)return 1066;}}
    p.setPlayHead(nullptr);std::cout<<"v0.8.10 grid-following OUT, exact musical cycle length, padded snapping, MIDI, undo/recall and continuous host playback checks passed\n";return 0;
}

int runSliceGroupsV0811Checks(){
    juce::ScopedJuceInitialiser_GUI init;Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);data->buffer.clear();p.audio=data;p.sourceBpm=120;p.split(4,false);p.useIndependentGrid();p.setStart(0,false);p.setRhythm(true,.25,.49);p.setSwing(false,50);
    auto at=[&](int sample){p.rebuildTimeline();for(int n=0;n<p.eventCount;++n)if(p.events[n].start==sample)return p.events[n].beat;return -1.;};
    p.slices={{0,23700,36},{23700,24240,37},{24240,48000,38},{48000,96000,39}};p.setQuantizeEarly(true);
    if(std::abs(at(23700)-.9875)>1e-8||at(24240)!=1)return 1070;
    auto unique=[&]{p.rebuildTimeline();for(int i=0;i<p.eventCount;++i)for(int j=0;j<i;++j)if(std::abs(p.events[i].beat-p.events[j].beat)<1e-7)return false;return true;};if(!unique()||p.eventCount!=4)return 1071;
    p.setQuantizeAmount(.5);if(std::abs(at(24240)-1.005)>1e-8||!unique())return 1072;p.setQuantizeAmount(0);if(std::abs(at(24240)-1.01)>1e-8)return 1073;p.setQuantizeAmount(1);
    p.slices={{0,23700,36},{23700,48000,37},{48000,96000,38}};if(at(23700)!=1)return 1074;p.setQuantizeEarly(false);if(std::abs(at(23700)-.9875)>1e-8)return 1075;
    p.setQuantizeEarly(true);p.slices={{0,23700,36},{23700,24000,37},{24000,24240,38},{24240,96000,39}};if(at(24000)!=1||std::abs(at(23700)-.9875)>1e-8||std::abs(at(24240)-1.01)>1e-8||!unique())return 1076;
    p.slices={{0,5760,36},{5760,6240,37},{6240,9000,38},{9000,96000,39}};p.setSwing(false,75);if(!unique())return 1077;
    p.setSwing(false,50);p.slices={{0,23700,36},{23700,24240,37},{24240,95760,38},{95760,96000,39}};if(!unique()||std::abs(at(95760)-3.99)>1e-8)return 1078;
    auto midi=p.makeMidi(120);std::vector<double> notes;for(int i=0;i<midi.getTrack(0)->getNumEvents();++i){auto m=midi.getTrack(0)->getEventPointer(i)->message;if(m.isNoteOn())notes.push_back(m.getTimeStamp());}if(notes.size()!=4||std::adjacent_find(notes.begin(),notes.end())!=notes.end())return 1079;
    p.resetHistory();p.beginEdit();p.setQuantizeAmount(.42);p.setGridStart(4800);p.endEdit();auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||restored.gridOrigin()!=4800||restored.quantizeAmount!=.42||!restored.quantizeEarly||restored.loopBeats()!=4)return 1080;
    if(!p.undoRedo(false)||p.gridOrigin()!=0||p.quantizeAmount!=1)return 1081;if(!p.undoRedo(true)||p.gridOrigin()!=4800||p.quantizeAmount!=.42)return 1082;
    p.setRhythm(false,.25,.49);p.setGridStart(0);p.setStart(0,true);data->transients={4800,24000,48000};Waveform wave(p);wave.setSize(1000,240);auto update=[&]{wave.state.audio=data;wave.state.slices=p.slices;wave.state.trimStart=p.regionStart();wave.state.trimEnd=p.regionEnd();wave.state.gridStart=p.gridOrigin();wave.state.startMarker=p.startMarker;wave.state.bpm=p.sourceBpm;};update();wave.changed=update;
    auto event=[&](float x,float y,bool command=false,bool shift=false){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,y},juce::ModifierKeys::leftButtonModifier|(command?juce::ModifierKeys::commandModifier:0)|(shift?juce::ModifierKeys::shiftModifier:0),1,0,0,0,0,&wave,&wave,juce::Time::getCurrentTime(),{x,y},juce::Time::getCurrentTime(),1,true);};
    wave.mouseDown(event(5,10));wave.mouseDrag(event(105,10));wave.mouseUp(event(105,10));if(p.gridOrigin()!=0)return 1097;wave.mouseDown(event(5,10,true));wave.mouseDrag(event(105,10,true));wave.mouseUp(event(105,10,true));if(p.gridOrigin()!=9600||p.regionStart()!=9600||p.regionEnd()!=105600||p.loopBeats()!=4)return 1083;
    p.gridSnapMode=2;float origin=(float)wave.sampleToX(p.gridOrigin());wave.mouseDown(event(origin,10));wave.mouseDrag(event(origin+70,10));wave.mouseUp(event(origin+70,10));if(p.gridOrigin()!=24000||p.regionStart()!=24000||p.regionEnd()!=120000)return 1084;
    p.setGridSnap(1);origin=(float)wave.sampleToX(p.gridOrigin());wave.mouseDown(event(origin,10));wave.mouseDrag(event(origin+210,10));wave.mouseUp(event(origin+210,10));if(p.gridOrigin()!=48000||p.loopBeats()!=4)return 1098;
    p.setGridStart(0);update();float barTwo=(float)wave.sampleToX(96000);wave.mouseDown(event(barTwo,10));wave.mouseDrag(event(barTwo+260,10));wave.mouseUp(event(barTwo+260,10));if(p.sourceBpm!=96||p.regionEnd()!=120000||p.loopBeats()!=4)return 1099;
    p.setSourceBpm(120);p.setGridStart(24000);p.setGridSnap(2);update();origin=(float)wave.sampleToX(p.gridOrigin());wave.mouseDown(event(origin,10,true));wave.mouseDrag(event(origin+100,10,true));wave.mouseUp(event(origin+100,10,true));if(p.gridOrigin()!=36000)return 1100;p.setGridStart(24000);update();
    p.setGridStart(0);p.setSourceBpm(120);p.setGridSnap(1);update();float insideBeat=(float)wave.sampleToX(96000)+80;wave.mouseDown(event(insideBeat,10));wave.mouseDrag(event(insideBeat+260,10));wave.mouseUp(event(insideBeat+260,10));if(p.sourceBpm!=96||p.loopBeats()!=4)return 1117;p.setSourceBpm(120);p.setGridStart(24000);update();
    auto oldSnap=p.configurationXml();oldSnap->setAttribute("gridSnapMode",0);if(!restored.applyConfiguration(*oldSnap,false)||restored.gridSnapMode!=0)return 1101;
    p.snapMode=0;float out=(float)wave.sampleToX(p.regionEnd());wave.mouseDown(event(out,25));wave.mouseDrag(event(out-130,25));wave.mouseUp(event(out-130,25));if(p.regionEnd()!=96000||p.loopBeats()!=3)return 1085;
    out=(float)wave.sampleToX(p.regionEnd());wave.mouseDown(event(out,25,true));wave.mouseDrag(event(out+20,25,true));wave.mouseUp(event(out+20,25,true));if(p.regionEnd()!=97920)return 1086;
    bool muted=p.slices[2].muted;float a=(float)wave.sampleToX(p.slices[2].start),b=(float)wave.sampleToX(p.slices[2].end);wave.mouseDown(event((a+b)*.5f,90,true,true));if(p.slices[2].muted==muted)return 1087;
    std::cout<<"v0.8.11 early/amount quantize, collision protection across swing/wrap/MIDI, grid-origin/free/transient gestures, default loop snapping/Command override and recall/undo checks passed\n";return 0;
}

int runSliceGroupsV0815Checks(){
    Processor p;p.setSwingLink(false,{});auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,192000);data->buffer.clear();for(int n=96060;n<192000;++n)data->buffer.setSample(0,n,.4f);p.audio=data;p.sourceBpm=120;p.slices={{0,48000,36},{48000,96060,37},{96060,120000,38},{120000,144000,39},{144000,168000,40},{168000,192000,41}};p.useIndependentGrid();p.setTrim(96012,192012);p.setStart(96012,false);p.setRhythm(true,.25,.49);p.setSwing(false,50);p.rebuildTimeline();
    bool first=false;for(int i=0;i<p.eventCount;++i)if(p.events[i].start==96060){first=true;if(std::abs(p.events[i].beat-.002)>1e-8)return 1110;}if(!first)return 1111;
    auto midi=p.makeMidi(120);bool midiFirst=false;for(int i=0;i<midi.getTrack(0)->getNumEvents();++i){auto m=midi.getTrack(0)->getEventPointer(i)->message;if(m.isNoteOn()&&m.getNoteNumber()==38){midiFirst=true;if(std::abs(m.getTimeStamp()-1.92)>1e-7)return 1112;}}if(!midiFirst)return 1113;
    p.setPlayMode(true);p.prepareToPlay(48000,96);CheckPlayHead host;host.position.setBpm(120);host.position.setIsPlaying(true);p.setPlayHead(&host);juce::AudioBuffer<float> block(2,96);juce::MidiBuffer incoming;
    for(int frame=0;frame<288000;frame+=96){host.position.setPpqPosition(frame/24000.);p.processBlock(block,incoming);if(frame%96000==0&&std::abs(block.getSample(0,88)-.4f)>.001)return 1114;}
    p.setPlayHead(nullptr);p.setTrim(96060,192060);p.setStart(96060,false);p.rebuildTimeline();if(p.eventCount==0||p.events[0].start!=96060||p.events[0].beat!=0)return 1115;
    p.setRhythm(false,.25,.49);p.rebuildTimeline();if(p.events[0].beat!=0)return 1116;
    std::cout<<"v0.8.15 off-grid START/IN first-hit protection, trimmed continuation, MIDI and continuous offset-loop playback checks passed\n";return 0;
}

int runSliceGroupsV0818Checks(){
    Processor p;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(2,480000);data->buffer.clear();
    for(int hit=0;hit<100;++hit){int start=2000+hit*4500;float level=.012f+(hit%11)*.035f;for(int n=0;n<1200;++n){float value=level*std::exp(-n/230.f)*std::sin(n*.41f);data->buffer.setSample(0,start+n,value);data->buffer.setSample(1,start+n,value*.7f);}}
    p.audio=data;p.useIndependentGrid();p.setTrim(0,480000);
    std::set<int> previous;std::vector<std::set<int>> sets;
    for(int strength=0;strength<=100;++strength){p.detect(strength);std::set<int> current;for(auto slice:p.slices)current.insert(slice.start);for(int point:previous)if(!current.count(point))return 1118;sets.push_back(current);previous=current;}
    if(sets.back().size()<=sets.front().size())return 1119;
    data->transients=Processor::detectedPoints(*data,0,480000,100);for(int point:sets.back())if(p.snapPosition(point+1,2)!=point)return 1121;
    p.setGridSnap(0);auto freeState=p.configurationXml();Processor freeRecall;freeRecall.audio=data;if(!freeRecall.applyConfiguration(*freeState,false)||freeRecall.gridSnapMode!=0)return 1122;
    for(int strength=100;strength>=0;--strength){p.detect(strength);std::set<int> current;for(auto slice:p.slices)current.insert(slice.start);if(current!=sets[strength])return 1120;}
    std::cout<<"v0.8.18 nested sensitivity and repeatable marker positions passed\n";return 0;
}

int runSliceGroupsV0820Checks(){
    Processor p;p.setSwing(false,50);p.setRhythm(true,.25,.75);
    if(p.adjustedBeat(.17)!=0||p.adjustedBeat(.20)!=.20)return 1123;
    p.setQuantizeEarly(true);if(p.adjustedBeat(.20)!=.25||p.adjustedBeat(.1875)!=.25)return 1124;
    for(int n=1;n<1000;++n){double beat=n/4000.;if(!p.quantizeCandidate(beat))return 1125;}
    p.setQuantizeEarly(false);p.setRhythm(true,.25,.99);if(p.adjustedBeat(.245)!=0||p.adjustedBeat(.249)!=.249)return 1126;
    {AudioData onset;onset.rate=48000;onset.buffer.setSize(2,4800);for(int n=0;n<4800;++n){float value=.004f; if(n>=2400)value+=.03f+juce::jmin(.3f,(n-2400)*.0005f);onset.buffer.setSample(0,n,value);onset.buffer.setSample(1,n,value*.7f);}int point=Processor::refineTransientStart(onset,2496,0,4800);if(point<2380||point>2400)return 1137;int exact=Processor::refineTransientStart(onset,2400,0,4800);if(exact<2380||exact>2400)return 1138;}
    auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/v0820-fixture");folder.createDirectory();auto source=folder.getChildFile("original.wav"),moved=folder.getChildFile("renamed.wav");source.deleteFile();moved.deleteFile();
    juce::AudioBuffer<float> buffer(2,480000);for(int ch=0;ch<2;++ch)for(int n=0;n<buffer.getNumSamples();++n)buffer.setSample(ch,n,.2f*std::sin(n*.03f));
    juce::WavAudioFormat wav;{std::unique_ptr<juce::OutputStream> stream=source.createOutputStream();auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));if(!writer||!writer->writeFromAudioSampleBuffer(buffer,0,buffer.getNumSamples()))return 1127;}
    auto start=juce::Time::getMillisecondCounterHiRes();auto id=LibraryStore::fingerprint(source);double hashMs=juce::Time::getMillisecondCounterHiRes()-start;
    start=juce::Time::getMillisecondCounterHiRes();if(!p.load(source,false))return 1128;double loadMs=juce::Time::getMillisecondCounterHiRes()-start;
    p.setRhythm(true,.25,.75);p.setQuantizeEarly(true);p.setPadName(39,"Moved clap");if(!p.saveLoop()||!source.moveFileTo(moved))return 1129;
    Processor recalled;if(!recalled.load(moved)||!recalled.hasSavedConfig||recalled.lateWindow!=.75||!recalled.quantizeEarly||recalled.padNames[39]!="Moved clap"||recalled.getAudio()->contentHash!=id)return 1130;
    auto different=folder.getChildFile("different.wav");different.replaceWithText("different bytes");if(LibraryStore::savedLoop(different))return 1131;
    p.audio=recalled.audio;p.setStart(0,false);p.slices={{0,9600,36},{9600,16800,37},{16800,24000,38},{24000,480000,39}};p.setRhythm(true,.25,.75);p.setQuantizeEarly(true);p.rebuildTimeline();for(int i=0;i<p.eventCount;++i)for(int j=0;j<i;++j)if(std::abs(p.events[i].beat-p.events[j].beat)<1e-7)return 1132;
    std::cout<<"v0.8.20 crossover/no-gap/99%/collision and moved-file recall checks passed; 2.9 MB fingerprint "<<hashMs<<" ms, complete cold application load "<<loadMs<<" ms\n";return 0;
}

int runSliceGroupsV0821Checks(){
    Processor p;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,192000);data->buffer.clear();p.audio=data;p.sourceBpm=120;p.useIndependentGrid();
    p.slices={{0,12000,36},{12000,24000,37},{24000,48000,38},{48000,72000,39},{72000,96000,40},{96000,192000,41}};
    for(int i=0;i<5;++i){int start=p.slices[i].start;for(int n=0;n<juce::jmin(10000,p.slices[i].end-start);++n){float signal=i==0?std::sin(n*.008f):std::sin(n*1.8f);float decay=i==1?4000:600;data->buffer.setSample(0,start+n,signal*.4f*std::exp(-n/decay));}}
    p.slices[3].category=42;p.resetHistory();int count=p.autoCategorise();if(count<3||p.slices[0].category!=36||p.slices[1].category!=41||p.slices[2].category!=39||p.slices[3].category!=42||p.slices[5].category!=-1)return 1133;
    double ms=p.categoryAnalysisMs;if(!p.undoRedo(false)||p.slices[0].category!=-1||p.slices[3].category!=42)return 1134;if(!p.undoRedo(true)||p.slices[1].category!=41)return 1135;
    std::cout<<"v0.8.21 basic auto categories, silent hits, protected assignments and undo passed; analysis "<<ms<<" ms\n";return 0;
}

int runSliceGroupsV0824Checks(){
 Processor p;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,192000);data->buffer.clear();p.audio=data;p.sourceBpm=120;p.split(8,false);p.assignCategory({5},39);p.setEnvelope({5},{true,2,3,90});p.setSliceGain({5},-4);int start=p.slices[5].start;p.setTrim(0,96000);p.detect(80);auto found=std::find_if(p.slices.begin(),p.slices.end(),[&](const Slice& x){return x.start==start;});if(found==p.slices.end()||found->category!=39||found->gainDb!=-4||found->envelope.decay!=90)return 1139;
 p.setTrim(96000,192000);p.detect(90);if(p.slices.front().start!=0||p.slices.front().end!=96000)return 1140;
 p.categoryStateLocked=true;p.setPadMuted(39,true);p.setPadSolo(41,true);auto other=std::make_shared<AudioData>(*data);other->path="lock-fixture";if(!p.installAudio(other,false)||!p.padMuted[39]||!p.padSolo[41])return 1141;p.categoryStateLocked=false;if(!p.installAudio(data,false)||p.padMuted[39]||p.padSolo[41])return 1142;
 std::cout<<"v0.8.24 outside-region edit preservation and shared mute/solo lock checks passed\n";return 0;
}
int runSliceGroupsV0825Checks(){
 AudioData data;data.rate=48000;data.buffer.setSize(1,96000);data.buffer.clear();for(auto hit:std::vector<std::pair<int,float>>{{23300,.08f},{24000,.4f},{27600,.4f},{60000,.4f}})for(int n=0;n<400;++n)data.buffer.setSample(0,hit.first+n,hit.second);
 auto points=Processor::detectedPoints(data,0,96000,100,120,0,.25);auto near=[&](int target){return std::any_of(points.begin(),points.end(),[&](int point){return std::abs(point-target)<40;});};if(!near(24000)||!near(27600)||!near(60000)||near(23300))return 1143;
 std::set<int> previous;for(int sensitivity=0;sensitivity<=100;++sensitivity){auto detected=Processor::detectedPoints(data,0,96000,sensitivity,120,0,.25);std::set<int> current(detected.begin(),detected.end());for(int point:previous)if(!current.count(point))return 1144;previous=current;}
 std::cout<<"v0.8.25 grid-favoured cluster, separate strong hits and additive sensitivity checks passed\n";return 0;
}

int runSliceGroupsV0826Checks(){
 {Processor test;auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,192000);audio->buffer.clear();test.audio=audio;test.sourceBpm=120;test.split(16);if(test.slices.size()!=32||test.divisions!=16||test.slices[1].start!=6000)return 1164;test.setTrim(0,48000);test.split(16);int n=0;for(auto slice:test.slices)if(slice.start<48000)++n;if(n!=8||test.divisions!=16)return 1165;}

 Processor p;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);data->buffer.clear();p.audio=data;p.split(16,false);p.assignCategory({1},39);p.setEnvelope({1},{true,1,2,90});p.resetHistory();p.beginEdit();p.setGridSliceSwing(100);p.endEdit();if(p.slices[1].start!=9000||p.slices[2].start!=12000||p.slices.back().end!=96000||p.slices[1].category!=39||p.slices[1].envelope.decay!=90)return 1145;
 auto xml=p.configurationXml();Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,false)||restored.gridSliceSwing!=100||restored.slices[1].start!=9000)return 1146;if(!p.undoRedo(false)||p.gridSliceSwing!=0||p.slices[1].start!=6000)return 1147;if(!p.undoRedo(true)||p.slices[1].start!=9000)return 1148;p.split(8,false);if(p.slices[1].start!=18000||p.slices[2].start!=24000)return 1149;
 std::cout<<"v0.8.26 grid swing boundaries, preserved category/envelope, recall and undo checks passed\n";return 0;
}
int runSliceGroupsV0828Checks(){
 juce::ScopedJuceInitialiser_GUI init;Processor p;auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,96000);for(int n=0;n<96000;++n)data->buffer.setSample(0,n,.4f*std::sin(n*2*juce::MathConstants<double>::pi*440/48000));p.audio=data;p.sourceBpm=120;p.split(16,false);p.setStretchMode(true);p.setLoopKey(9);p.setTargetKey(5);p.setKeyLocked(true);if(p.keyTranspose()!=-4)return 1150;p.alternateKeyTranspose();if(p.keyTranspose()!=8)return 1151;p.setTargetKey(11);if(p.keyTranspose()!=2)return 1152;
 auto xml=p.configurationXml(true);Processor restored;restored.audio=data;if(!restored.applyConfiguration(*xml,true)||!restored.stretchMode||restored.loopKey!=9||restored.targetKey!=11||!restored.keyLocked)return 1153;
 auto frequency=[](const juce::AudioBuffer<float>& b){int crossings=0,begin=b.getNumSamples()/3,end=b.getNumSamples()*2/3;for(int n=begin+1;n<end;++n)if(b.getSample(0,n-1)<=0&&b.getSample(0,n)>0)++crossings;return crossings*48000./(end-begin);};
 auto started=juce::Time::getMillisecondCounterHiRes();auto result=StretchWorker::render({data,0,96000,0,120,160,12});if(!result||result->buffer.getNumSamples()!=72000||std::abs(frequency(result->buffer)-880)>4)return 1154;double renderMs=juce::Time::getMillisecondCounterHiRes()-started;
 auto straight=StretchWorker::render({data,0,96000,0,120,160,0});if(!straight||std::abs(frequency(straight->buffer)-440)>4)return 1155;
 p.setKeyLocked(false);p.prepareToPlay(48000,256);CheckPlayHead host;host.position.setIsPlaying(true);host.position.setBpm(160);host.position.setPpqPosition(1);p.setPlayHead(&host);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;p.processBlock(block,midi);for(int n=0;n<500&&p.stretchWorker.busy;++n)juce::Thread::sleep(5);p.processBlock(block,midi);if(block.getMagnitude(0,0,256)<.1f)return 1156;host.position.setIsPlaying(false);p.processBlock(block,midi);if(block.getMagnitude(0,0,256)>1e-5f)return 1157;
 ZoomBar zoom;zoom.setSize(400,12);double offset=-1,size=-1;zoom.changed=[&](double a,double b){offset=a;size=b;};zoom.setCurrentRange(.25,.25,juce::dontSendNotification);auto event=juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{50,6},juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::altModifier,1,0,0,0,0,&zoom,&zoom,juce::Time::getCurrentTime(),{50,6},juce::Time::getCurrentTime(),1,false);zoom.mouseDown(event);if(offset!=0||size!=1)return 1158;zoom.setCurrentRange(.5,.125,juce::dontSendNotification);zoom.mouseDoubleClick(event);if(offset!=0||size!=1)return 1159;
 std::cout<<"v0.8.28 stretch duration/pitch, key lock/alternate/recall, host play/stop and zoom resets passed; two-second render "<<renderMs<<" ms\n";return 0;
}

int runSliceGroupsV0831Checks(){
 for(auto item:std::vector<std::pair<juce::String,int>>{{"NPP_128_A_Radio_Synth.wav",9},{"NPP_128_F#_Chrd_Synth.wav",6},{"Loop_120_Bb_minor.wav",10},{"Synth_C#m.wav",1},{"Loop_Ebmajor.wav",3},{"a_random_bass_loop.wav",-1},{"Bass_Drop_Synth.wav",-1},{"Angel Ice 2 C3 - A3.wav",9},{"Bizarre D-Train Inc C3 - C3.wav",0},{"Euromast C3 - G#3.wav",8},{"Finite One C3 - D#3.wav",3},{"Sample Cm.wav",0},{"Sample C.wav",0},{"Sample Bb2.wav",10},{"Sample C3m.wav",-1}})if(Processor::keyFromFilename(item.first)!=item.second)return 1166;
 Processor p;p.setSwingLink(false,{});auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,96000);audio->buffer.clear();audio->path=juce::File::getCurrentWorkingDirectory().getChildFile("work/NPP_128_A_Radio_Synth.wav").getFullPathName();p.setStretchMode(true);p.setKeyLocked(true);p.setTargetKey(5);if(!p.installAudio(audio,false)||!p.stretchMode||p.loopKey!=9||p.keyTranspose()!=-4)return 1167;
 p.setStretchMode(false);if(!p.installAudio(audio,false)||p.stretchMode)return 1168;
 p.setStretchMode(true);p.setLoopKey(2);auto saved=p.configurationXml();LibraryStore::saveLoop(juce::File(audio->path),saved->toString());p.setStretchMode(false);if(!p.installAudio(audio,true)||!p.stretchMode||p.loopKey!=2)return 1169;
 std::cout<<"v0.8.31 filename key inference, browsing mode persistence and saved-state precedence passed\n";return 0;
}

int runSliceGroupsV0832Checks(){
 auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,384000);for(int n=0;n<384000;++n)audio->buffer.setSample(0,n,.3f*std::sin(n*2*juce::MathConstants<double>::pi*440/48000));StretchWorker::Request request{audio,0,384000,0,120,140,-4};double start=juce::Time::getMillisecondCounterHiRes();auto preview=StretchWorker::render(request,{},true);double fast=juce::Time::getMillisecondCounterHiRes()-start;start=juce::Time::getMillisecondCounterHiRes();auto full=StretchWorker::render(request);double slow=juce::Time::getMillisecondCounterHiRes()-start;if(!preview||!full||preview->buffer.getNumSamples()!=full->buffer.getNumSamples()||preview->buffer.getMagnitude(0,0,preview->buffer.getNumSamples())<.1)return 1170;
 int crossings=0,begin=preview->buffer.getNumSamples()/3,end=preview->buffer.getNumSamples()*2/3;for(int n=begin+1;n<end;++n)if(preview->buffer.getSample(0,n-1)<=0&&preview->buffer.getSample(0,n)>0)++crossings;if(std::abs(crossings*48000./(end-begin)-440*std::pow(2.,-4./12))>3)return 1173;
 StretchWorker worker;worker.request(request);for(int n=0;n<2000&&worker.busy;++n)juce::Thread::sleep(2);auto result=worker.result();if(!result||!(result->request==request))return 1171;auto second=request;second.pitch=3;worker.request(second);for(int n=0;n<2000&&worker.busy;++n)juce::Thread::sleep(2);worker.request(request);if(worker.busy||worker.result()!=result)return 1172;
 std::cout<<"v0.8.32 eight-second stretch preview "<<fast<<" ms versus high quality "<<slow<<" ms; immediate cache recall passed\n";return 0;
}

int runSliceGroupsV0833Checks(){
 juce::ScopedJuceInitialiser_GUI init;Processor p;p.swingLinked=false;auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,96000);for(int n=0;n<96000;++n)audio->buffer.setSample(0,n,.3f*std::sin(n*2*juce::MathConstants<double>::pi*440/48000));audio->buildWaveformPeaks();p.audio=audio;p.sourceBpm=120;p.split(4);p.assignCategory({0},39);p.padSolo[39]=true;p.padGainDb[39]=-6;p.setSliceGain({0},-6);p.setEnvelope({0},{true,0,0,100});p.setPitch(12);p.hostBpm=120;p.hasHost=true;
 auto sliced=renderLoopAudio(AudioExportSnapshot::capture(p));if(sliced.getNumSamples()!=96000)return 1175;float peak=sliced.getMagnitude(0,0,4800);if(peak<.04||peak>.08||sliced.getMagnitude(0,7000,80000)>1e-5)return 1176;
 auto wavFile=juce::File::getCurrentWorkingDirectory().getChildFile("work/v0833-export.wav");juce::AudioBuffer<float> headroom(2,32);for(int ch=0;ch<2;++ch)for(int n=0;n<32;++n)headroom.setSample(ch,n,1.5f);if(!writeRenderedAudio(wavFile,headroom,48000))return 1181;auto reader=std::unique_ptr<juce::AudioFormatReader>(p.formats.createReaderFor(wavFile));juce::AudioBuffer<float> readback(2,32);if(!reader||!reader->usesFloatingPointData||reader->lengthInSamples!=32||!reader->read(&readback,0,32,0,true,true)||std::abs(readback.getSample(0,10)-1.5f)>.000001)return 1182;
 p.setStretchMode(true);p.hostBpm=160;auto stretched=renderLoopAudio(AudioExportSnapshot::capture(p));if(stretched.getNumSamples()!=72000)return 1177;int crossings=0;for(int n=24001;n<48000;++n)if(stretched.getSample(0,n-1)<=0&&stretched.getSample(0,n)>0)++crossings;if(std::abs(crossings*2.-880)>4)return 1178;
 auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/v0833-browse");folder.createDirectory();auto first=folder.getChildFile("First_120_C.wav"),second=folder.getChildFile("Next_120_A.wav");for(auto file:{first,second}){juce::WavAudioFormat wav;auto fileStream=file.createOutputStream();fileStream->setPosition(0);fileStream->truncate();std::unique_ptr<juce::OutputStream> stream=std::move(fileStream);auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));if(!writer||!writer->writeFromAudioSampleBuffer(audio->buffer,0,96000))return 1179;}
 p.setPitch(0);p.hostBpm=140;LoopLoader loader(p);loader.prepareUpcoming({second},"initial");loader.request(first);loader.startThread();for(int n=0;n<2000;++n){auto current=p.getAudio();if(current&&current->path==first.getFullPathName()&&!p.stretchWorker.busy)break;juce::Thread::sleep(2);}juce::Thread::sleep(300);loader.request(second);bool loaded=false;for(int n=0;n<1000;++n){auto current=p.getAudio();if(current&&current->path==second.getFullPathName()){loaded=true;break;}juce::Thread::sleep(2);}if(!loaded||!p.stretchWorker.cached(p.stretchRequest())||p.stretchWorker.busy)return 1180;
 loader.signalThreadShouldExit();loader.stopThread(-1);std::cout<<"v0.8.33 rendered audio duration, solo/gains/AHD, stretched pitch and prepared browser recall passed\n";return 0;
}

int runSliceGroupsV0834Checks(){
 juce::ScopedJuceInitialiser_GUI init;TimingField bars;bars.formatter=[](double v){return juce::String(v,1);};bars.precisionWarning=[](double v){return std::abs(v*10-std::round(v*10))>1e-6;};bars.display(1.26,true);if(bars.getText()!="1.3"||bars.findColour(juce::TextEditor::textColourId)!=juce::Colour(0xffffac52))return 1183;bars.display(1.2,true);if(bars.getText()!="1.2"||bars.findColour(juce::TextEditor::textColourId)!=juce::Colours::white)return 1184;
 Processor p(false);p.swingLinked=false;auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,192000);for(int n=0;n<192000;++n)audio->buffer.setSample(0,n,.3f*std::sin(n*.08));p.audio=audio;p.sourceBpm=120;p.split(16);p.setStretchMode(true);p.prepareToPlay(48000,256);CheckPlayHead host;host.position.setIsPlaying(true);host.position.setBpm(140);host.position.setPpqPosition(.3);p.setPlayHead(&host);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;p.processBlock(block,midi);for(int i=0;i<1000&&p.stretchWorker.busy;++i)juce::Thread::sleep(2);p.processBlock(block,midi);if(block.getMagnitude(0,0,256)<.1)return 1185;
 for(int i=0;i<10;++i){p.setTrim(2000+i*1000,180000-i*1000);host.position.setPpqPosition(.3+i*.02);p.processBlock(block,midi);if(block.getMagnitude(0,0,256)<.05)return 1186;}
 for(int i=0;i<1000&&p.stretchWorker.busy;++i)juce::Thread::sleep(2);p.processBlock(block,midi);if(block.getMagnitude(0,0,256)<.05)return 1187;host.position.setIsPlaying(false);p.processBlock(block,midi);if(block.getMagnitude(0,0,256)>1e-6)return 1188;
 std::cout<<"v0.8.34 Bars precision warning and uninterrupted Stretch marker drags passed\n";return 0;
}

int runSliceGroupsV0836Checks(){
 Processor p(false);p.swingLinked=false;auto audio=std::make_shared<AudioData>();audio->buffer.setSize(1,88200);audio->buffer.clear();audio->path=juce::File::getCurrentWorkingDirectory().getChildFile("work/v0836-labelled_A.wav").getFullPathName();if(!LibraryStore::setFileKey(juce::File(audio->path),6)||!p.installAudio(audio,false)||p.loopKey!=6)return 1195;
 p.setLoopKey(2);auto saved=p.configurationXml();LibraryStore::saveLoop(juce::File(audio->path),saved->toString());if(!p.installAudio(audio,true)||p.loopKey!=6)return 1196;LibraryStore::setFileKey(juce::File(audio->path),-1);if(!p.installAudio(audio,true)||p.loopKey!=-1)return 1197;
 std::cout<<"v0.8.36 browser key overrides, saved-loop recall and cleared key persistence passed\n";return 0;
}

int runSliceGroupsV0837Checks(){
 Processor p(false);p.swingLinked=false;auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,96000);for(int n=0;n<96000;++n)audio->buffer.setSample(0,n,.2f);p.audio=audio;p.sourceBpm=120;p.split(4);p.setPlayMode(true);p.prepareToPlay(48000,256);CheckPlayHead host;host.position.setIsPlaying(true);host.position.setBpm(120);host.position.setPpqPosition(0);p.setPlayHead(&host);juce::AudioBuffer<float> block(2,256);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,37,(juce::uint8)127),128);p.processBlock(block,midi);if(block.getSample(0,100)<.05f||block.getSample(0,100)>.201f||block.getSample(0,220)>.28f)return 1198;bool automatic=false,manual=false;for(auto voice:p.voices)if(voice.active){automatic|=voice.automatic;manual|=!voice.automatic;}if(automatic||!manual)return 1199;midi.clear();host.position.setPpqPosition(256./48000*2);p.processBlock(block,midi);if(std::abs(block.getSample(0,220)-.2f)>.001)return 1199;
 std::cout<<"Play transport and sample-accurate monophonic MIDI takeover passed\n";return 0;
}

int runSliceGroupsV09Checks(){
 Processor p(false);p.swingLinked=false;auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,96000);audio->buffer.clear();for(int n=0;n<24000;++n)audio->buffer.setSample(0,n,.25f*std::sin(2*juce::MathConstants<double>::pi*220*n/48000));p.audio=audio;p.sourceBpm=120;p.trimEnd=96000;p.split(4);p.setPlaybackMode(2);p.resetHistory();
 if(!p.warpArrangement||p.warpArrangement->size()!=1||p.warpArrangement->front().end()!=4)return 1200;
 if(!p.cutWarp(0,1)||p.warpArrangement->size()!=2)return 1201;
 auto chunk=(*p.warpArrangement)[0];chunk.move(2);p.replaceWarp(0,chunk);p.deleteWarp(1);auto rendered=StretchWorker::render(p.stretchRequest());if(!rendered||rendered->buffer.getNumSamples()!=96000||rendered->buffer.getMagnitude(0,48000)>.00001f||rendered->buffer.getMagnitude(48000,24000)<.2)return 1202;
 int marker=p.addWarpPoint(0,2.5);if(marker!=1)return 1203;p.moveWarpPoint(0,marker,2.75);rendered=StretchWorker::render(p.stretchRequest());if(!rendered||rendered->buffer.getMagnitude(48000,24000)<.1)return 1204;
 auto xml=p.configurationXml(true);Processor restored(false);restored.audio=audio;if(!restored.applyConfiguration(*xml,true)||!restored.warpMode||!restored.warpArrangement||*restored.warpArrangement!=*p.warpArrangement)return 1205;
 if(!p.undoRedo(false)||(*p.warpArrangement)[0].points[1].beat!=2.5||!p.undoRedo(true)||(*p.warpArrangement)[0].points[1].beat!=2.75)return 1206;
 auto trim=WarpChunk::trim(chunk,2.25,true,false,96000);if(trim.sourceBegin()!=6000||std::abs(trim.begin()-2.25)>.00001)return 1207;auto stretched=WarpChunk::trim(chunk,4,false,true,96000);if(stretched.sourceEnd()!=24000||stretched.end()!=4)return 1208;
 p.deleteWarp(0);rendered=StretchWorker::render(p.stretchRequest());if(!rendered||rendered->buffer.getMagnitude(0,96000)>.00001f)return 1210;
 restored.prepareToPlay(48000,256);CheckPlayHead warpHost;warpHost.position.setIsPlaying(true);warpHost.position.setBpm(120);warpHost.position.setPpqPosition(2);restored.setPlayHead(&warpHost);auto request=restored.stretchRequest();auto ready=StretchWorker::render(request);restored.stretchWorker.cachePrepared(ready);juce::AudioBuffer<float> live(2,256);juce::MidiBuffer messages;restored.processBlock(live,messages);if(live.getMagnitude(0,256)<.05)return 1214;
 auto exported=renderLoopAudio(AudioExportSnapshot::capture(restored));if(exported.getNumSamples()!=96000||exported.getMagnitude(0,48000)>.00001||exported.getMagnitude(48000,24000)<.1)return 1211;
 Waveform wave(restored);wave.setSize(1000,240);auto update=[&]{wave.state.audio=audio;wave.state.bpm=120;wave.state.trimEnd=96000;wave.warpEditing=true;wave.warpChunks=restored.warpArrangement;};update();wave.changed=update;
 auto event=[&](float x,float y,int mods,juce::Point<float> origin){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,y},juce::ModifierKeys(mods|juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&wave,&wave,juce::Time::getCurrentTime(),origin,juce::Time::getCurrentTime(),1,true);};
 wave.mouseDown(event(625,100,juce::ModifierKeys::shiftModifier,{625,100}));if(restored.warpArrangement->size()!=2)return 1212;
 wave.mouseDown(event(650,100,juce::ModifierKeys::altModifier,{650,100}));wave.mouseDrag(event(750,100,juce::ModifierKeys::altModifier,{650,100}));wave.mouseUp(event(750,100,juce::ModifierKeys::altModifier,{650,100}));if(restored.warpArrangement->size()!=3)return 1213;
 std::cout<<"v0.9.0 Warp cut/move, silence, keyframe stretch, trim, save/recall and undo/redo passed\n";return 0;
}

int runSliceGroupsV091Checks(){Processor p(false);auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,70000);audio->buffer.clear();p.audio=audio;p.split(4);p.hasHost=true;p.hostBpm=100;p.sourceBpm=160;p.setPlaybackMode(2);if(p.sourceBpm!=100||std::abs(p.loopBeats()-2)>.0001||p.regionEnd()!=57600||std::abs(p.warpArrangement->front().sourceAt(1)-28800)>.001)return 1220;auto rendered=StretchWorker::render(p.stretchRequest());if(!rendered||rendered->buffer.getNumSamples()!=57600)return 1221;auto saved=p.configurationXml();p.setPlaybackMode(0);if(!p.applyConfiguration(*saved,false)||p.sourceBpm!=100||p.regionEnd()!=57600)return 1222;auto next=std::make_shared<AudioData>(*audio);next->buffer.setSize(1,100000);next->path="unsaved-warp-check.wav";if(!p.installAudio(next,false,0,false)||p.sourceBpm!=100||p.regionEnd()!=115200)return 1223;std::cout<<"v0.9.1 neutral Warp speed and nearest half-bar region passed\n";return 0;}

int runSliceGroupsV092Checks(){Processor p(false);auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,96000);audio->buffer.clear();p.audio=audio;p.split(4);p.setPlaybackMode(2);p.cutWarp(0,2);p.addWarpPoint(0,1);p.setStart(24000);auto original=*p.warpArrangement;p.resetHistory();p.beginEdit();p.stretchWarpRegion(original,0,96000,48000,24000);p.stretchWarpRegion(original,0,96000,192000,24000);p.endEdit();if(p.regionEnd()!=192000||p.startMarker!=48000||p.warpArrangement->front().points[1].beat!=2||p.warpArrangement->back().end()!=8||p.warpArrangement->back().sourceEnd()!=96000)return 1230;if(!p.undoRedo(false)||p.regionEnd()!=96000||p.warpArrangement->back().end()!=4)return 1231;if(!p.undoRedo(true)||p.regionEnd()!=192000)return 1232;Waveform wave(p);wave.setSize(1000,240);auto update=[&]{wave.state.audio=audio;wave.state.bpm=120;wave.state.trimStart=p.regionStart();wave.state.trimEnd=p.regionEnd();wave.state.startMarker=p.startMarker;wave.warpEditing=true;wave.warpChunks=p.warpArrangement;};update();wave.changed=update;auto event=[&](float x){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,25},juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::commandModifier),1,0,0,0,0,&wave,&wave,juce::Time::getCurrentTime(),{1000,25},juce::Time::getCurrentTime(),1,true);};wave.mouseDown(event(1000));wave.mouseDrag(event(750));wave.mouseDrag(event(500));wave.mouseUp(event(500));if(p.regionEnd()!=96000||p.warpArrangement->back().end()!=8)return 1233;
 std::cout<<"v0.9.2 whole Warp region stretching and single-gesture undo passed\n";return 0;}

int runSliceGroupsV093Checks(){Processor p(false);auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,96000);audio->buffer.clear();p.audio=audio;p.split(4);p.setPlaybackMode(2);p.setTrim(0,192000);p.setGridSnap(0);Waveform wave(p);wave.setSize(1000,240);auto update=[&]{wave.state.audio=audio;wave.state.bpm=120;wave.state.trimEnd=p.regionEnd();wave.warpEditing=true;wave.warpChunks=p.warpArrangement;};update();wave.changed=update;
 auto event=[&](float x,int modifiers,juce::Point<float> origin){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),{x,100},juce::ModifierKeys(modifiers|juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&wave,&wave,juce::Time::getCurrentTime(),origin,juce::Time::getCurrentTime(),1,true);};
 wave.mouseDown(event(500,0,{500,100}));wave.mouseDrag(event(250,0,{500,100}));wave.mouseUp(event(250,0,{500,100}));auto c=p.warpArrangement->front();if(std::abs(c.end()-2)>.01||c.sourceEnd()!=96000||p.regionEnd()!=192000)return 1240;
 wave.mouseDown(event(249,juce::ModifierKeys::altModifier,{249,100}));wave.mouseDrag(event(125,juce::ModifierKeys::altModifier,{249,100}));wave.mouseUp(event(125,juce::ModifierKeys::altModifier,{249,100}));c=p.warpArrangement->front();if(c.sourceEnd()>=96000||std::abs(c.end()-1)>.01||p.regionEnd()!=192000)return 1241;
 std::cout<<"v0.9.3 audio-edge default stretching, Option trimming and independent loop region passed\n";return 0;}

int runPadLockChecks(){LoopTiming guessed;guessed.bpm=65;guessed.beats=4;guessed.bars=1;auto ranged=constrainEstimatedTempo(guessed,80,160);if(ranged.bpm!=130||ranged.beats!=8)return 1292;guessed.method="Filename";if(constrainEstimatedTempo(guessed,80,160).bpm!=65)return 1293;Processor p(false);auto a=std::make_shared<AudioData>();a->rate=48000;a->buffer.setSize(1,96000);a->buffer.clear();p.audio=a;p.split(4);p.categoryStateLocked=true;p.padMuted[36]=true;p.padSolo[39]=true;auto project=p.configurationXml(true);Processor restored(false);restored.audio=a;if(!restored.applyConfiguration(*project,true)||!restored.categoryStateLocked||!restored.padMuted[36]||!restored.padSolo[39])return 1250;auto preset=p.configurationXml(false);for(auto* child:preset->getChildIterator())if(child->hasTagName("pad")){child->setAttribute("mute",false);child->setAttribute("solo",false);}if(!p.installPreset(a,*preset)||!p.padMuted[36]||!p.padSolo[39])return 1251;if(!p.applyConfiguration(*preset,false)||!p.padMuted[36]||!p.padSolo[39])return 1294;p.categoryStateLocked=false;if(!p.installPreset(a,*preset)||p.padMuted[36]||p.padSolo[39])return 1252;std::cout<<"Pad mute/solo lock project recall and preset browsing passed\n";return 0;}

int runAutoToggleChecks(){Processor p(false);p.repeatCategories=true;p.autoCategories=true;auto a=std::make_shared<AudioData>();a->rate=48000;a->buffer.setSize(1,96000);a->buffer.clear();p.audio=a;p.split(4);auto config=p.configurationXml(true);Processor restored(false);restored.audio=a;if(!restored.applyConfiguration(*config,true)||!restored.repeatCategories||!restored.autoCategories)return 1260;std::cout<<"RPT and Auto Categorise toggle project persistence passed\n";return 0;}

int runLoadBenchmark(const char* path){Processor p(false);p.autoCategories=true;auto start=juce::Time::getMillisecondCounterHiRes();auto audio=p.readAudio(juce::File(path));auto read=juce::Time::getMillisecondCounterHiRes();if(!audio)return 1;p.installAudio(audio,true,0,false);auto loaded=juce::Time::getMillisecondCounterHiRes();std::cout<<"Read/decode/waveform/transients: "<<read-start<<" ms; install/settings/categories: "<<loaded-read<<" ms\n";start=juce::Time::getMillisecondCounterHiRes();p.installAudio(audio,true,0,false);std::cout<<"Cached metadata recall: "<<juce::Time::getMillisecondCounterHiRes()-start<<" ms\n";start=juce::Time::getMillisecondCounterHiRes();auto cached=p.readAudio(juce::File(path));std::cout<<"Disk-cached audio loading: "<<juce::Time::getMillisecondCounterHiRes()-start<<" ms; analysis hit: "<<(cached&&cached->analysisCacheHit)<<"\n";return 0;}

int runFolderAnalysisChecks(){juce::ScopedJuceInitialiser_GUI gui;Processor p(false);auto source=LibraryStore::root().getChildFile("analysis-fixture-120.wav");juce::AudioBuffer<float> buffer(1,96000);for(int n=0;n<96000;++n)buffer.setSample(0,n,n%12000<1000?.3f*std::sin(n*.2):0);if(!writeRenderedAudio(source,buffer,48000))return 1280;auto first=p.readAudio(source);if(!first||first->analysisCacheHit||!AnalysisStore::exists(source))return 1281;auto second=p.readAudio(source);if(!second||!second->analysisCacheHit||second->contentHash!=first->contentHash||second->transients!=first->transients||second->peaks!=first->peaks||second->analysedTiming.bpm!=first->analysedTiming.bpm)return 1282;auto a=first->waveformRange(123,54321),b=second->waveformRange(123,54321);if(a.low!=b.low||a.high!=b.high)return 1283;source.setLastModificationTime(source.getLastModificationTime()+juce::RelativeTime::seconds(2));if(AnalysisStore::exists(source))return 1284;auto changed=p.readAudio(source);if(!changed||changed->analysisCacheHit)return 1285;if(p.readAudio(source,[]{return true;}))return 1286;AnalysisStore::fileFor(source).deleteFile();LoopLoader loader(p);loader.busy=true;FolderAnalyser analyser(p,loader);{LibraryPreparation::Foreground foreground;analyser.request({source});juce::Thread::sleep(100);if(AnalysisStore::exists(source))return 1287;}loader.busy=false;bool ready=false;for(int i=0;i<300;++i){if(AnalysisStore::exists(source)){ready=true;break;}juce::Thread::sleep(10);}if(!ready)return 1288;std::cout<<"Persistent folder analysis, waveform/transient/BPM recall, invalidation, cancellation and foreground priority passed\n";return 0;}

int runSliceContinuityChecks(){Processor p(false),reference(false);auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,96000);for(int n=0;n<96000;++n)audio->buffer.setSample(0,n,.2f+.1f*std::sin(n*.08));for(auto* engine:{&p,&reference}){engine->audio=audio;engine->sourceBpm=120;engine->split(16);engine->setPlayMode(true);engine->prepareToPlay(48000,256);}CheckPlayHead host;host.position.setBpm(120);host.position.setIsPlaying(true);p.setPlayHead(&host);reference.setPlayHead(&host);juce::AudioBuffer<float> a(2,256),b(2,256);std::atomic<bool> held{false},release{false};std::thread ui;float difference=0;for(int block=0;block<900;++block){if(block==390){ui=std::thread([&]{const juce::ScopedLock lock(p.lock);held=true;while(!release)juce::Thread::sleep(1);});while(!held)juce::Thread::sleep(1);}if(block==460){release=true;ui.join();}host.position.setPpqPosition(block*256.*2/48000);juce::MidiBuffer ma,mb;if(block==420){auto message=juce::MidiMessage::noteOn(1,39,(juce::uint8)100);ma.addEvent(message,90);mb.addEvent(message,90);}p.processBlock(a,ma);reference.processBlock(b,mb);for(int ch=0;ch<2;++ch)for(int n=0;n<256;++n)difference=juce::jmax(difference,std::abs(a.getSample(ch,n)-b.getSample(ch,n)));}if(ui.joinable()){release=true;ui.join();}if(difference>1e-6f)return 1290;reference.clearVoices();std::vector<float> cycle(96000);difference=0;for(int block=0;block<1125;++block){host.position.setPpqPosition(block*256.*2/48000);juce::MidiBuffer empty;reference.processBlock(b,empty);for(int n=0;n<256;++n){int sample=block*256+n;if(sample>=96000&&sample<192000)cycle[sample-96000]=b.getSample(0,n);else if(sample>=192000)difference=juce::jmax(difference,std::abs(cycle[sample-192000]-b.getSample(0,n)));}}if(difference>1e-6f)return 1291;std::cout<<"Slice transport/MIDI rendering remains sample-identical across 70 UI-contended blocks and recovery\n";return 0;}

int runLoudnessChecks(){
    juce::AudioBuffer<float> mono(1,96000),stereo(2,96000),silent(2,96000);silent.clear();
    for(int n=0;n<96000;++n){float value=.1f*std::sin(2*juce::MathConstants<double>::pi*1000*n/48000);mono.setSample(0,n,value);stereo.setSample(0,n,value);stereo.setSample(1,n,value);}
    double m=LoopLoudness::measure(mono,48000),s=LoopLoudness::measure(stereo,48000);
    if(std::abs(m+23.0)>.15||std::abs((s-m)-3.0103)>.01||LoopLoudness::measure(silent,48000)!=-100)return 1430;
    juce::AudioBuffer<float> gated(1,96000*5);gated.clear();gated.copyFrom(0,0,mono,0,0,96000);if(std::abs(LoopLoudness::measure(gated,48000)-m)>.5)return 1431;
    juce::AudioBuffer<float> hot(2,48000);for(int n=0;n<48000;++n){float value=n<24000?2.f:.1f;hot.setSample(0,n,value);hot.setSample(1,n,value*.5f);}LoopPeakLimiter limiter;limiter.process(hot,48000);if(hot.getMagnitude(0,48000)>.891252f||limiter.reduction<=0)return 1432;for(int n=0;n<48000;++n)if(std::abs(hot.getSample(1,n)-hot.getSample(0,n)*.5f)>1e-6)return 1433;
    Processor p(false);auto data=std::make_shared<AudioData>();data->buffer=mono;data->rate=48000;data->integratedLufs=m;data->buildWaveformPeaks();p.audio=data;p.split(4,false);p.normaliseLufs=p.normaliseEnabled=true;p.targetLufs=-18;if(std::abs(20*std::log10(p.normaliseGain())-(-18-m-3.01029995664))>1e-8)return 1434;auto config=p.configurationXml();Processor restored(false);restored.audio=data;if(!restored.applyConfiguration(*config,false)||!restored.normaliseLufs||restored.targetLufs!=-18)return 1435;
    auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/loudness-check");folder.createDirectory();auto source=folder.getChildFile("reference.wav");if(!writeRenderedAudio(source,mono,48000))return 1436;auto first=LoopLoudness::analyse(source,mono,48000);double cached;if(!LoopLoudness::cached(source,cached)||std::abs(first-cached)>1e-9)return 1437;
    data->integratedLufs=-100;if(p.normaliseGain()!=1)return 1438;
    std::cout<<"LUFS K-weighting/gates, silence, stereo weighting, gain, persistence, cached analysis and stereo-linked limiter passed; reference "<<m<<" LUFS\n";return 0;
}

int runOneShotChecks(){
    // Note-off during decay releases from the actual current level, independent of buffer size.
    for(float sustain:{0.f,.05f,.25f})for(int size:{17,128,512}){Processor q(false);auto dc=std::make_shared<AudioData>();dc->rate=48000;dc->buffer.setSize(1,48000);for(int i=0;i<48000;++i)dc->buffer.setSample(0,i,.5f);q.audio=dc;q.setPlaybackMode(3);q.shotEnvelope={0,.2f,sustain,.005f};q.prepareToPlay(48000,size);juce::AudioBuffer<float> sound(2,size);juce::MidiBuffer notes;notes.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);q.processBlock(sound,notes);notes.addEvent(juce::MidiMessage::noteOff(1,60),0);int frames=0;while(frames<480){q.processBlock(sound,notes);frames+=size;}if(sound.getSample(0,size-1)!=0||q.shots[0].active)return 1542;}
    // Zero sustain decays have a soft terminal slope and remain identical across host block sizes.
    {auto dc=std::make_shared<AudioData>();dc->rate=48000;dc->buffer.setSize(1,48000);for(int i=0;i<48000;++i)dc->buffer.setSample(0,i,.5f);std::vector<float> reference;for(int size:{17,256}){Processor q(false);q.audio=dc;q.setPlaybackMode(3);q.shotEnvelope={0,.002f,0,.02f};q.prepareToPlay(48000,size);q.triggerShot(60,1);juce::AudioBuffer<float> sound(2,size);juce::MidiBuffer notes;std::vector<float> rendered;while(rendered.size()<1024){q.processBlock(sound,notes);for(int i=0;i<size;++i)rendered.push_back(sound.getSample(0,i));}if(rendered[143]>.0001f||std::abs(rendered[143]-rendered[142])>.0003f||std::abs(rendered[500])>1e-8)return 1540;if(reference.empty())reference=rendered;else for(int i=0;i<1024;++i)if(std::abs(reference[i]-rendered[i])>1e-7)return 1541;}}

    Processor p(false);auto data=std::make_shared<AudioData>();data->rate=48000;data->buffer.setSize(1,48000);for(int n=0;n<48000;++n)data->buffer.setSample(0,n,.2f*std::sin(2*juce::MathConstants<double>::pi*400*n/48000));p.audio=data;p.split(256,false);if(p.slices.size()!=256)return 1460;p.rebuildTimeline();if(p.eventCount<256)return 1461;auto largeXml=p.configurationXml();Processor largeRestored(false);largeRestored.audio=data;if(!largeRestored.applyConfiguration(*largeXml,false)||largeRestored.slices.size()!=256)return 1462;auto halfMidi=p.makeMidi(120);auto* track=halfMidi.getTrack(0);int hits=0;for(int i=0;i<track->getNumEvents();++i)if(track->getEventPointer(i)->message.isNoteOn())++hits;if(hits!=128||std::abs(track->getEndTime()-p.loopBeats(120)*480)>1e-6)return 1463;p.split(1);p.loopKey=2;p.rootOctave=3;p.shotEnvelope={0,.005f,.5f,.01f};p.setPlaybackMode(3);p.prepareToPlay(48000,512);juce::AudioBuffer<float> block(2,512);juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);p.processBlock(block,midi);if(!p.shots[0].active||std::abs(p.shots[0].ratio-std::pow(2.,-2./12))>1e-9||block.getMagnitude(0,512)<.01)return 1450;midi.addEvent(juce::MidiMessage::noteOff(1,60),0);p.processBlock(block,midi);if(p.shots[0].active)return 1451;
    p.setPlaybackMode(3);midi.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);midi.addEvent(juce::MidiMessage::noteOn(1,62,1.f),50);p.processBlock(block,midi);int active=0;for(auto& voice:p.shots)if(voice.active)++active;if(active!=2)return 1452;auto xml=p.configurationXml();Processor restored(false);restored.audio=data;if(!restored.applyConfiguration(*xml,false)||!restored.oneShotMode||restored.rootOctave!=3||std::abs(restored.shotEnvelope.sustain-.5f)>1e-6)return 1453;
    for(int octave:{-3,3}){p.shotOctaveShift=octave;p.triggerShot(62,1);auto& voice=p.shots[(p.shotCursor-1)%32];if(std::abs(voice.ratio-std::pow(2.,octave))>1e-8)return 1480;auto state=p.configurationXml();if(!restored.applyConfiguration(*state,false)||restored.shotOctaveShift!=octave)return 1481;}p.shotOctaveShift=0;
    auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/shot-type-check");folder.createDirectory();auto source=folder.getChildFile("sample_D.wav");if(!writeRenderedAudio(source,data->buffer,48000))return 1454;LibraryStore::setPlaybackType({folder},1);if(LibraryStore::playbackType(source)!=1)return 1455;Processor loaded(false);if(!loaded.load(source)||!loaded.oneShotMode||loaded.loopKey!=2)return 1456;LibraryStore::setPlaybackType({source},0);if(LibraryStore::playbackType(source)!=0||!loaded.load(source)||loaded.oneShotMode)return 1457;LibraryStore::setPlaybackType({source},-1);if(LibraryStore::playbackType(source)!=1)return 1458;
    {Processor looped(false);auto constant=std::make_shared<AudioData>();constant->rate=48000;constant->buffer.setSize(1,48000);for(int i=0;i<48000;++i)constant->buffer.setSample(0,i,.2f);looped.audio=constant;looped.split(1);looped.setPlaybackMode(3);looped.shotEnvelope={0,0,1,.01f};looped.shotStart=1000;looped.shotEnd=24000;looped.shotLoopStart=4000;looped.shotLoopEnd=8000;looped.shotLoop=true;looped.shotCrossfadeMs=10;looped.prepareToPlay(48000,512);juce::MidiBuffer notes;notes.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);juce::AudioBuffer<float> sound(2,512);for(int i=0;i<80;++i)looped.processBlock(sound,notes);if(!looped.shots[0].active||looped.shots[0].pos<4000||looped.shots[0].pos>=8000||sound.getMagnitude(0,512)<.19f)return 1470;notes.addEvent(juce::MidiMessage::noteOff(1,60),0);looped.processBlock(sound,notes);if(looped.shots[0].active)return 1471;looped.shotLoopFade=100;looped.triggerShot(60,1);int voice=(looped.shotCursor-1)%32;for(int i=0;i<80;++i)looped.processBlock(sound,notes);if(sound.getMagnitude(0,512)>.002||!looped.shots[voice].enteredLoop)return 1472;auto config=looped.configurationXml();Processor recall(false);recall.audio=constant;if(!recall.applyConfiguration(*config,false)||!recall.shotLoop||recall.shotStart!=1000||recall.shotEndSample()!=24000||recall.shotLoopBeginSample()!=4000||recall.shotLoopEndSample()!=8000||recall.shotLoopFade!=100||recall.shotCrossfadeSamples()!=480)return 1473;looped.shotLoop=false;looped.shotLoopFade=0;looped.triggerShot(60,1);for(int i=0;i<80;++i)looped.processBlock(sound,notes);for(auto& v:looped.shots)if(v.active)return 1474;recall.audio=data;recall.shotZeroSnap=true;recall.setShotBoundary(0,12013);if(std::abs(data->buffer.getSample(0,recall.shotStart))>.02f||std::abs(recall.shotStart-12013)>100)return 1475;}
    {auto cycle=std::make_shared<AudioData>();cycle->rate=48000;cycle->buffer.setSize(1,1024);for(int i=0;i<1024;++i)cycle->buffer.setSample(0,i,.2f*std::sin(2*juce::MathConstants<double>::pi*i/256));Processor plain(false),smooth(false);for(auto* q:{&plain,&smooth}){q->audio=cycle;q->split(1);q->setPlaybackMode(3);q->shotEnvelope={0,0,1,.01f};q->shotLoop=true;q->shotLoopStart=256;q->shotLoopEnd=512;q->prepareToPlay(48000,512);}smooth.shotCrossfadeMs=100;plain.triggerShot(60,1);smooth.triggerShot(60,1);juce::MidiBuffer empty;juce::AudioBuffer<float> a(2,512),b(2,512);for(int block=0;block<16;++block){plain.processBlock(a,empty);smooth.processBlock(b,empty);for(int n=0;n<512;++n)if(std::abs(a.getSample(0,n)-b.getSample(0,n))>1e-5)return 1490;}if(std::abs(plain.shots[0].pos-smooth.shots[0].pos)>1e-8)return 1491;smooth.shotMono=true;smooth.triggerShot(62,.25f);if(!smooth.shots[0].choked||std::abs(smooth.shots[1].velocity-.25f)>1e-6)return 1492;smooth.processBlock(b,empty);if(smooth.shots[0].active||!smooth.shots[1].active)return 1493;smooth.shotVelocity=0;smooth.triggerShot(64,.1f);if(smooth.shots[2].velocity!=1)return 1494;auto xml=smooth.configurationXml();Processor recall(false);recall.audio=cycle;if(!recall.applyConfiguration(*xml,false)||!recall.shotMono||recall.shotVelocity!=0)return 1495;}
    {auto folder=juce::File::getCurrentWorkingDirectory().getChildFile("work/sfz-fixture"),samples=folder.getChildFile("Samples");samples.createDirectory();juce::AudioBuffer<float> tone(1,2000);for(int n=0;n<2000;++n)tone.setSample(0,n,.2f);if(!writeRenderedAudio(samples.getChildFile("Low Tone.wav"),tone,48000))return 1500;for(int n=0;n<2000;++n)tone.setSample(0,n,.4f);if(!writeRenderedAudio(samples.getChildFile("High Tone.wav"),tone,48000))return 1501;auto instrument=folder.getChildFile("instrument.sfz");instrument.replaceWithText("<control> default_path=Samples/\n<global> ampeg_attack=0 ampeg_decay=0 ampeg_sustain=100 ampeg_release=0.01\n<group> lovel=1 hivel=127\n<region> sample=Low Tone.wav lokey=48 hikey=60 pitch_keycenter=60 loop_mode=loop_continuous loop_start=100 loop_end=999\n<region> sample=High Tone.wav lokey=61 hikey=84 pitch_keycenter=72 volume=-6 pan=50 cutoff=1000\n");Processor sfz(false);if(!sfz.load(instrument)||!sfz.oneShotMode||sfz.audio->sfzRegions.size()!=2||!sfz.audio->sfzWarning.contains("cutoff")||!sfz.shotLoop)return 1502;sfz.prepareToPlay(48000,512);sfz.triggerShot(60,.5f);juce::AudioBuffer<float> sound(2,512);juce::MidiBuffer empty;sfz.processBlock(sound,empty);if(std::abs(sound.getSample(1,300)-.1f)>1e-4)return 1503;sfz.clearVoices();sfz.triggerShot(72,.5f);sfz.processBlock(sound,empty);if(std::abs(sound.getSample(1,300)-.1f)>1e-3||std::abs(sound.getSample(0,300)-.05f)>1e-3)return 1504;sfz.clearVoices();sfz.triggerShot(90,1);for(auto& voice:sfz.shots)if(voice.active)return 1505;juce::AudioFormatManager formats;formats.registerBasicFormats();auto found=BrowserSearchIndex::enumerate(folder,{},true);if(!found.contains(instrument))return 1506;}
    {Processor mono(false);auto tone=std::make_shared<AudioData>();tone->rate=48000;tone->buffer.setSize(1,24000);for(int n=0;n<24000;++n)tone->buffer.setSample(0,n,.2f);mono.audio=tone;mono.split(1);mono.setPlaybackMode(3);mono.shotMono=true;mono.shotLegato=true;mono.shotLoop=true;mono.shotEnvelope={.1f,0,1,.01f};mono.prepareToPlay(48000,128);juce::AudioBuffer<float> sound(2,128);juce::MidiBuffer notes;notes.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);mono.processBlock(sound,notes);double before=mono.shots[0].pos;float first=sound.getSample(0,127);notes.addEvent(juce::MidiMessage::noteOn(1,64,1.f),0);mono.processBlock(sound,notes);if(mono.shotCursor!=1||mono.shotMonoNote!=64||mono.shots[0].pos<=before||sound.getSample(0,127)<=first)return 1510;notes.addEvent(juce::MidiMessage::noteOff(1,64),0);mono.processBlock(sound,notes);if(mono.shotMonoNote!=60||mono.shots[0].note!=60||mono.shotCursor!=1)return 1511;notes.addEvent(juce::MidiMessage::noteOn(1,64,1.f),0);notes.addEvent(juce::MidiMessage::noteOn(1,67,1.f),1);notes.addEvent(juce::MidiMessage::noteOff(1,64),2);mono.processBlock(sound,notes);if(mono.shotMonoNote!=67)return 1512;notes.addEvent(juce::MidiMessage::noteOff(1,67),0);mono.processBlock(sound,notes);if(mono.shotMonoNote!=60)return 1513;notes.addEvent(juce::MidiMessage::noteOff(1,60),0);for(int i=0;i<5;++i)mono.processBlock(sound,notes);if(mono.shots[0].active||mono.shotMonoNote!=-1)return 1514;mono.shotLegato=false;notes.addEvent(juce::MidiMessage::noteOn(1,60,1.f),0);notes.addEvent(juce::MidiMessage::noteOn(1,64,1.f),1);mono.processBlock(sound,notes);int cursor=mono.shotCursor;notes.addEvent(juce::MidiMessage::noteOff(1,64),0);mono.processBlock(sound,notes);if(mono.shotMonoNote!=60||mono.shotCursor!=cursor+1)return 1515;notes.addEvent(juce::MidiMessage::allNotesOff(1),0);mono.processBlock(sound,notes);for(auto order:mono.shotHeldOrder)if(order)return 1516;mono.shotLegato=true;auto config=mono.configurationXml();Processor restored(false);restored.audio=tone;if(!restored.applyConfiguration(*config,false)||!restored.shotLegato)return 1517;}
    {Processor note(false);auto tone=std::make_shared<AudioData>();tone->rate=48000;tone->buffer.setSize(1,48000);double hz=440*std::pow(2.,(48-69)/12.);for(int i=0;i<48000;++i)tone->buffer.setSample(0,i,.2f*std::sin(2*juce::MathConstants<double>::pi*hz*i/48000));note.audio=tone;note.split(1);note.loopKey=0;note.rootOctave=3;if(!note.applyShotNoteLoop()||!note.shotNoteLoop||std::abs((note.shotLoopEndSample()-note.shotLoopBeginSample())/48.-15.289)>.02)return 1520;int size=note.shotLoopEndSample()-note.shotLoopBeginSample();if(!note.scaleShotLoop(2)||!note.shotNoteLoop||note.shotNoteCycles!=2||std::abs(note.shotLoopEndSample()-note.shotLoopBeginSample()-size*2)>1)return 1525;if(!note.scaleShotLoop(.5)||note.shotNoteCycles!=1||note.shotLoopEndSample()-note.shotLoopBeginSample()!=size)return 1526;note.moveShotLoop(2000,size,false);if(note.shotNoteLoop||note.shotLoopBeginSample()!=2000||note.shotLoopEndSample()!=2000+size)return 1521;note.shotLoopStart=0;note.shotZeroSnap=true;if(!note.applyShotNoteLoop()||note.shotLoopEndSample()!=note.nearestZero(size,1,note.shotEndSample()))return 1522;auto config=note.configurationXml();Processor recall(false);recall.audio=tone;if(!recall.applyConfiguration(*config,false)||!recall.shotNoteLoop)return 1523;note.setShotBoundary(3,note.shotLoopEndSample()+20);if(note.shotNoteLoop)return 1524;note.shotLoop=false;if(note.scaleShotLoop(2))return 1527;}
    std::cout<<"C3 doubled note length, zero-snap precedence, loop movement, manual disengagement and persistence passed\n";
    std::cout<<"Mono last-held-note priority, middle-note release, legato envelope/phase continuity, retrigger fallback, all-notes-off and persistence passed\n";
    std::cout<<"Single-cycle crossfade preserves waveform and period; mono choke, velocity, SFZ regions/ranges/tuning/pan/loop and state passed\n";
    std::cout<<"One Shot sample bounds, sustained loop/crossfade, note-off release, loop-only decay, zero crossing snap and saved state passed\n";
    std::cout<<"One Shot chromatic root mapping, MIDI note-off ADSR release, polyphony, persistence and folder/file playback type inheritance passed\n";return 0;
}

int runSfzFileCheck(const char* path){Processor p(false);auto data=p.readAudio(juce::File(juce::String::fromUTF8(path)));if(!data||!p.installAudio(data,false))return 1600;std::cout<<"Loaded "<<data->sfzRegions.size()<<" regions; mode="<<p.playbackMode()<<"\n";p.prepareToPlay(48000,64);juce::AudioBuffer<float> out(2,64);juce::MidiBuffer midi;std::set<juce::String> sources;int checks=0;for(int note:{36,45,50,55,60,65,70,80})for(int velocity:{1,10,25,45,60,70,80,90,100,105,115,127}){p.clearVoices();midi.addEvent(juce::MidiMessage::noteOn(1,note,(juce::uint8)velocity),0);p.processBlock(out,midi);int count=0;for(auto& v:p.shots)if(v.active&&v.region){++count;sources.insert(v.source->path);if(note<v.region->low||note>v.region->high||velocity<v.region->lovel||velocity>v.region->hivel)return 1601;if((note==36||note==60||note==80)&&(velocity==10||velocity==70||velocity==127))std::cout<<"Note "<<note<<" velocity "<<velocity<<" -> "<<juce::File(v.source->path).getFileName()<<" root "<<v.region->root<<" ratio "<<v.ratio<<"\n";}if(count!=1)return 1602;++checks;}std::cout<<checks<<" note/velocity checks; "<<sources.size()<<" different sample files actually triggered.\n"<<data->sfzWarning<<"\n";return 0;}

#include "Release032Checks.h"

#include "Release033Checks.h"

#include "SaveABChecks.h"

#include "Browser332Checks.h"

#include "ManualRefreshChecks.h"

#include "Save334Checks.h"

#include "Type335Checks.h"

#include "Preparation340Checks.h"

#include "Json341Checks.h"

#include "Database350Checks.h"

#include "Folder351Checks.h"

#include "Anchor352Checks.h"

#include "Stretch360Checks.h"
#include "Features370Checks.h"
#include "Features380Checks.h"
#include "FolderSearch381Checks.h"

#include "OneShot382Checks.h"

#include "Travel383Checks.h"

#include "Release384Checks.h"

#include "Output385Checks.h"

#include "Relocate386Checks.h"

#include "Features387Checks.h"

#include "Move388Checks.h"

#include "Multi390Checks.h"

#include "Sequence400Checks.h"

#include "Voice401Checks.h"

#include "Swing402Checks.h"

int runDistribution403(){juce::ScopedJuceInitialiser_GUI gui;try{auto cache=trackStemModelDirectory();if(!cache.getFullPathName().endsWith("/Library/Application Support/Slice Groups/Models/scnet-v1"))throw std::runtime_error("model cache location");auto bundled=juce::File::getCurrentWorkingDirectory().getChildFile("outputs/SliceGroups/Resources");for(bool hq:{false,true}){auto f=bundled.getChildFile(hq?"SCNet-HQ.pt":"SCNet.pt");if(!trackStemWeightValid(f,hq))throw std::runtime_error("model integrity");}LibraryStore::root().createDirectory();auto fixture=LibraryStore::root().getChildFile("invalid-model.pt");if(!fixture.replaceWithText("bad"))throw std::runtime_error("fixture creation");if(trackStemWeightValid(fixture,false))throw std::runtime_error("invalid model accepted");setenv("SLICE_GROUPS_SCNET_MODEL",fixture.getFullPathName().toRawUTF8(),1);if(trackStemModelFile()!=fixture)throw std::runtime_error("override changed");trackStemEnsureWeight(fixture,false);if(fixture.loadFileAsString()!="bad")throw std::runtime_error("override overwritten");unsetenv("SLICE_GROUPS_SCNET_MODEL");auto json=juce::JSON::parse(R"({"tag_name":"v0.40.3","html_url":"https://github.com/dcupmusic/slice-groups-releases/releases/tag/v0.40.3","assets":[{"name":"SliceGroups-0.40.3-AppleSilicon.zip","state":"uploaded","browser_download_url":"https://github.com/dcupmusic/slice-groups-releases/releases/download/v0.40.3/SliceGroups-0.40.3-AppleSilicon.zip"},{"name":"SliceGroups-0.40.3-AppleSilicon-Update.zip","state":"uploaded","browser_download_url":"https://github.com/dcupmusic/slice-groups-releases/releases/download/v0.40.3/SliceGroups-0.40.3-AppleSilicon-Update.zip"}]})");auto release=AppUpdates::parseRelease(json,"0.40.2");if(!release.available||!release.url.endsWith("-Update.zip"))throw std::runtime_error("lightweight update preference");std::cout<<"Model cache path, model hashes, override protection and lightweight update selection passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 4031;}}

int runNavigation404(){juce::ScopedJuceInitialiser_GUI gui;try{Processor p(false);p.autoSave=false;p.sequence.bars=2;p.sequence.notes={{1,60,100,1,0,1},{2,67,100,1,2,1}};SequencerEditor panel(p);panel.setSize(900,500);if(!panel.navigation404ForCheck())throw std::runtime_error("vertical scroll/zoom and bounds");auto stream=LibraryStore::root().getChildFile("sequencer404.png").createOutputStream();juce::PNGImageFormat png;if(stream)png.writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()),*stream);Editor editor(p);if(!editor.fileKeys404ForCheck())throw std::runtime_error("file shortcuts, typing guards and conflict transfer");std::cout<<"Vertical wheel scrolling, cursor-centred pitch zoom, full MIDI range, draggable pitch bar and view-only edits; global previous/next/trash shortcuts with typing guards and conflict transfer passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 4041;}}

int runLasso405(){juce::ScopedJuceInitialiser_GUI gui;Processor p(false);p.autoSave=false;auto audio=std::make_shared<AudioData>();audio->rate=48000;audio->buffer.setSize(1,48000);audio->buffer.clear();audio->path=LibraryStore::root().getChildFile("Tone.wav").getFullPathName();p.audio=audio;p.split(4,false);p.sequence.bars=2;p.sequence.notes={{1,60,100,1,1,.25},{2,64,100,1,2,.25},{3,67,100,1,4,.25}};p.resetHistory();SequencerEditor editor(p);editor.setSize(1000,520);if(!editor.lasso405ForCheck()){std::cerr<<"Lasso/delete/clear failed\n";return 4051;}auto out=LibraryStore::root().getChildFile("lasso405.png").createOutputStream();juce::PNGImageFormat png;if(out)png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()),*out);std::cout<<"Lasso both directions, additive Shift selection, selected delete, Clear and undo passed.\n";return 0;}

int runBrowser406(){juce::ScopedJuceInitialiser_GUI gui;try{Processor p(false);p.autoSave=false;if(p.gridSnapMode||p.boundarySnap)throw std::runtime_error("initial snap defaults");auto audio=std::make_shared<AudioData>();audio->path=LibraryStore::root().getChildFile("Unsaved.wav").getFullPathName();audio->rate=48000;audio->buffer.setSize(1,48000);audio->buffer.clear();p.gridSnapMode=2;p.boundarySnap=1;if(!p.installAudio(audio,true,0,false)||p.gridSnapMode||p.boundarySnap)throw std::runtime_error("unsaved file snap reset");p.gridSnapMode=2;p.boundarySnap=1;auto saved=p.configurationXml();p.gridSnapMode=p.boundarySnap=0;if(!p.applyConfiguration(*saved,false)||p.gridSnapMode!=2||p.boundarySnap!=1)throw std::runtime_error("saved snap preserved");struct Model:juce::ListBoxModel{int getNumRows()override{return 2;}void paintListBoxItem(int row,juce::Graphics& g,int w,int h,bool)override{g.setColour(juce::Colours::white);g.drawText(row?"Destination folder":"Other folder",8,0,w-10,h,juce::Justification::centredLeft);}}model;BrowserList list;list.setModel(&model);list.setSize(300,100);list.updateContent();int dropped=-1;list.drop=[&](const juce::var&,int row){dropped=row;};juce::Array<juce::var> files;files.add(audio->path);juce::DragAndDropTarget::SourceDetails first(juce::var(files),&list,list.getRowPosition(0,true).getCentre()),second(juce::var(files),&list,list.getRowPosition(1,true).getCentre());list.itemDragEnter(first);if(list.dropRow!=0)throw std::runtime_error("drag enter highlight");list.itemDragMove(second);if(list.dropRow!=1)throw std::runtime_error("drag move highlight");auto out=LibraryStore::root().getChildFile("drop406.png").createOutputStream();juce::PNGImageFormat png;if(out)png.writeImageToStream(list.createComponentSnapshot(list.getLocalBounds()),*out);list.fileDragEnter({audio->path},first.localPosition.x,first.localPosition.y);if(list.dropRow!=0)throw std::runtime_error("native file drag enter highlight");list.fileDragMove({audio->path},second.localPosition.x,second.localPosition.y);if(list.dropRow!=1)throw std::runtime_error("native file drag move highlight");list.filesDropped({audio->path},second.localPosition.x,second.localPosition.y);if(dropped!=1||list.dropRow!=-1)throw std::runtime_error("native file drop cleanup");list.itemDragExit(second);if(list.dropRow!=-1)throw std::runtime_error("drag exit cleanup");list.itemDragEnter(second);list.itemDropped(second);if(dropped!=1||list.dropRow!=-1)throw std::runtime_error("drop target and cleanup");RackProcessor rack;RackEditor editor(rack);if(!editor.shortcut406ForCheck())throw std::runtime_error("rack hotkey routing / stale held key / read-only typing guards");std::cout<<"Folder drop highlights, target and cleanup; fresh/unsaved snap defaults and saved recall; rack hotkeys, repeated fresh presses without release events, read-only and typing guards passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 4061;}}

#include "Transient407Checks.h"

int runGrid409(){Processor p(false);p.autoSave=false;auto a=std::make_shared<AudioData>();a->rate=48000;a->buffer.setSize(1,480000);a->buffer.clear();p.audio=a;p.sourceBpm=120;p.split(4,false);p.useIndependentGrid();p.gridStart=0;p.setTrim(288000,384000);p.setStart(288000,false);p.resetHistory();p.beginEdit();p.alignGridBeat(16,386000,4);p.alignGridBeat(16,388000,4);p.endEdit();if(p.regionStart()!=288000||p.regionEnd()!=388000||std::abs(p.sourceBpm-115.2)>1e-8)return 4091;if(std::abs(p.gridOrigin()+12*60/p.sourceBpm*48000-288000)>1)return 4092;auto xml=p.configurationXml();Processor q(false);q.audio=a;if(!q.applyConfiguration(*xml,false)||q.gridOrigin()!=p.gridOrigin()||q.regionStart()!=288000)return 4093;if(!p.undoRedo(false)||p.regionStart()!=288000||p.regionEnd()!=384000||p.sourceBpm!=120)return 4094;if(!p.undoRedo(true)||p.regionEnd()!=388000)return 4095;p.setSourceBpm(100);if(p.regionStart()!=288000||p.regionEnd()!=403200||std::abs(p.gridOrigin()+12*28800.-288000)>1)return 4096;p.setSourceBpm(130);if(p.regionStart()!=288000||std::abs(p.loopBeats()-4)>1e-8)return 4097;std::cout<<"Loop-in anchored grid drag, BPM edits, negative origin recall and grouped undo passed\n";int legacy=runSliceGroupsV089Checks();return legacy?legacy:runSliceGroupsV0810Checks();}

int runDisk410(){juce::ScopedJuceInitialiser_GUI gui;LibraryPreparation::checksPaused=true;auto root=LibraryStore::root().getChildFile("watch-fixture");root.deleteRecursively();root.createDirectory();root.getChildFile("destination").createDirectory();juce::AudioBuffer<float> tone(1,1200);tone.clear();for(auto name:{"first.wav","second.wav"})if(!writeRenderedAudio(root.getChildFile(name),tone,48000))return 4101;Processor p(false);p.autoSave=false;Editor e(p);e.setSize(1200,1000);return e.disk410ForCheck(root)?0:4102;}

int runDelta411(){juce::ScopedJuceInitialiser_GUI gui;LibraryPreparation::checksPaused=true;auto root=LibraryStore::root().getChildFile("large-delta-fixture");root.deleteRecursively();root.createDirectory();root.getChildFile("destination").createDirectory();auto cold=root.getChildFile("cold");cold.createDirectory();juce::AudioBuffer<float> tone(1,1200);tone.clear();juce::Array<juce::File> files{cold,root.getChildFile("destination")};for(auto name:{"first.wav","second.wav"}){auto f=root.getChildFile(name);if(!writeRenderedAudio(f,tone,48000))return 4111;files.add(f);}for(int i=0;i<50000;++i)files.add(cold.getChildFile("sample-"+juce::String(i).paddedLeft('0',5)+".wav"));BrowserSearchIndex::store(root,files);LocalDatabase::deltaCatalogWrites=0;Processor p(false);p.autoSave=false;Editor e(p);e.setSize(1200,1000);return e.delta411ForCheck(root)?0:4112;}
