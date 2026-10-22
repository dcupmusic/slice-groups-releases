#pragma once
#include <stdio.h>
#include <errno.h>
// Background-only file and folder moves. Never overwrite a destination or modify a friend's library.
struct LibraryMove {
 static juce::Result perform(juce::Array<juce::File> sources,juce::File destination,juce::Array<juce::File>& moved){
  destination=LibraryCopy::canonical(destination);
  if(!destination.isDirectory()||!LibraryStore::inLibrary(destination)||!LibraryCopy::writableDestination(destination))return juce::Result::fail("Choose an editable library folder.");
  juce::Array<juce::File> selected;std::set<juce::String> reserved;
  juce::Array<juce::File> inputs;for(auto input:sources)inputs.addIfNotAlreadyThere(LibraryCopy::canonical(LibraryStore::resolveFile(input)));
  for(auto source:inputs){bool nested=false;for(auto other:inputs)if(other!=source&&source.isAChildOf(other))nested=true;if(nested)continue;
   if(!source.exists()||source.isSymbolicLink()||(!source.isDirectory()&&source.hasFileExtension("sfz")))return juce::Result::fail("Choose audio files or folders. Move an SFZ together with its containing folder.");
   if(source==destination||destination.isAChildOf(source))return juce::Result::fail("A folder cannot be moved into itself or its subfolders.");
   if(!LibraryCopy::writableDestination(source)||!LibraryStore::editable(source))return juce::Result::fail("Shared libraries are read-only. Use Copy to My Library instead.");
   if(source.isDirectory()&&source.getChildFile(".slicegroups").exists())return juce::Result::fail("Move shared library roots in Finder, then use Relocate Library.");
   for(auto library:LibraryStore::libraryRoots())if(library.isAChildOf(source)&&!LibraryCopy::writableDestination(library))return juce::Result::fail("The folder contains a read-only shared library.");
   if(source.getParentDirectory()==destination)continue;auto target=destination.getChildFile(source.getFileName());if(target.exists()||!reserved.insert(target.getFullPathName()).second)return juce::Result::fail("An item with that name already exists in the destination. Nothing was moved.");selected.add(source);
  }
  for(auto source:selected){auto target=destination.getChildFile(source.getFileName());int type=LibraryStore::playbackType(source);
   if(::renamex_np(source.getFullPathName().toRawUTF8(),target.getFullPathName().toRawUTF8(),RENAME_EXCL)!=0)return juce::Result::fail(errno==EXDEV?"Moving between drives is not supported. Use Copy to My Library instead.":"Could not move "+source.getFileName()+". "+juce::String(moved.size())+" files moved.");
   try{LibraryStore::database()->relocateLibrary(source,target,{},true,type);}catch(const std::exception& error){bool restored=::renamex_np(target.getFullPathName().toRawUTF8(),source.getFullPathName().toRawUTF8(),RENAME_EXCL)==0;return juce::Result::fail(restored?"Move rolled back: "+juce::String(error.what()):"Metadata move failed. Audio is at "+target.getFullPathName()+"; original path could not be restored.");}
   moved.add(target);
  }
  LibraryStore::pollDatabase();LibraryStore::notifySharing();return juce::Result::ok();
 }
};
