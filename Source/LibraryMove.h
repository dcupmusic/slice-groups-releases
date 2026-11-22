#pragma once
#include <stdio.h>
#include <errno.h>
// Background-only file and folder moves. Never overwrite a destination or modify a friend's library.
struct LibraryMove {
 static juce::File availableTarget(juce::File source,juce::File destination,std::set<juce::String>& reserved){auto name=source.getFileName();auto extension=source.isDirectory()?juce::String():source.getFileExtension();auto stem=source.isDirectory()?name:source.getFileNameWithoutExtension();int number=1;int suffix=stem.lastIndexOf(" (");if(suffix>=0&&stem.endsWithChar(')')){auto digits=stem.substring(suffix+2,stem.length()-1);if(digits.isNotEmpty()&&digits.containsOnly("0123456789")&&digits.getLargeIntValue()>0&&digits.getLargeIntValue()<INT_MAX-1){number=(int)digits.getLargeIntValue()+1;stem=stem.substring(0,suffix);}}
  auto candidate=destination.getChildFile(name);while(candidate.exists()||reserved.count(candidate.getFullPathName().toLowerCase())){if(number==INT_MAX)return {};candidate=destination.getChildFile(stem+" ("+juce::String(number++)+")"+extension);}reserved.insert(candidate.getFullPathName().toLowerCase());return candidate;
 }
 static juce::Result perform(juce::Array<juce::File> sources,juce::File destination,juce::Array<juce::File>& moved,juce::String renamed={},std::vector<std::pair<juce::File,juce::File>>* actual=nullptr){
  renamed=renamed.trim();if(!renamed.isEmpty()&&(sources.size()!=1||renamed=="."||renamed==".."||renamed.containsAnyOf("/\\:")))return juce::Result::fail("Enter a valid name.");
  destination=LibraryCopy::canonical(destination);
  if(!destination.isDirectory()||(!LibraryStore::inLibrary(destination)&&!(renamed.isNotEmpty()&&sources.size()==1&&LibraryStore::libraryRoots().contains(sources[0])&&sources[0].getParentDirectory()==destination))||!LibraryCopy::writableDestination(destination))return juce::Result::fail("Choose an editable library folder.");
  juce::Array<juce::File> selected,targets;std::set<juce::String> reserved;
  juce::Array<juce::File> inputs;for(auto input:sources)inputs.addIfNotAlreadyThere(LibraryCopy::canonical(LibraryStore::resolveFile(input)));
  for(auto source:inputs){bool nested=false;for(auto other:inputs)if(other!=source&&source.isAChildOf(other))nested=true;if(nested)continue;
   if(!source.exists()||source.isSymbolicLink()||(!source.isDirectory()&&source.hasFileExtension("sfz")&&(renamed.isEmpty()||source.getParentDirectory()!=destination)))return juce::Result::fail("Choose audio files or folders. Move an SFZ together with its containing folder.");
   if(source==destination||destination.isAChildOf(source))return juce::Result::fail("A folder cannot be moved into itself or its subfolders.");
   if(!LibraryCopy::writableDestination(source)||!LibraryStore::editable(source))return juce::Result::fail("Shared libraries are read-only. Use Copy to My Library instead.");
   if(source.isDirectory()&&source.getChildFile(".slicegroups").exists())return juce::Result::fail("Move shared library roots in Finder, then use Relocate Library.");
   for(auto library:LibraryStore::libraryRoots())if(library.isAChildOf(source)&&!LibraryCopy::writableDestination(library))return juce::Result::fail("The folder contains a read-only shared library.");
   if(source.getParentDirectory()==destination&&(renamed.isEmpty()||renamed==source.getFileName()))continue;auto target=renamed.isEmpty()?availableTarget(source,destination,reserved):destination.getChildFile(renamed);if(target==juce::File())return juce::Result::fail("Could not choose an unused destination name.");if(renamed.isNotEmpty()&&(target.exists()||!reserved.insert(target.getFullPathName().toLowerCase()).second))return juce::Result::fail("An item with that name already exists in the destination. Nothing was moved.");selected.add(source);targets.add(target);
  }
  for(auto source:selected)if(!LocalMultiEdits::indexFolder517(source))return juce::Result::fail("Could not preserve local instrument edits.");
  for(int index=0;index<selected.size();++index){auto source=selected[index],target=targets[index];int type=LibraryStore::playbackType(source);
   while(::renamex_np(source.getFullPathName().toRawUTF8(),target.getFullPathName().toRawUTF8(),RENAME_EXCL)!=0){int error=errno;if(renamed.isEmpty()&&(error==EEXIST||error==ENOTEMPTY)){target=availableTarget(source,destination,reserved);if(target!=juce::File())continue;}return juce::Result::fail(error==EXDEV?"Moving between drives is not supported. Use Copy to My Library instead.":"Could not move "+source.getFileName()+". "+juce::String(moved.size())+" files moved.");}
   try{LibraryStore::database()->relocateLibrary(source,target,{},true,type);}catch(const std::exception& error){bool restored=::renamex_np(target.getFullPathName().toRawUTF8(),source.getFullPathName().toRawUTF8(),RENAME_EXCL)==0;if(!restored){moved.add(target);if(actual)actual->emplace_back(source,target);}return juce::Result::fail(restored?"Move rolled back: "+juce::String(error.what()):"Metadata move failed. Audio is at "+target.getFullPathName()+"; original path could not be restored.");}
   moved.add(target);if(actual)actual->emplace_back(source,target);
  }
  LibraryStore::pollDatabase();LibraryStore::notifySharing();return juce::Result::ok();
 }
};
