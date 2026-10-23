#include "NativeTrash.h"
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
namespace NativeTrash {
bool keyIsRepeat(){NSEvent* event=[NSApp currentEvent];return event&&event.type==NSEventTypeKeyDown&&event.isARepeat;}
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

#include "LibraryWatch.h"
#import <CoreServices/CoreServices.h>
struct LibraryWatch::Impl {
 juce::CriticalSection mutex;juce::StringArray pending,roots;FSEventStreamRef stream=nullptr;dispatch_queue_t queue=dispatch_queue_create("audio.slicegroups.library-watch",DISPATCH_QUEUE_SERIAL);
 static void events(ConstFSEventStreamRef,void* context,size_t count,void* paths,const FSEventStreamEventFlags flags[],const FSEventStreamEventId[]){auto& self=*static_cast<Impl*>(context);auto strings=static_cast<char**>(paths);const juce::ScopedLock lock(self.mutex);for(size_t i=0;i<count;++i){juce::String path(strings[i]);if(path.contains("/.slicegroups/")||path.endsWith("/.slicegroups")||path.contains("/.DS_Store"))continue;self.pending.addIfNotAlreadyThere(path);if(flags[i]&(kFSEventStreamEventFlagMustScanSubDirs|kFSEventStreamEventFlagUserDropped|kFSEventStreamEventFlagKernelDropped))self.pending.addIfNotAlreadyThere("*");}}
 void stop(){if(stream){FSEventStreamStop(stream);FSEventStreamInvalidate(stream);dispatch_sync(queue,^{});FSEventStreamRelease(stream);stream=nullptr;}}
 ~Impl(){stop();dispatch_release(queue);}
};
LibraryWatch::LibraryWatch():impl(std::make_unique<Impl>()){}
LibraryWatch::~LibraryWatch()=default;
void LibraryWatch::setRoots(const juce::Array<juce::File>& files){juce::StringArray roots;for(auto file:files)roots.add(file.getFullPathName());if(roots==impl->roots)return;impl->stop();impl->roots=roots;if(roots.isEmpty())return;@autoreleasepool{NSMutableArray* paths=[NSMutableArray array];for(auto root:roots)[paths addObject:[NSString stringWithUTF8String:root.toRawUTF8()]];FSEventStreamContext context{0,impl.get(),nullptr,nullptr,nullptr};impl->stream=FSEventStreamCreate(nullptr,Impl::events,&context,(__bridge CFArrayRef)paths,kFSEventStreamEventIdSinceNow,.25,kFSEventStreamCreateFlagFileEvents|kFSEventStreamCreateFlagNoDefer);if(impl->stream){FSEventStreamSetDispatchQueue(impl->stream,impl->queue);if(!FSEventStreamStart(impl->stream)){FSEventStreamInvalidate(impl->stream);FSEventStreamRelease(impl->stream);impl->stream=nullptr;}}}}
juce::StringArray LibraryWatch::takeChanges(){const juce::ScopedLock lock(impl->mutex);juce::StringArray out;out.swapWith(impl->pending);return out;}
