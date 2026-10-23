#pragma once
#include <juce_core/juce_core.h>
// macOS notifications only; no polling or audio-thread work.
class LibraryWatch {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 LibraryWatch();~LibraryWatch();
 void setRoots(const juce::Array<juce::File>&);
 juce::StringArray takeChanges();
};
