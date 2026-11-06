#pragma once
#include <juce_cryptography/juce_cryptography.h>
// Read the small configuration header only: browser listing never decodes media.
struct PresetFile528 {
 static constexpr juce::int64 magic=0x5347504d30303031LL;
 static std::unique_ptr<juce::XmlElement> header(juce::File file,juce::String* error=nullptr){
  auto fail=[&](const char* message)->std::unique_ptr<juce::XmlElement>{if(error)*error=message;return {};};
  auto in=file.createInputStream();if(!in)return fail("Could not read preset file");auto size=in->getTotalLength();if(size<1)return fail("Preset file is empty");
  if(size>=8&&in->readInt64()==magic){if(size<80||in->readInt()!=1)return fail("Unsupported portable preset version");int length=in->readInt();if(length<=0||length>64*1024*1024||length>size-80)return fail("Invalid preset header length");char digest[65]{};if(in->read(digest,64)!=64)return fail("Incomplete preset header checksum");juce::MemoryBlock text((size_t)length);if(in->read(text.getData(),length)!=length)return fail("Incomplete preset header");if(juce::SHA256(text).toHexString()!=juce::String(digest))return fail("Preset header checksum failed");auto xml=juce::parseXML(juce::String::fromUTF8((const char*)text.getData(),length));if(!xml||xml->getIntAttribute("portablePresetVersion")!=1)return fail("Unsupported preset format");return xml;}
  if(size>64*1024*1024)return fail("Legacy preset configuration is too large");in->setPosition(0);auto xml=juce::parseXML(in->readEntireStreamAsString());if(!xml)return fail("Invalid legacy preset");return xml;
 }
};
