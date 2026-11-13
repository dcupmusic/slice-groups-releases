// A group owns its last audio browser order, independent of the editor lifetime.
struct AdvanceHead final : juce::AudioPlayHead {
    PositionInfo position;
    juce::Optional<PositionInfo> getPosition() const override { return position; }
};
struct AdvanceReady {
    std::shared_ptr<const AudioData> audio;
    std::optional<TrackGrid> grid;
    std::shared_ptr<const StretchWorker::Result> stretch;
    std::unique_ptr<Processor> renderer;
    AdvanceHead head;
    uint64_t load = 0, browser = 0, config = 0;
    int masterKey = -1, numerator = 4, denominator = 4;
    double tempo = 0, rate = 0, origin = 0;
    bool adopted = false, rendered = false, failed = false;
    std::optional<std::pair<double, double>> trackRegion;
};
juce::ThreadPool advancePool{1, 0, juce::Thread::Priority::background};
std::atomic<bool> advanceCancelled{false}, trackReachedEnd{false}, advanceTransportPlaying{false};
std::atomic<bool> advanceInstalling{false}, advanceNeedsStart{false};
std::atomic<double> trackScheduledStart{std::numeric_limits<double>::quiet_NaN()};
std::atomic<double> advanceEndBeat{std::numeric_limits<double>::quiet_NaN()};
std::atomic<uint64_t> autoAdvanced{0}, advanceBrowserRevision{0};
std::shared_ptr<const juce::Array<juce::File>> advanceBrowserOrder;
std::shared_ptr<const AudioData> advanceSource;
std::shared_ptr<AdvanceReady> advanceReady;
// Only the audio thread clears the active pointer. The worker retains ownership
// until then, so a handoff never destroys an instrument on the audio thread.
std::atomic<AdvanceReady*> advanceBridge{nullptr};
bool advancePreview = false;
juce::MidiBuffer advanceMidi;
uint64_t advanceGeneration = 0, advancePreparedRevision = 0, advancePreparedConfig = 0;
double advancePreparedTempo = 0;
int advancePreparedMasterKey = -1, advancePreparedNumerator = 4, advancePreparedDenominator = 4;
double advancePreparedRate = 0;
bool advanceBusy = false, advanceBrowserPending = false;
uint64_t advanceTypeRevision = 0;
double advanceLoopOrigin = 0, advanceLoopEnd = 0, advanceExpectedBeat = 0, advanceLoopLength = 0;
bool advanceLoopArmed = false, advanceLate = false;
std::atomic<double> advanceLastStartCheck{std::numeric_limits<double>::quiet_NaN()};

// Audio-thread clock for Auto Continue. Keep elapsed project beats through a
// host cycle while leaving the raw playhead position available to MIDI and UI.
double advanceCycleOffset = 0;
double advanceExpectedRaw = std::numeric_limits<double>::quiet_NaN();
bool advanceHostWrapped = false;

double advanceClock(double raw, const juce::AudioPlayHead::PositionInfo& position,
                    double step, int frames) {
    advanceHostWrapped = false;
    if (!autoContinue || !position.getIsPlaying() || isNonRealtime()) {
        advanceLate=false;
        advanceCycleOffset = 0;
        advanceExpectedRaw = std::numeric_limits<double>::quiet_NaN();
        return raw;
    }

    if (std::abs(raw - advanceExpectedRaw) > 2 * step + 1e-6) {
        auto points = position.getLoopPoints();
        if (position.getIsLooping() && points && points->ppqEnd > points->ppqStart) {
            const double cycle = points->ppqEnd - points->ppqStart;
            // Hosts can quantise a wrap to the nearest processing block.
            const double tolerance = juce::jmin(cycle * .25, step * (frames + 2) + 1e-6);
            advanceHostWrapped = raw < advanceExpectedRaw
                && std::abs(raw - points->ppqStart) <= tolerance
                && std::abs(advanceExpectedRaw - points->ppqEnd) <= tolerance
                && std::abs(raw + cycle - advanceExpectedRaw) <= tolerance;
            if (advanceHostWrapped) advanceCycleOffset += advanceExpectedRaw - raw;
        }
        if (!advanceHostWrapped) {advanceCycleOffset = 0;advanceLate=false;}
    }
    advanceExpectedRaw = raw + frames * step;
    return raw + advanceCycleOffset;
}

void stopAutoContinue() {
    advanceCancelled = true;
    advancePool.removeAllJobs(false, -1);
}

void setAdvanceBrowserOrder(const juce::Array<juce::File>& order) {
    const juce::ScopedLock guard(lock);
    const auto typeRevision = LibraryStore::kindRevision("playback-types.json").load();
    if (!advanceBrowserPending && advanceTypeRevision == typeRevision
        && advanceBrowserOrder && *advanceBrowserOrder == order) return;
    advanceBrowserPending = false;
    advanceTypeRevision = typeRevision;
    advanceBrowserOrder = std::make_shared<const juce::Array<juce::File>>(order);
    ++advanceBrowserRevision;
}
void invalidateAdvanceBrowser() {
    const juce::ScopedLock guard(lock);
    if (advanceBrowserPending) return;
    advanceBrowserPending = true;
    ++advanceBrowserRevision;
}
void setAutoContinue(bool enabled) {
    const juce::ScopedLock guard(lock);
    if (autoContinue == enabled) return;
    autoContinue = enabled;
    advanceLate=false;
    resetMidiAdvance();
    ++advanceBrowserRevision;
    trackReachedEnd = false;
    advanceLoopArmed = false;
    timelineDirty = true;
    if (!trackMode) trackCuePaused = false;
    edited(false);
}
void resetAdvancePlayback(bool successor = false) {
    advanceLate=false;
    trackReachedEnd = false;
    trackCuePaused = successor;
    advanceNeedsStart = false;
    advanceLoopArmed = false;
    advanceLoopOrigin = 0;
    advanceLastStartCheck = std::numeric_limits<double>::quiet_NaN();
    if (!successor) advanceEndBeat = std::numeric_limits<double>::quiet_NaN();
    trackScheduledStart = std::numeric_limits<double>::quiet_NaN();
}
bool autoContinueEligible() const {
    if (trackMode) return trackPlaybackAllowed;
    if (oneShotMode || sequenceEnabled) return false;
    if (stretchAnchorEnabled && !warpMode) return false;
    return !extractionPreviewActive && !(extractionActive && extractionMidiAudition);
}
static double advanceBar(double beat, double meter, double origin) {
    return origin + std::ceil((beat - origin) / meter - 1e-8) * meter;
}
// Called at the first playable block. A late preparation waits for a later bar.
double advanceStartForBlock(double ppq, double step, double meter, double barOrigin) {
    double target = trackScheduledStart;
    if (advanceNeedsStart.exchange(false)) {
        double end = advanceEndBeat;
        target = advanceBar(std::isfinite(end) && end <= ppq + step ? juce::jmax(ppq - step, end) : ppq - step, meter, barOrigin);
        trackScheduledStart = target;
    } else if (std::isfinite(target) && (ppq > target + step || ppq < advanceLastStartCheck - step)) {
        target = advanceBar(ppq - step, meter, barOrigin);
        trackScheduledStart = target;
    }
    if (std::isfinite(target)) advanceLastStartCheck = ppq;
    return target;
}
void armAdvanceLoop(double ppq, double length, double step, int frames, double scheduledStart) {
    if (!advanceLoopArmed || advanceLoopLength != length || (!advanceHostWrapped && std::abs(ppq - advanceExpectedBeat) > 2 * step + 1e-6) || std::isfinite(scheduledStart)) {
        double first = std::isfinite(scheduledStart) ? juce::jmax(ppq, scheduledStart) : ppq;
        double phase = (first - advanceLoopOrigin) / length;
        advanceLoopLength = length;
        advanceLoopEnd = advanceLoopOrigin + (std::floor(phase + 1e-9) + 1) * length;
        advanceLoopArmed = true;
    }
    advanceExpectedBeat = ppq + frames * step;
}
void reachedAdvanceEnd(double beat) {
    advanceEndBeat = beat;
    trackCuePaused = true;
    trackReachedEnd = true;
}
int advanceMasterKey() const {
    return masterSequenceKey->enabled ? masterSequenceKey->key.load() : -1;
}
void retireAdvanceReady() {
    if (advanceReady)
        advancePool.addJob([retired = std::move(advanceReady)] {});
}
enum class AdvanceBoundary { pending, stop, ready };
AdvanceBoundary advanceBoundary(bool locked) const {
    if (!locked || !advanceReady || advanceReady->load != loadGeneration
        || advanceReady->browser != advanceBrowserRevision
        || advanceReady->config != configRevision || advanceReady->masterKey != advanceMasterKey()
        || advanceReady->numerator != hostNumerator || advanceReady->denominator != hostDenominator
        || advanceReady->rate != outputRate || std::abs(advanceReady->tempo - hostBpm) >= .001)
        return AdvanceBoundary::pending;
    return advanceReady->renderer ? AdvanceBoundary::ready : AdvanceBoundary::stop;
}
void renderAdvance(AdvanceReady& ready, juce::AudioBuffer<float>& out, int frame,
                   const juce::AudioPlayHead::PositionInfo& position, double beat) {
    float* channels[32];
    const int count = juce::jmin(32, out.getNumChannels());
    for (int ch = 0; ch < count; ++ch) channels[ch] = out.getWritePointer(ch) + frame;
    juce::AudioBuffer<float> part(channels, count, out.getNumSamples() - frame);
    ready.head.position = position;
    ready.head.position.setPpqPosition(beat);
    advanceMidi.clear();
    ready.renderer->processBlock(part, advanceMidi);
    ready.rendered = true;
}
bool handoffAdvance(juce::AudioBuffer<float>& out, int frame,
                    const juce::AudioPlayHead::PositionInfo& position, double beat, bool locked) {
    auto result = advanceBoundary(locked);
    if (result == AdvanceBoundary::pending) {advanceLate=true;return false;}
    reachedAdvanceEnd(beat);
    if (result == AdvanceBoundary::stop) trackReachedEnd = false;
    if (result == AdvanceBoundary::ready) {
        auto& ready = *advanceReady;
        ready.origin = advanceLate ? 0. : beat;
        ready.renderer->advanceLoopOrigin = ready.origin;
        if(advanceLate&&ready.renderer->trackMode){auto session=std::atomic_load(&ready.renderer->trackPlayer.session);if(session&&session->loop){double length=session->loopEnd-session->loopBegin;if(length>0)ready.renderer->trackPlayer.queueSeek(session->loopBegin+std::fmod(std::fmod(beat,length)+length,length),false);}else ready.renderer->trackPlayer.queueSeek((session?session->firstBeat:0.)+std::fmod(std::fmod(beat,4.)+4.,4.),false);}
        advanceBridge.store(&ready);
        renderAdvance(ready, out, frame, position, beat);
    }
    return true;
}
bool finishAdvanceBridge(AdvanceReady& ready) {
    if (!ready.adopted || !ready.rendered) return false;
    auto& incoming = *ready.renderer;
    if (trackMode) {
        auto session = std::atomic_load(&trackPlayer.session);
        if (!session || session.get() != trackPlayer.audioPublished.load() || session->audio != audio || session->awaitingPitchPreserved) return false;
        trackPlayer.adoptAdvancePosition(incoming.trackPlayer, *session);
    } else if (stretchMode) {
        stretchFade = incoming.stretchFade;
        stretchBlend = incoming.stretchBlend;
        currentNormaliseGain = incoming.currentNormaliseGain;
    } else {
        incoming.copySliceRuntime(*this, false);
        timelineDirty = rhythmDirty = false;
    }
    advanceLoopOrigin = ready.origin;
    advanceLoopArmed = false;
    trackCuePaused = false;
    trackReachedEnd = false;
    return true;
}
void serviceTrackAdvance() {
    if (trackOffline || advanceCancelled || advancePreview) return;
    const juce::ScopedTryLock guard(lock);
    if (!guard.isLocked() || advanceBusy) return;
    if (auto* bridge = advanceBridge.load()) {
        if (bridge->adopted || bridge->failed || bridge->load != loadGeneration) return;
        auto ready = advanceReady;
        advanceBusy = advanceInstalling = true;
        advancePool.addJob([this, ready] {
            const auto serial = ready->load;
            const bool loaded = !advanceCancelled && installAudio(ready->audio, true, serial, false, true);
            const juce::ScopedLock guard(lock);
            advanceBusy = advanceInstalling = false;
            if (!loaded || loadGeneration != serial || audio != ready->audio) {
                ready->failed = true;
                return;
            }
            playMode = true;
            sequenceEnabled = false;
            if (trackMode && ready->grid) {
                trackGrid = *ready->grid;
                trackGridReady = trackGridEdited = true;
                trackPreparedAudio = audio;
                trackLoop = ready->trackRegion.has_value();
                if (trackLoop) {
                    trackLoopStart = ready->trackRegion->first;
                    trackLoopEnd = ready->trackRegion->second;
                }
                trackPlayer.hostTempo = ready->tempo;
                trackPlayer.queueSeek(trackLoop ? trackLoopStart : trackGrid.beatAt(0, audio->rate), false);
                trackPlayer.previewWorker.cachePrepared(ready->stretch);
                serviceTrack();
            } else if (stretchMode) {
                stretchWorker.cachePrepared(ready->stretch);
                stretchWorker.request(stretchRequest());
                playingStretch = ready->stretch;
                previousStretch.reset();
            } else {
                rebuildTimeline();
            }
            ready->adopted = true;
            ++autoAdvanced;
        });
        return;
    }
    if (!audio || !autoContinue) {
        advanceSource.reset();
        retireAdvanceReady();
        return;
    }

    auto source = audio;
    auto token = loadGeneration.load();
    auto revision = advanceBrowserRevision.load();
    const int masterKey = advanceMasterKey();
    if (advanceSource != source || advanceGeneration != token || advancePreparedRevision != revision
        || advancePreparedConfig != configRevision || advancePreparedMasterKey != masterKey
        || advancePreparedNumerator != hostNumerator || advancePreparedDenominator != hostDenominator
        || advancePreparedRate != outputRate || std::abs(advancePreparedTempo - hostBpm) >= .001) {
        advanceSource = source;
        advanceGeneration = token;
        advancePreparedRevision = revision;
        advancePreparedConfig = configRevision;
        advancePreparedTempo = hostBpm;
        advancePreparedMasterKey = masterKey;
        advancePreparedNumerator = hostNumerator;
        advancePreparedDenominator = hostDenominator;
        advancePreparedRate = outputRate;
        retireAdvanceReady();
    }
    if (advanceBrowserPending || !playMode || !autoContinueEligible()
        || !advanceTransportPlaying || isNonRealtime() || advanceReady) return;

    auto order = advanceBrowserOrder;
    auto cancelled = [this, token, revision] {
        return advanceCancelled || loadGeneration != token || advanceBrowserRevision != revision;
    };
    advanceBusy = true;
    double low = bpmRangeMin, high = bpmRangeMax, tempo = hostBpm, rate = outputRate;
    const int numerator = hostNumerator, denominator = hostDenominator;
    auto settings = configurationXml(true)->toString();
    auto revisionOfSettings = configRevision;
    const std::optional<std::pair<double, double>> previousTrackLoop = trackMode && trackLoop
        ? std::make_optional(std::make_pair(trackLoopStart, trackLoopEnd)) : std::nullopt;
    advancePool.addJob([this, source, order, cancelled, low, high, tempo, rate, settings, token,
                        revision, revisionOfSettings, previousTrackLoop, masterKey, numerator, denominator] {
        auto result = std::make_shared<AdvanceReady>();
        result->load = token;
        result->browser = revision;
        result->config = revisionOfSettings;
        result->tempo = tempo;
        result->masterKey = masterKey;
        result->numerator = numerator;
        result->denominator = denominator;
        result->rate = rate;
        int row = order ? order->indexOf(juce::File(source->path)) : -1;
        for (int i = row + 1; row >= 0 && i < order->size() && !cancelled(); ++i) {
            auto file = LibraryStore::resolveFile((*order)[i]);
            if (file.hasFileExtension("sgpreset;sfz;sgmulti;mid;midi")
                || !formats.findFormatForFileExtension(file.getFileExtension())) continue;
            int type = LibraryStore::playbackType(file);
            if (type == 1 || type == 6 || type == 7 || file.isDirectory()) continue;
            const auto modified = file.getLastModificationTime();
            const auto size = file.getSize();
            auto preview = std::make_unique<Processor>(false);
            preview->trackOffline = true;
            preview->autoSave = false;
            preview->audio = source;
            if (auto config = juce::parseXML(settings)) preview->applyConfiguration(*config, true);
            preview->masterSequenceKey->enabled = masterKey >= 0;
            if (masterKey >= 0) preview->masterSequenceKey->key = masterKey;
            preview->hostBpm = tempo;
            preview->hostNumerator = numerator;
            preview->hostDenominator = denominator;
            preview->hasHost = true;
            preview->bpmRangeMin = low;
            preview->bpmRangeMax = high;
            auto data = DecodedSampleCache::instance().get(file);
            if (!data) data = preview->readAudio(file, cancelled);
            if (!data || cancelled() || !preview->installAudio(data, true, 0, false)) break;
            if (preview->oneShotMode) continue;
            preview->isContentionEngine = preview->advancePreview = true;
            preview->autoContinue = preview->midiGateEnabled = false;
            preview->playMode = true;
            preview->sequenceEnabled = false;
            preview->setPlayHead(&result->head);
            preview->prepareToPlay(rate, 0);
            if (preview->trackMode) {
                auto grid = preview->trackGrid;
                if (!preview->trackGridReady) {
                    auto cache = LibraryStore::root().getChildFile("track-analysis-v3").getChildFile(data->contentHash + ".xml");
                    if (!TrackAnalyser::loadCache(cache, *data, low, high, grid))
                        grid = TrackAnalyser::analyse(*data, low, high, cancelled);
                    grid.autoChooseMode(data->rate);
                }
                const double first = grid.beatAt(0, data->rate);
                const double last = grid.beatAt(data->buffer.getNumSamples(), data->rate);
                if (preview->loopLengthLocked || previousTrackLoop) {
                    const double requested = preview->loopLengthLocked ? preview->lockedBars * preview->sourceBeatsPerBar
                        : previousTrackLoop->second - previousTrackLoop->first;
                    const double length = requested;
                    if (length > 0) {
                        const double begin = previousTrackLoop ? previousTrackLoop->first
                            : grid.beatAt(preview->regionStart(), data->rate);
                        const double start = juce::jlimit(first, juce::jmax(first,last - length), begin);
                        result->trackRegion = {start, start + length};
                    }
                }
                const double begin = result->trackRegion ? result->trackRegion->first : first;
                if (!preview->trackVinyl && !cancelled()) {
                    const int frames = data->buffer.getNumSamples();
                    StretchWorker::Request request{data, 0, frames, 0, (last - first) * 60 * data->rate / frames,
                        tempo, preview->pitchSemitones + preview->keyTranspose()};
                    result->stretch = StretchWorker::render(
                        TrackPlayer::playheadRequest(request, grid, grid.sampleAt(begin, data->rate)), cancelled, true);
                    if (!result->stretch) break;
                    preview->trackPlayer.previewWorker.cachePrepared(result->stretch);
                }
                preview->trackPlayer.queueSeek(begin, false);
                preview->trackPlayer.publish(data, grid, result->trackRegion.has_value(), begin,
                    result->trackRegion ? result->trackRegion->second : last, preview->normaliseGain(), tempo,
                    preview->pitchSemitones + preview->keyTranspose(), preview->trackStemsEnabled,
                    preview->trackStemGains(), {}, preview->trackVinyl, preview->trackStemHQ,4,
                    result->trackRegion&&result->trackRegion->second>last?juce::jmax(1.,std::round((last-first)/preview->sourceBeatsPerBar))*preview->sourceBeatsPerBar:0);
                auto session = std::atomic_load(&preview->trackPlayer.session);
                if (!session || session->awaitingPitchPreserved) break;
                result->grid = std::move(grid);
            } else if (preview->stretchMode) {
                result->stretch = StretchWorker::render(preview->stretchRequest(), cancelled);
                if (!result->stretch) break;
                preview->playingStretch = result->stretch;
            } else {
                preview->rebuildTimeline();
                preview->timelineDirty = preview->rhythmDirty = false;
            }
            if (!file.existsAsFile() || modified != file.getLastModificationTime() || size != file.getSize()) break;
            result->audio = data;
            result->renderer = std::move(preview);
            break;
        }
        const juce::ScopedLock lockGuard(lock);
        advanceBusy = false;
        if (!cancelled()) advanceReady = std::move(result);
    });
}
