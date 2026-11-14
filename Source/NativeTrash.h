#pragma once
#include <juce_core/juce_core.h>
namespace NativeTrash {
juce::int64 dateAdded(const juce::File&);
bool keyIsRepeat();
bool move(const juce::File& original,juce::File& location,juce::String& error);
bool restore(const juce::File& location,const juce::File& original,juce::String& error);
}
