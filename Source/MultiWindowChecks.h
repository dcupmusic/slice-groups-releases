#pragma once
int runMultiWindowChecks() {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryPreparation::checksPaused = true;
    auto require = [](bool value, const char* reason) { if (!value) throw std::runtime_error(reason); };
    try {
        auto root = LibraryStore::root().getChildFile("Multi-window library");
        require(root.createDirectory().wasOk(), "create catalogue fixture");
        LibraryStore::addLibraryRoot(root);
        juce::Array<juce::File> entries;
        for (int i = 0; i < 50000; ++i) entries.add(root.getChildFile("sample " + juce::String(i) + ".wav"));
        LibraryStore::database()->setCatalog(root, entries);
        std::vector<std::unique_ptr<RackProcessor>> processors;
        std::vector<std::unique_ptr<RackEditor>> editors;
        for (int count : {1, 4, 8}) {
            auto begin = juce::Time::getMillisecondCounterHiRes();
            while ((int)editors.size() < count) {
                auto processor = std::make_unique<RackProcessor>();
                for (auto& group : processor->loops) {group->autoSave = false;group->trackOffline = true;}
                auto editor = std::make_unique<RackEditor>(*processor);
                editor->setSize(1000, 760);
                editor->setVisible(true);
                processors.push_back(std::move(processor));
                editors.push_back(std::move(editor));
            }
            auto deadline = begin + 30000;
            bool complete = false;
            while (!complete && juce::Time::getMillisecondCounterHiRes() < deadline) {
                complete = true;
                for (auto& editor : editors) complete &= editor->activeEditorForCheck().browserRowsForCheck() == 50000;
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, .002, true);
            }
            require(complete, "each instance displays the full catalogue");
            const double opening = juce::Time::getMillisecondCounterHiRes() - begin;
            begin = juce::Time::getMillisecondCounterHiRes();
            for (int tick = 0; tick < 100; ++tick) for (auto& editor : editors) {
                editor->timerCallback();
                editor->activeEditorForCheck().timerCallback();
            }
            const double idle = (juce::Time::getMillisecondCounterHiRes() - begin) / 100;
            begin = juce::Time::getMillisecondCounterHiRes();
            for (auto& editor : editors) require(!editor->createComponentSnapshot(editor->getLocalBounds()).isNull(), "paint open window");
            std::cout << count << " windows: library ready " << opening << " ms; combined idle UI tick " << idle
                      << " ms; combined first paint " << juce::Time::getMillisecondCounterHiRes() - begin << " ms" << std::endl;
        }
        {
            auto& processor = *processors.front()->loops[0];
            auto& editor = editors.front()->activeEditorForCheck();
            auto audio = std::make_shared<AudioData>();
            audio->rate = 48000;audio->path = root.getChildFile("audition.wav").getFullPathName();
            audio->buffer.setSize(1, 96000);audio->buffer.clear();
            require(processor.installAudio(audio, false, 0, false), "load waveform for animation checks");
            editor.refresh();
            processor.voices[0].active = true;processor.voices[0].pos = 1200;
            editor.refreshPlaybackAnimation();
            require(editor.wave.state.voices[0].active && editor.wave.state.voices[0].pos == 1200, "fast playhead refresh preserves animation");
            processor.setPitch(3);
            editor.timerCallback();
            require(editor.pitch.getValue() == 3, "parameter edits update without waiting for idle refresh");
            processor.voices[0].active = false;
            editor.refreshPlaybackAnimation();
            require(!editor.wave.state.voices[0].active, "stopped voices clear their old playheads");
        }
        require(BrowserSearchIndex::preparedHits >= 7, "open instances reuse prepared catalogue data");
        const auto neverCancelled = [] {return false;};
        auto first = BrowserSearchIndex::prepared(root, {root}, neverCancelled);
        auto again = BrowserSearchIndex::prepared(root, {root}, neverCancelled);
        require(first && first == again && first->files.size() == 50000, "warm catalogue reuses one immutable view");
        LocalDatabase::CatalogDelta delta;
        auto added = root.getChildFile("new sample.wav");
        delta.added.push_back({added.getFullPathName(), root.getFullPathName(), added.getFileName(), false});
        LibraryStore::database()->applyCatalogDelta(delta);
        auto changed = BrowserSearchIndex::prepared(root, {root}, neverCancelled);
        require(changed && changed != first && changed->files.size() == 50001, "file changes invalidate shared catalogue");
        require(first->files.size() == 50000, "refresh preserves other windows' existing rows");
        LibraryStore::database()->invalidateCatalog(root);
        require(!BrowserSearchIndex::prepared(root, {root}, neverCancelled), "invalidated catalogue cannot return cached paths");
        std::cout << "Shared catalogue reuse, incremental invalidation and immutable open-window results passed\n";
        return 0;
    } catch (const std::exception& error) {std::cerr << "Multi-window check failed: " << error.what() << "\n";return 1;}
}
