#pragma once
#include <deque>
#include <ctime>
// Explicit local beat-grid preparation. Never loads a sample into an engine or replaces manual edits.
class TrackPreanalysis final:public juce::Thread {
 struct Job{juce::File file;double low=80,high=160;};
 juce::CriticalSection mutex;juce::WaitableEvent wake;std::deque<Job> queue;std::set<juce::String> queued;juce::String status,lastFailure;std::atomic<uint64_t> revision{0};int total=0,completed=0,succeeded=0,failed=0;bool active=false;
 void report(juce::String text){const juce::ScopedLock g(mutex);status=std::move(text);++revision;}
 void enqueue(const Job& job){if(queued.insert(job.file.getFullPathName()).second){queue.push_back(job);++total;}}
 static double cpuTime(){timespec time{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&time)==0)return time.tv_sec*1000.+time.tv_nsec/1e6;return juce::Time::getMillisecondCounterHiRes();}
 struct HashStream final:juce::InputStream{
  juce::FileInputStream stream;std::function<bool()> cancel;std::function<void(float)> progress;
  HashStream(juce::File f,std::function<bool()> c,std::function<void(float)> p):stream(f),cancel(std::move(c)),progress(std::move(p)){}
  juce::int64 getTotalLength()override{return stream.getTotalLength();}bool isExhausted()override{return stream.isExhausted();}juce::int64 getPosition()override{return stream.getPosition();}bool setPosition(juce::int64 position)override{return stream.setPosition(position);}
  int read(void* data,int bytes)override{if(cancel())return 0;int count=stream.read(data,bytes);if(progress)progress((float)((double)getPosition()/juce::jmax((juce::int64)1,getTotalLength())));return count;}
 };
public:
 static TrackPreanalysis& instance(){static TrackPreanalysis worker;return worker;}
 TrackPreanalysis():Thread("Prepare track beat grids"){}~TrackPreanalysis()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 static bool supported(juce::File file){return file.hasFileExtension("wav;wave;aif;aiff;flac;mp3;ogg;oga");}
 // Main-thread requests only enqueue paths. Folder discovery, validation and all disk I/O stay on this worker.
 void request(const juce::Array<juce::File>& files,double low=80,double high=160){const juce::ScopedLock g(mutex);if(!active&&queue.empty()){total=completed=succeeded=failed=0;lastFailure.clear();}low=juce::jlimit(20.,399.,low);high=juce::jlimit(low+1,400.,high);for(auto file:files)enqueue({file,low,high});if(!active){status=queue.empty()?"No tracks selected.":"Track analysis queued: "+juce::String((int)queue.size())+" items";++revision;}if(!queue.empty()&&!isThreadRunning()&&!startThread(juce::Thread::Priority::low)){status="Could not start track analysis.";queue.clear();queued.clear();++revision;}wake.signal();}
 bool busy(){const juce::ScopedLock g(mutex);return active||!queue.empty();}
 uint64_t version()const{return revision.load();}juce::String statusText(){const juce::ScopedLock g(mutex);return status;}
 std::array<int,4> countsForCheck(){const juce::ScopedLock g(mutex);return {total,completed,succeeded,failed};}
 void run()override{
  while(!threadShouldExit()){
   Job job;{const juce::ScopedLock g(mutex);if(!queue.empty()){job=queue.front();queue.pop_front();active=true;}}
   if(job.file==juce::File()){wake.wait(100);continue;}
   if(job.file.isDirectory()){
    report("Finding audio files in "+job.file.getFileName()+"...");
    {const juce::ScopedLock g(mutex);--total;}
    for(auto entry:juce::RangedDirectoryIterator(job.file,true,"*",juce::File::findFiles|juce::File::ignoreHiddenFiles,juce::File::FollowSymlinks::no)){
     if(threadShouldExit())break;auto file=entry.getFile();if(supported(file)){const juce::ScopedLock g(mutex);enqueue({file,job.low,job.high});}
    }
    {const juce::ScopedLock g(mutex);queued.erase(job.file.getFullPathName());active=false;status=queue.empty()?"No audio files found.":"Track analysis queued: "+juce::String((int)queue.size())+" items";++revision;}continue;
   }
   bool ok=false;juce::String error;int lastPercent=-1;juce::String lastStage;
   auto progress=[&](float value,juce::String stage){int percent=juce::jlimit(0,100,(int)(value*100));if(percent==lastPercent&&stage==lastStage)return;lastPercent=percent;lastStage=stage;const juce::ScopedLock g(mutex);status=stage+" "+job.file.getFileName()+": "+juce::String(percent)+"% | "+juce::String(completed)+"/"+juce::String(total)+" complete";++revision;};
   double sliceStart=cpuTime();
   auto cancel=[&]{if(threadShouldExit())return true;
    // Budget actual CPU work, not time spent descheduled. Never throttle an idle host.
    double worked=cpuTime()-sliceStart;
    if(LibraryPreparation::playbackBusy()&&worked>=2){wake.wait(juce::jlimit(1,2,(int)std::ceil(worked*.5)));sliceStart=cpuTime();}
    else if(worked>=8){juce::Thread::yield();sliceStart=cpuTime();}
    return threadShouldExit();
   };
   progress(0,"Reading");
   try{
    auto file=LibraryStore::resolveFile(job.file);if(!supported(file))error="Unsupported audio format";
    else{
     juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
     if(!reader||reader->lengthInSamples<=0||reader->lengthInSamples>=INT_MAX)error="Could not read audio (the file may not be downloaded locally)";
     else{
      auto size=file.getSize();auto modified=file.getLastModificationTime();HashStream stream(file,cancel,[&](float v){progress(v*.1f,"Reading");});
      if(stream.stream.failedToOpen())error="Could not open the source file";
      else{
       auto hash=juce::SHA256(stream).toHexString();auto cache=LibraryStore::root().getChildFile("track-analysis-v3").getChildFile(hash+".xml");TrackGrid grid;
       if(!cancel())ok=TrackAnalyser::loadCacheForFrames(cache,(int)reader->lengthInSamples,job.low,job.high,grid);
       if(!ok&&!cancel()){
        AudioData audio;audio.path=file.getFullPathName();audio.contentHash=hash;audio.rate=reader->sampleRate;audio.buffer.setSize(juce::jmin(2,(int)reader->numChannels),(int)reader->lengthInSamples);bool read=true;
        for(int start=0;start<audio.buffer.getNumSamples()&&!cancel();start+=65536){int count=juce::jmin(65536,audio.buffer.getNumSamples()-start);if(!reader->read(&audio.buffer,start,count,start,true,true)){read=false;break;}progress(.1f+.2f*(start+count)/audio.buffer.getNumSamples(),"Reading");}
        if(read&&!cancel()){
         grid=TrackAnalyser::analyse(audio,job.low,job.high,cancel,[&](float value){progress(.3f+.7f*value,"Analysing");});
         if(!cancel()&&grid.anchors.size()>1&&file.getSize()==size&&file.getLastModificationTime()==modified){TrackAnalyser::saveCache(cache,job.low,job.high,grid);ok=TrackAnalyser::loadCacheForFrames(cache,(int)reader->lengthInSamples,job.low,job.high,grid);if(!ok)error="Could not save the analysis cache";}
         else if(!threadShouldExit())error="No usable beat grid, or source file changed during analysis";
        }else if(!threadShouldExit())error="Could not finish reading the audio";
       }
       if(ok&&!cancel())TrackAnalyser::markPrepared(file,hash);
      }
     }
    }
   }catch(const std::exception& e){error=e.what();}catch(...){error="Unexpected analysis error";}
   {const juce::ScopedLock g(mutex);queued.erase(job.file.getFullPathName());++completed;if(ok)++succeeded;else{++failed;lastFailure=job.file.getFileName()+": "+error;}active=false;
    if(queue.empty())status="Track analysis complete: "+juce::String(succeeded)+" ready"+(failed?", "+juce::String(failed)+" failed — "+lastFailure:juce::String());
    else status=(ok?"Track analysis ready: ":"Track analysis failed: ")+job.file.getFileName()+" | "+juce::String(completed)+"/"+juce::String(total)+" complete"+(ok?juce::String():" — "+error);
    ++revision;
   }
  }
 }
};
