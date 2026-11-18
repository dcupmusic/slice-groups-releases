#pragma once
// Drag-out files are durable user media, never disposable cache entries.
namespace PermanentExports {
inline juce::File folder(){return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Slice Groups").getChildFile("Exports");}
inline juce::File destination(const juce::String& name,const juce::String& extension,const juce::File& root=folder()){
 if(!root.createDirectory().wasOk())return {};
 return root.getNonexistentChildFile(juce::File::createLegalFileName(name)+" "+juce::Uuid().toDashedString(),extension);
}
template<class Writer> bool write(const juce::File& destination,Writer writer){
 if(destination==juce::File())return false;
 juce::TemporaryFile temporary(destination);
 return writer(temporary.getFile())&&temporary.overwriteTargetFileWithTemporary();
}
}
