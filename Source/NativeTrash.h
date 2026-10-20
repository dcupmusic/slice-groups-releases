#pragma once
#include <juce_core/juce_core.h>
namespace NativeTrash {
bool move(const juce::File& original,juce::File& location,juce::String& error);
bool restore(const juce::File& location,const juce::File& original,juce::String& error);
}
