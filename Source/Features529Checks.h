int runFeatures529() {
    try {
        juce::ScopedJuceInitialiser_GUI gui;
        auto require = [](bool ok, const char* reason) {
            if (!ok) throw std::runtime_error(reason);
        };
        auto dir = LibraryStore::root().getChildFile("continue-fixture");
        require(dir.createDirectory().wasOk(), "create audio fixtures");
        auto waveFile = [&](const juce::String& name, float level, int frames = 16000) {
            auto file = dir.getChildFile(name);
            juce::WavAudioFormat wav;
            auto stream = file.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.release(), 8000, 1, 16, {}, 0));
            require(writer != nullptr, "open audio fixture");
            juce::AudioBuffer<float> samples(1, frames);
            for (int i = 0; i < samples.getNumSamples(); ++i) samples.setSample(0, i, level * std::sin(i * .2f));
            require(writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()), "write audio fixture");
            return file;
        };
        auto slice = waveFile("01_slice_120bpm.wav", .2f);
        auto stretch = waveFile("02_stretch_120bpm.wav", .3f);
        auto warp = waveFile("03_warp_120bpm.wav", .4f);
        auto shot = waveFile("04_one_shot.wav", .5f);
        auto track = waveFile("05_track_120bpm.wav", .3f);
        LibraryStore::setPlaybackType({stretch}, 4);
        LibraryStore::setPlaybackType({warp}, 5);
        LibraryStore::setPlaybackType({shot}, 1);
        LibraryStore::setPlaybackType({track}, 2);

        struct Head : juce::AudioPlayHead {
            double beat = 0;
            bool cycle = false;
            juce::Optional<PositionInfo> getPosition() const override {
                PositionInfo info;
                info.setPpqPosition(beat);
                info.setBpm(120);
                info.setIsPlaying(true);
                if (cycle) {
                    info.setIsLooping(true);
                    info.setLoopPoints(juce::AudioPlayHead::LoopPoints{72., 80.});
                    info.setPpqPositionOfLastBarStart(beat < 76. ? 72. : 76.);
                }
                return info;
            }
        } head;
        auto waitFor = [](auto predicate) {
            for (int i = 0; i < 1000; ++i) {
                if (predicate()) return true;
                juce::Thread::sleep(5);
            }
            return false;
        };
        for (int mode : {0, 1, 2, 4, 6}) {
            Processor p(false);
            p.autoSave = false;
            p.trackOffline = true;
            p.trackStemsEnabled = false;
            p.prepareToPlay(8000, 128);
            p.setPlayHead(&head);
            p.hostBpm = 120;
            p.hasHost = true;
            auto file = mode == 1 ? stretch : mode == 2 ? warp : mode == 4 ? track : slice;
            require(p.load(file), "load first audio");
            p.setPlaybackMode(mode == 6 ? 0 : mode);
            p.melodicSliceMode = mode == 6;
            p.setPlayMode(true);
            if (mode == 4) {
                p.trackGrid.straight = true;
                p.trackGrid.bpm = 120;
                p.trackGrid.straightAnchor = {0, 0};
                p.trackGrid.rebuild();
                p.trackPreparedAudio = p.audio;
                p.trackGridReady = p.trackGridEdited = true;
                p.trackVinyl = true;
            }
            p.trackOffline = false;
            p.serviceTrack();
            if (p.stretchMode && !p.trackMode)
                require(waitFor([&]{auto ready=p.stretchWorker.result();return ready && ready->request==p.stretchRequest();}), "stretch ready");

            juce::AudioBuffer<float> block(2, 128);
            juce::MidiBuffer midi;
            auto render = [&] {
                p.processBlock(block, midi);
                head.beat += 128. / 4000.;
            };
            head.beat = 0;
            for (int i = 0; i < 150; ++i) render();
            require(p.trackCuePaused == (mode == 4), "off stops Track and loops other modes");
            if (mode != 4) require(block.getMagnitude(0, 128) > 0, "off renders a second pass");

            if (mode != 4) p.setPlayMode(false);
            p.setPlayMode(true);
            p.setAutoContinue(true);
            auto next = mode == 1 ? warp : mode == 2 ? track : stretch;
            p.setAdvanceBrowserOrder({file, shot, next});
            if (mode == 4) {
                p.setTrackLoop(true, 1, 2);
                p.trackPlayer.seek = 1;
                p.advanceNeedsStart = false;
            }
            head.beat = .012;
            render();
            require(waitFor([&]{
                p.serviceTrackAdvance();
                const juce::ScopedLock guard(p.lock);
                return p.advanceReady && p.advanceReady->renderer;
            }), "successor staged before the boundary");
            for (int i = 0; i < 150 && !p.trackCuePaused; ++i) render();
            require(p.trackCuePaused && p.advanceBridge.load(), "hands off inside the boundary block");
            require(block.getMagnitude(80, 48) > .01f, "successor audio is present in the boundary block");
            if (mode == 4) {
                if (std::abs(p.advanceEndBeat.load() - 1.012) >= .001)
                    std::cerr << "Track boundary was " << p.advanceEndBeat.load() << ", expected 1.012\n";
                require(std::abs(p.advanceEndBeat.load() - 1.012) < .001, "Track advances at the selected loop end");
            }
            require(waitFor([&]{p.serviceTrackAdvance();return p.autoAdvanced.load() == 1;}), "adopts successor with editor closed");
            require(juce::File(p.getAudio()->path) == next, "browser order skips One Shot");
            require(p.playbackMode() == (mode == 1 ? 2 : mode == 2 ? 4 : 1), "adopts the successor's playback mode");
            for (int i = 0; i < 50 && p.advanceBridge.load(); ++i) {
                render();
                p.serviceTrack();
                juce::Thread::sleep(2);
            }
            require(!p.advanceBridge.load(), "prepared audio transfers to the group renderer");
            require(waitFor([&]{
                p.serviceTrackAdvance();
                const juce::ScopedLock guard(p.lock);
                return p.advanceReady && p.advanceSource == p.audio && !p.advanceReady->renderer;
            }), "end of filtered order is staged");
            for (int i = 0; i < 400 && !p.trackCuePaused; ++i) { render(); p.serviceTrackAdvance(); }
            require(p.trackCuePaused && p.autoAdvanced == 1, "last row stops");
            render();
            require(block.getMagnitude(0, 128) == 0, "no outgoing repeat after stop");

            auto project = p.configurationXml(true);
            require(project->getBoolAttribute("autoContinue") && !p.configurationXml(false)->hasAttribute("autoContinue"), "project-only toggle");
            project->removeAttribute("autoContinue");
            project->setAttribute("trackAutoCue", true);
            require(p.applyConfiguration(*project, true) && !p.autoContinue, "legacy cue-on becomes continue-off");
            project->setAttribute("trackAutoCue", false);
            require(p.applyConfiguration(*project, true) && p.autoContinue, "legacy cue-off preserves continuous playback");
        }

        {
            Processor p(false);
            p.autoSave = false;
            p.prepareToPlay(8000, 128);
            p.setPlayHead(&head);
            require(p.load(slice), "load for invalidation checks");
            p.setPlayMode(true);
            p.setAutoContinue(true);
            p.setTrim(0, 8000);
            const auto original = p.getAudio();
            const auto passEnd = p.loopBeats();
            auto missing = dir.getChildFile("missing_120bpm.wav");
            juce::AudioBuffer<float> block(2, 128);
            juce::MidiBuffer midi;
            auto render = [&] {
                p.processBlock(block, midi);
                head.beat += 128. / 4000.;
            };
            auto prepared = [&] {
                p.serviceTrackAdvance();
                const juce::ScopedLock guard(p.lock);
                return p.advanceReady != nullptr;
            };
            head.beat = 0;
            p.setAdvanceBrowserOrder({slice, stretch, warp});
            render();
            require(waitFor(prepared), "prepare initial browser successor");
            p.invalidateAdvanceBrowser();
            p.setAdvanceBrowserOrder({slice, missing, warp});
            require(waitFor([&]{p.serviceTrackAdvance();const juce::ScopedLock guard(p.lock);return p.advanceReady && !p.advanceReady->renderer;}), "stage missing successor");
            for (int i = 0; i < 150 && !p.trackCuePaused; ++i) { render(); p.serviceTrackAdvance(); }
            require(p.trackCuePaused && std::abs(p.advanceEndBeat.load() - passEnd) < .00001,
                "trimmed pass ends at its musical boundary");
            require(waitFor([&]{p.serviceTrackAdvance();return !p.trackReachedEnd;}), "missing successor resolves");
            require(p.getAudio() == original && p.autoAdvanced == 0, "missing file stops without using a stale successor");

            p.setPlayMode(true);
            p.setAdvanceBrowserOrder({slice, stretch, warp});
            head.beat = 0;
            render();
            require(waitFor(prepared), "prepare before manual load");
            require(p.load(warp), "manual load overrides prepared successor");
            p.serviceTrackAdvance();
            require(juce::File(p.getAudio()->path) == warp && p.autoAdvanced == 0, "manual load cancels queued advance");
        }
        {
            Processor p(false);
            p.autoSave = false;
            p.prepareToPlay(8000, 128);
            p.setPlayHead(&head);
            require(p.load(slice), "load for precedence checks");
            p.setPlayMode(true);
            p.setAutoContinue(true);
            p.setAdvanceBrowserOrder({slice, stretch});
            p.midiGateEnabled = true;
            juce::AudioBuffer<float> block(2, 128);
            juce::MidiBuffer midi;
            auto renderPasses = [&] {
                head.beat = 0;
                for (int i = 0; i < 300; ++i) {
                    p.processBlock(block, midi);
                    p.serviceTrackAdvance();
                    head.beat += 128. / 4000.;
                }
            };
            p.stretchAnchorEnabled = true;
            renderPasses();
            require(!p.trackCuePaused && p.autoAdvanced == 0, "FX Anchor takes precedence with MIDI Gate");
            p.stretchAnchorEnabled = false;
            p.sequenceEnabled = true;
            renderPasses();
            require(!p.trackCuePaused && p.autoAdvanced == 0, "sequencer takes precedence");
        }

        {
            auto first = waveFile("06_long_stretch_120bpm.wav", .2f, 64000);
            auto second = waveFile("07_long_stretch_120bpm.wav", .3f, 64000);
            auto third = waveFile("08_long_slice_120bpm.wav", .4f, 64000);
            LibraryStore::setPlaybackType({first, second}, 4);
            Processor p(false);
            p.autoSave = false;
            p.prepareToPlay(8000, 128);
            p.setPlayHead(&head);
            require(p.load(first), "load four-bar source for Bar Lock");
            p.setTrim(16000, 48000);
            p.setLoopLock(true);
            p.setLoopBars(2);
            p.setPlayMode(true);
            p.setAutoContinue(true);
            p.setAdvanceBrowserOrder({first, second, third});
            juce::AudioBuffer<float> block(2, 128);
            juce::MidiBuffer midi;
            auto waitForRender = [&] {
                return waitFor([&]{auto ready=p.stretchWorker.result();return ready && ready->request==p.stretchRequest();});
            };
            p.stretchWorker.request(p.stretchRequest());
            require(waitForRender(), "first locked loop render ready");

            // Live cycles bars 19–20, so the next block after beat 79.968
            // arrives at 72 rather than at the sample loop's endpoint of 80.
            head.cycle = true;
            head.beat = 72.;
            auto render = [&] {
                p.processBlock(block, midi);
                p.serviceTrackAdvance();
                head.beat += .032;
                if (head.beat >= 80. - 1e-8) head.beat -= 8.;
            };
            auto staged = [&] {
                return waitFor([&]{
                    p.serviceTrackAdvance();
                    const juce::ScopedLock guard(p.lock);
                    return p.advanceReady && p.advanceReady->renderer && p.advanceSource == p.audio && !p.advanceBridge.load();
                });
            };
            // Do not service preparation until the first pass has already wrapped.
            for (int i = 0; i <= 250; ++i) {
                p.processBlock(block, midi);
                head.beat += .032;
                if (head.beat >= 80. - 1e-8) head.beat -= 8.;
            }
            require(!p.trackCuePaused && !p.autoAdvanced && block.getMagnitude(48, 80) > .1f,
                "late successor repeats the outgoing loop without silence");
            require(staged(), "stage successor during the repeated pass");
            auto verifyPass = [&](double end, uint64_t count, const juce::File& next, float minimum) {
                for (int i = 0; i < 251 && !p.advanceBridge.load(); ++i) render();
                require(p.advanceBridge.load() && std::abs(p.advanceEndBeat.load() - end) < .001,
                    "locked loop advances at its exact endpoint across arrangement wraps");
                require(block.getMagnitude(48, 80) > minimum, "successor sounds immediately after the endpoint");
                if (count == 1) {
                    const auto& incoming = p.advanceBridge.load()->stretch->buffer;
                    for (int frame = 40; frame < 128; ++frame)
                        require(std::abs(block.getSample(0, frame) - incoming.getSample(0, frame+128)) < .0002f,
                            "late post-seam samples match the incoming project phase");
                }
                require(waitFor([&]{p.serviceTrackAdvance();return p.autoAdvanced == count;}), "locked loop adopts the next file");
                require(juce::File(p.getAudio()->path) == next && p.loopLengthLocked
                    && p.regionStart() == 16000 && std::abs(p.loopBeats() - 8.) < .001, "successor retains the locked loop region");
                render();
                require(!p.advanceBridge.load() && block.getMagnitude(0, 128) > .08f, "transfer continues without restarting the pass");
            };
            verifyPass(80.032, 1, second, .24f);
            require(staged(), "stage the third file");
            verifyPass(88., 2, third, .28f);
            head.cycle = false;
        }

        {
            Processor p(false);
            p.autoSave = false;
            p.prepareToPlay(8000, 128);
            p.setPlayHead(&head);
            require(p.load(track), "load unlooped Track");
            p.trackStemsEnabled = false;
            p.trackVinyl = true;
            p.trackGrid.straight = true;
            p.trackGrid.bpm = 120;
            p.trackGrid.straightAnchor = {0, 0};
            p.trackGrid.rebuild();
            p.trackPreparedAudio = p.audio;
            p.trackGridReady = p.trackGridEdited = true;
            p.serviceTrack();
            p.setAutoContinue(true);
            p.setAdvanceBrowserOrder({track, stretch});
            p.trackPlayer.seek = 0;
            head.beat = 0;
            juce::AudioBuffer<float> block(2, 128);
            juce::MidiBuffer midi;
            auto render = [&] {p.processBlock(block, midi);head.beat += .032;};
            for (int i = 0; i < 150 && !p.trackCuePaused; ++i) render();
            require(p.trackCuePaused && p.trackReachedEnd && !p.advanceBridge.load(), "unlooped Track waits at the end");
            for (int i = 0; i < 5; ++i) render();
            require(block.getMagnitude(0, 128) == 0, "unlooped Track does not repeat while waiting");
            require(waitFor([&]{
                p.serviceTrackAdvance();
                const juce::ScopedLock guard(p.lock);
                return p.advanceReady && p.advanceReady->renderer;
            }), "late Track successor ready");
            require(std::abs(std::remainder(head.beat, 4.)) > .05, "Track test resumes between project bars");
            render();
            require(p.advanceBridge.load() && block.getMagnitude(48, 80) > .15f, "late Track successor starts immediately");
        }

        RackProcessor rack;
        for (int i = 0; i < 8; ++i) {
            rack.loops[i]->trackOffline = true;
            rack.loops[i]->autoSave = false;
            rack.loops[i]->setPlaybackMode(i % 6);
        }
        RackEditor editor(rack);
        for (bool browser : {false, true}) {
            rack.loops[rack.selected]->browserVisible = browser;
            for (auto size : {juce::Point<int>{browser ? 1000 : 620, 620}, juce::Point<int>{1440, 1210}})
                require(editor.fixedGroupSize529ForCheck(size), "window dimensions stay exact across group switches");
        }
        std::cout << "529 passed: fixed-size group switches, five Auto Continue modes, in-block seams, late preparation, arrangement wraps and project migration.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL 529: " << error.what() << "\n";
        return 1;
    }
}
