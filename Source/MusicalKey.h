#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
// -2: no override/detection; -1: explicitly unknown. Quality -1 preserves legacy root-only metadata.
struct MusicalKey {
 int root=-2,quality=-1;
 bool known()const{return root>=0;}bool minor()const{return quality==1;}
 bool operator==(const MusicalKey& x)const{return root==x.root&&quality==x.quality;}
 bool operator!=(const MusicalKey& x)const{return !(*this==x);}
 static MusicalKey filename(juce::String path){auto name=juce::File(path).getFileNameWithoutExtension().replace(juce::String::charToString(0x266f),"#").replace(juce::String::charToString(0x266d),"b");juce::StringArray tokens;tokens.addTokens(name,"_ -()[]{}.,","");for(int i=tokens.size();--i>=0;){auto token=tokens[i];if(token.isEmpty())continue;auto letter=token.substring(0,1).toUpperCase();int root=letter=="C"?0:letter=="D"?2:letter=="E"?4:letter=="F"?5:letter=="G"?7:letter=="A"?9:letter=="B"?11:-1;if(root<0)continue;auto suffix=token.substring(1);if(suffix.startsWith("#")){++root;suffix=suffix.substring(1);}else if(suffix.startsWith("b")){--root;suffix=suffix.substring(1);}while(suffix.isNotEmpty()&&juce::CharacterFunctions::isDigit(suffix.getLastCharacter()))suffix=suffix.dropLastCharacters(1);suffix=suffix.toLowerCase();if(suffix.isNotEmpty()&&suffix!="m"&&suffix!="min"&&suffix!="minor"&&suffix!="maj"&&suffix!="major")continue;if(token.length()==1&&token!=letter)continue;bool minor=suffix=="m"||suffix=="min"||suffix=="minor";if(suffix.isEmpty()&&i+1<tokens.size()){auto next=tokens[i+1].toLowerCase();minor=next=="minor"||next=="min"||next=="m";}return {(root+12)%12,minor?1:0};}return {};}
 juce::var value()const{auto* o=new juce::DynamicObject();o->setProperty("root",root);o->setProperty("quality",quality);return juce::var(o);}
 static MusicalKey read(juce::var value){if(!value.isObject())return {};return {juce::jlimit(-1,11,(int)value.getProperty("root",-1)),juce::jlimit(-1,1,(int)value.getProperty("quality",-1))};}
 juce::String label()const{if(root<0)return juce::String::charToString(0x2014);static const char* names[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};return juce::String(names[root])+(minor()?"m":"");}
};
