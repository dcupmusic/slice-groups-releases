#pragma once
#include <juce_core/juce_core.h>
namespace LalalCredentials {
juce::String read();
bool save(const juce::String&,juce::String& error);
bool remove(juce::String& error);
}
