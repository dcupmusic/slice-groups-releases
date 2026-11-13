// MIDI successors retain the current instrument. All members are protected by lock.
struct MidiAdvanceReady {
    SequenceClip clip, playback;
    juce::String sourcePath;
    uint64_t identity=0, revision=0, browser=0, config=0, generation=0;
    bool consumed=false, playable=false;
};
std::unique_ptr<juce::ThreadPool> midiAdvancePool;
std::atomic<bool> midiAdvanceCancelled{false};
std::shared_ptr<MidiAdvanceReady> midiAdvanceReady;
std::shared_ptr<const juce::Array<juce::File>> midiAdvanceOrder;
uint64_t midiAdvanceBrowser=0, midiAdvanceGeneration=0;
std::atomic<uint64_t> midiAdvanced{0};
bool midiAdvanceBusy=false, midiAdvanceBrowserPending=false, midiAdvancePaused=false;
bool midiAdvanceArmed=false, midiAdvanceWasEnabled=false;
double midiAdvanceEnd=0, sequenceOrigin=0;

void stopMidiAutoContinue() {
    midiAdvanceCancelled=true;
    if(midiAdvancePool)midiAdvancePool->removeAllJobs(false,-1);
}
void resetMidiAdvance() {
    ++midiAdvanceGeneration;
    midiAdvancePaused=midiAdvanceArmed=false;
    sequenceOrigin=0;
    sequenceWasPlaying=false;
}
void setMidiAdvanceBrowserOrder(const juce::Array<juce::File>& files) {
    const juce::ScopedLock guard(lock);
    if(!midiAdvanceBrowserPending&&midiAdvanceOrder&&*midiAdvanceOrder==files)return;
    midiAdvanceOrder=std::make_shared<const juce::Array<juce::File>>(files);
    midiAdvanceBrowserPending=false;
    ++midiAdvanceBrowser;
}
void invalidateMidiAdvanceBrowser() {
    const juce::ScopedLock guard(lock);
    if(!midiAdvanceBrowserPending){midiAdvanceBrowserPending=true;++midiAdvanceBrowser;}
}
bool midiAdvanceMatches(const MidiAdvanceReady& ready)const {
    return ready.sourcePath==sequence.path&&ready.identity==sequenceIdentity510&&ready.revision==sequence.revision
        &&ready.browser==midiAdvanceBrowser&&ready.config==configRevision
        &&ready.generation==midiAdvanceGeneration;
}
void retireMidiAdvance() {
    if(midiAdvanceReady&&midiAdvancePool)
        midiAdvancePool->addJob([retired=std::move(midiAdvanceReady)]{});
}
void serviceMidiAdvance() {
    if(midiAdvanceCancelled)return;
    const juce::ScopedTryLock guard(lock);
    if(!guard.isLocked()||midiAdvanceBusy)return;
    if(midiAdvanceReady&&(midiAdvanceReady->consumed||!midiAdvanceMatches(*midiAdvanceReady)))retireMidiAdvance();
    if(!autoContinue||!sequenceEnabled||sequence.path.isEmpty()||!SequenceClip::midi(juce::File(sequence.path))||!midiAdvanceOrder
        ||midiAdvanceBrowserPending||midiAdvanceReady||sequenceEditGesture||sequenceRecord||midiAdvancePaused)return;
    if(!midiAdvancePool)midiAdvancePool=std::make_unique<juce::ThreadPool>(1,0,juce::Thread::Priority::background);
    // Snapshot and queue a dirty outgoing file before making its successor available.
    queueSequenceAutoSave(true);
    auto ready=std::make_shared<MidiAdvanceReady>();
    ready->sourcePath=sequence.path;ready->identity=sequenceIdentity510;ready->revision=sequence.revision;
    ready->browser=midiAdvanceBrowser;ready->config=configRevision;ready->generation=midiAdvanceGeneration;
    auto order=midiAdvanceOrder;auto current=juce::File(sequence.path);
    const bool quantise=sequenceAutoQuantize,mono=extractionActive&&extractionOptions.monophonic;
    const double grid=sequenceSwingGrid;
    midiAdvanceBusy=true;
    midiAdvancePool->addJob([this,ready,order,current,quantise,mono,grid]{
        int row=order->indexOf(current);
        for(int i=row+1;row>=0&&i<order->size()&&!midiAdvanceCancelled;++i){
            auto file=LibraryStore::resolveFile((*order)[i]);
            if(!SequenceClip::midi(file))continue;
            auto modified=file.getLastModificationTime();auto size=file.getSize();
            if(!ready->clip.load(file))break;
            if(modified!=file.getLastModificationTime()||size!=file.getSize())break;
            ready->playback=ready->clip;
            if(quantise)for(auto& note:ready->playback.notes){
                if(note.beat<ready->playback.loopStart||note.beat>=ready->playback.loopEnd())continue;
                note.beat=juce::jlimit(ready->playback.loopStart,ready->playback.loopEnd()-.000001,SequenceClip::snapped(note.beat,grid,false));
                note.length=juce::jmin(note.length,ready->playback.loopEnd()-note.beat);
            }
            if(mono)ready->playback.makeMonophonic();
            ready->playable=true;
            break;
        }
        const juce::ScopedLock guard(lock);
        midiAdvanceBusy=false;
        if(!midiAdvanceCancelled&&midiAdvanceMatches(*ready))midiAdvanceReady=ready;
    });
}
void releaseSequenceAt(int sample) {
    for(int note=0;note<128;++note){
        if(sequenceHeld[note])addSequenceEvent(juce::MidiMessage::noteOff(16,note),sample);
        else if(sequenceExternalHeld[note])sequenceOutput.addEvent(juce::MidiMessage::noteOff(1,note),sample);
    }
    sequenceHeld.fill(false);sequenceExternalHeld.fill(false);
}
bool advanceMidiAt(double beat,int sample) {
    if(sequenceEditGesture||sequenceRecord||!midiAdvanceReady||!midiAdvanceMatches(*midiAdvanceReady))return false;
    releaseSequenceAt(sample);
    if(!midiAdvanceReady->playable){midiAdvancePaused=true;return true;}
    auto& ready=*midiAdvanceReady;
    const auto revision=sequence.revision+1;
    std::swap(sequence,ready.clip);
    std::swap(sequencePlayback,ready.playback);
    sequence.revision=sequencePlayback.revision=revision;
    ++sequenceIdentity510;
    ready.consumed=true;
    sequenceOrigin=beat;
    midiAdvanceEnd=beat+sequencePlayback.beats();
    ++midiAdvanced;
    return true;
}
