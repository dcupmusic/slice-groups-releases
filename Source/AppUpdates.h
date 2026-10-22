#pragma once
// Checks only the public release endpoint. Downloads open in the user's browser;
// a loaded VST3 is never overwritten by the plug-in.
class AppUpdates final:private juce::Thread,public juce::DeletedAtShutdown {
public:
 struct State { bool checking=false,available=false;juce::String version,url,message;uint64_t serial=0; };
 static constexpr const char* releases="https://github.com/dcupmusic/slice-groups-releases/releases";
 static bool versionParts(juce::String text,std::array<int,3>& parts){if(text.startsWithChar('v'))text=text.substring(1);auto tokens=juce::StringArray::fromTokens(text,".","");if(tokens.size()!=3)return false;for(int i=0;i<3;++i){if(tokens[i].isEmpty()||tokens[i].length()>6||!tokens[i].containsOnly("0123456789"))return false;parts[i]=tokens[i].getIntValue();}return true;}
 static State parseRelease(const juce::var& json,juce::String current){State out;out.message="Could not check for updates.";std::array<int,3> installed{},latest{};auto tag=json.getProperty("tag_name",{}).toString();if(!json.isObject()||!versionParts(current,installed)||!versionParts(tag,latest)||(bool)json.getProperty("draft",false)||(bool)json.getProperty("prerelease",false))return out;
  auto page=juce::String(releases)+"/tag/"+tag;if(json.getProperty("html_url",{}).toString()!=page)return out;
  out.version=tag.startsWithChar('v')?tag.substring(1):tag;out.available=latest>installed;out.url=page;out.message=out.available?"Update available: "+out.version:"You have the latest version.";
  auto assetValues=json.getProperty("assets",{});if(auto* assets=assetValues.getArray())for(auto extension:{juce::String("-Update.zip"),juce::String(".pkg"),juce::String(".zip")}){auto expected="SliceGroups-"+out.version+"-AppleSilicon"+extension;for(auto& asset:*assets){auto url=asset.getProperty("browser_download_url",{}).toString();if(asset.getProperty("name",{}).toString()==expected&&asset.getProperty("state",{}).toString()=="uploaded"&&url=="https://github.com/dcupmusic/slice-groups-releases/releases/download/"+tag+"/"+expected){out.url=url;return out;}}}
  return out;
 }
 static AppUpdates& instance(){static auto* service=new AppUpdates();return *service;}
 State state(){const juce::ScopedLock lock(mutex);return published;}
 void request(bool force=false){if(juce::SystemStats::getEnvironmentVariable("SLICE_GROUPS_DISABLE_UPDATES",{})=="1")return;const juce::ScopedLock lock(mutex);if(published.checking||(!force&&juce::Time::currentTimeMillis()-lastAttempt<6*60*60*1000LL))return;lastAttempt=juce::Time::currentTimeMillis();published.checking=true;published.message="Checking for updates...";++published.serial;pending=true;wake.signal();}
private:
 juce::CriticalSection mutex;State published;juce::WaitableEvent wake;bool pending=false;juce::int64 lastAttempt=0;
 AppUpdates():Thread("Slice Groups update check"){startThread(juce::Thread::Priority::background);}
 ~AppUpdates()override{signalThreadShouldExit();wake.signal();stopThread(-1);}
 void run()override{while(!threadShouldExit()){wake.wait(1000);{const juce::ScopedLock lock(mutex);if(!pending)continue;pending=false;}State next;next.message="Could not check for updates. Try again later.";
   try{int status=0;auto stream=juce::URL("https://api.github.com/repos/dcupmusic/slice-groups-releases/releases/latest").createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(8000).withNumRedirectsToFollow(2).withStatusCode(&status).withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: SliceGroups/" JucePlugin_VersionString "\r\n"));
    if(stream&&status==200){juce::MemoryBlock body;char block[8192];while(!threadShouldExit()&&!stream->isExhausted()&&body.getSize()<2*1024*1024){int n=stream->read(block,sizeof block);if(n<=0)break;body.append(block,(size_t)n);}if(!threadShouldExit()&&body.getSize()<2*1024*1024)next=parseRelease(juce::JSON::parse(juce::String::fromUTF8((const char*)body.getData(),(int)body.getSize())),JucePlugin_VersionString);}
   }catch(...){ }
   const juce::ScopedLock lock(mutex);next.serial=published.serial+1;published=next;
  }}
};
