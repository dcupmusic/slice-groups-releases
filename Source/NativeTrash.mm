#include "NativeTrash.h"
#import <Foundation/Foundation.h>
namespace NativeTrash {
static NSURL* url(const juce::File& file){return [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()]];}
bool move(const juce::File& original,juce::File& location,juce::String& message){@autoreleasepool {
 NSError* error=nil;NSURL* moved=nil;
 if(![[NSFileManager defaultManager] trashItemAtURL:url(original) resultingItemURL:&moved error:&error]||moved==nil){message=error?juce::String([[error localizedDescription] UTF8String]):"macOS did not return the Trash location.";return false;}
 location=juce::File(juce::String([[moved path] UTF8String]));return true;
}}
bool restore(const juce::File& location,const juce::File& original,juce::String& message){@autoreleasepool {
 if(original.exists()){message="Original location is occupied: "+original.getFullPathName();return false;}
 if(!location.exists()){message="Item is no longer in Trash: "+original.getFileName();return false;}
 auto result=original.getParentDirectory().createDirectory();if(!result){message=result.getErrorMessage();return false;}
 NSError* error=nil;
 // NSFileManager move refuses an existing destination, including a creation race.
 if(![[NSFileManager defaultManager] moveItemAtURL:url(location) toURL:url(original) error:&error]){message=error?juce::String([[error localizedDescription] UTF8String]):"Could not restore item.";return false;}return true;
}}
}
