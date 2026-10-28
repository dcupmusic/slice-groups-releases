#pragma once
class LocalMultiEdits {
public:
 static juce::File location(juce::File source){auto path=source.getFullPathName();return LibraryStore::root().getChildFile("multi-edits").getChildFile(juce::SHA256(path.toRawUTF8(),path.getNumBytesAsUTF8()).toHexString()+".xml");}
 static std::unique_ptr<juce::XmlElement> load(juce::File source){auto x=juce::parseXML(location(source));if(!x||x->getStringAttribute("localSource")!=source.getFullPathName()||x->getStringAttribute("localStamp")!=juce::String(source.getLastModificationTime().toMilliseconds())||x->getStringAttribute("localSize")!=juce::String(source.getSize()))return {};return x;}
 static bool save(juce::File source,const juce::String& text){auto x=juce::parseXML(text);if(!x)return false;x->setAttribute("localSource",source.getFullPathName());x->setAttribute("localStamp",juce::String(source.getLastModificationTime().toMilliseconds()));x->setAttribute("localSize",juce::String(source.getSize()));auto file=location(source);if(!file.getParentDirectory().createDirectory())return false;juce::TemporaryFile temp(file);return temp.getFile().replaceWithText(x->toString())&&temp.overwriteTargetFileWithTemporary();}
};
