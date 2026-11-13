#pragma once
int runMidiContinueChecks(){
    juce::ScopedJuceInitialiser_GUI gui;
    auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);};
    auto wait=[&](auto predicate){for(int i=0;i<1000;++i){if(predicate())return true;juce::Thread::sleep(2);}return false;};
    try{
        auto root=LibraryStore::root().getChildFile("MIDI Continue");require(root.createDirectory().wasOk(),"create fixtures");
        auto make=[&](juce::String name,int pitch,double begin,double length){
            SequenceClip clip;clip.key=0;clip.setLoop(begin,begin+length);
            clip.notes={{1,pitch,100,1,begin,length+1},{2,90,100,1,begin+length+1,.25}};clip.nextId=3;
            auto file=root.getChildFile(name+".mid");require(clip.save(file),"save fixture");return file;
        };
        auto first=make("01_C",60,2,1),second=make("02_C",67,3,2),third=make("03_C",72,0,3);
        auto missing=root.getChildFile("missing.mid");
        auto configure=[&](Processor& p){
            p.autoSave=false;p.oneShotMode=true;p.sequenceLocked=true;p.sequenceMidiOut=true;p.outputRate=2000;
            require(p.loadSequence(first),"load first clip");p.setAutoContinue(true);p.setMidiAdvanceBrowserOrder({first,root.getChildFile("skip.wav"),second,third});
        };
        auto prepared=[&](Processor& p){return wait([&]{p.serviceMidiAdvance();const juce::ScopedLock guard(p.lock);return p.midiAdvanceReady&&p.midiAdvanceMatches(*p.midiAdvanceReady);});};
        auto render=[&](Processor& p,double beat,int frames=100){juce::MidiBuffer midi;p.renderSequenceMidi(midi,beat,.001,frames,true);return midi;};
        auto noteAt=[](const juce::MidiBuffer& midi,int pitch,bool on,int sample){for(auto e:midi){auto m=e.getMessage();if(m.isNoteOnOrOff()&&m.getNoteNumber()==pitch&&m.isNoteOn()==on&&e.samplePosition==sample)return true;}return false;};
        {
            Processor p(false);configure(p);p.pitchSemitones=3;p.keyLocked=true;p.targetKey=0;
            auto instrument=std::make_shared<AudioData>();instrument->rate=2000;instrument->buffer.setSize(1,6000);instrument->buffer.clear();p.audio=instrument;
            require(prepared(p),"prepare while editor is closed");
            auto midi=render(p,.95);
            require(p.sequence.path==second.getFullPathName()&&p.midiAdvanced==1,"advance despite Lock Sequence");
            require(noteAt(midi,60,false,50)&&noteAt(midi,67,true,50),"in-block release and successor onset share the endpoint");
            require(p.sequence.loopStart==3&&p.sequence.beats()==2,"incoming clip uses its own saved loop");
            require(p.audio==instrument&&p.oneShotMode&&p.pitchSemitones==3&&p.keyLocked&&p.sequenceMidiOut,"instrument and pitch/output settings retained");
            p.finishSequenceOutput(midi);
            require(noteAt(midi,60,false,50)&&noteAt(midi,67,true,50),"MIDI Out matches the handoff");
            require(!noteAt(midi,90,true,50),"out-of-region note excluded");
            Processor fallback(false);p.copySequenceConfiguration(fallback);p.copySequenceRuntime(fallback);require(fallback.sequenceOrigin==1,"fallback retains the advanced clip phase");
            auto next=render(p,1.05);for(auto e:next)require(!e.getMessage().isNoteOn(),"held successor note is not restarted");
            require(prepared(p),"prepare third clip");render(p,2.95);
            require(p.sequence.path==third.getFullPathName()&&p.sequence.beats()==3&&p.midiAdvanced==2,"different-length successors remain seamless");
            require(prepared(p),"stage end of list");auto stop=render(p,5.95);
            require(p.midiAdvancePaused&&p.sequence.path==third.getFullPathName()&&noteAt(stop,72,false,50),"end of list stops and releases while retaining clip");
            for(auto e:render(p,6.05))require(!e.getMessage().isNoteOn(),"stopped sequence never restarts");
        }
        {
            Processor p(false);configure(p);require(prepared(p),"prepare fractional-frame endpoint");
            juce::MidiBuffer midi;p.renderSequenceMidi(midi,.9505,.001,100,true);
            require(noteAt(midi,67,true,50),"fractional-frame endpoint uses the first available sample");
            for(auto event:midi)require(!(event.getMessage().isNoteOn()&&event.getMessage().getNoteNumber()==60&&event.samplePosition>0),"outgoing loop never retriggers just before a fractional endpoint");
        }
        {
            Processor p(false);configure(p);
            auto late=render(p,.95);require(p.midiAdvanced==0&&noteAt(late,60,true,50),"late preparation repeats a full pass");
            require(prepared(p),"late successor completes");auto next=render(p,1.95);
            require(p.midiAdvanced==1&&noteAt(next,67,true,50),"late successor waits until the following endpoint");
        }
        {
            Processor p(false);configure(p);require(prepared(p),"prepare cancellation fixture");
            p.invalidateMidiAdvanceBrowser();p.setMidiAdvanceBrowserOrder({first,missing});
            require(prepared(p),"prepare replacement browser order");render(p,.95);
            require(p.midiAdvancePaused&&p.sequence.path==first.getFullPathName()&&!p.midiAdvanced,"missing successor stops without using stale work");
            require(p.loadSequence(third),"manual load after stop");
            require(!p.midiAdvancePaused&&p.sequence.path==third.getFullPathName(),"manual load clears transient stop");
        }
        {
            Processor p(false);configure(p);p.autoSave=true;p.keyLocked=true;p.targetKey=5;
            p.sequence.notes[0].velocity=83;p.sequence.changed();
            require(prepared(p),"dirty source staged after queuing autosave");render(p,.95);
            require(wait([&]{return p.sequenceSavesBusy==0;}),"outgoing autosave completes");
            SequenceClip saved;require(saved.load(first)&&saved.notes[0].pitch==60&&saved.notes[0].velocity==83,"autosave retains original notes without playback transposition");
        }
        {
            struct Head:juce::AudioPlayHead{double beat=79.95;juce::Optional<PositionInfo> getPosition()const override{
                PositionInfo info;info.setPpqPosition(beat);info.setBpm(120);info.setIsPlaying(true);info.setIsLooping(true);info.setLoopPoints(juce::AudioPlayHead::LoopPoints{72.,80.});return info;
            }} head;
            auto longClip=make("04_long_C",60,2,8);
            Processor p(false);configure(p);p.setPlayHead(&head);require(p.loadSequence(longClip),"load wrap fixture");p.setMidiAdvanceBrowserOrder({longClip,second});
            require(prepared(p),"prepare before arrangement wrap");
            juce::AudioBuffer<float> audio(2,100);juce::MidiBuffer midi;p.processBlock(audio,midi);
            require(noteAt(midi,67,true,50)&&p.midiAdvanced==1,"arrangement endpoint handoff");
            head.beat=72.05;midi.clear();p.processBlock(audio,midi);
            for(auto e:midi)require(!e.getMessage().isNoteOn(),"arrangement wrap does not duplicate the onset");
        }
        {
            Processor p(false);configure(p);p.sequence.path.clear();p.resetMidiAdvance();p.serviceMidiAdvance();
            render(p,.95);require(!p.midiAdvanced&&!p.midiAdvancePaused,"unsaved clips keep their ordinary loop behavior");
        }
        std::cout<<"MIDI Auto Continue: own loop bounds, exact in-block handoff, Lock Sequence, instrument preservation, MIDI Out, late preparation, stop/missing/cancellation, autosave and arrangement wraps passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"MIDI Auto Continue failed: "<<error.what()<<"\n";return 1;}
}
