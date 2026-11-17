#pragma once
// One low-priority service for all instances, independent of GUI message timers.
class AutoContinueService522 final:private juce::Thread {
 juce::CriticalSection mutex;std::map<void*,std::function<void()>> clients;
 AutoContinueService522():Thread("Slice Groups look-ahead"){startThread(juce::Thread::Priority::background);}
 void run()override{while(!threadShouldExit()){{const juce::ScopedLock guard(mutex);for(auto& client:clients)client.second();}wait(20);}}
public:
 static AutoContinueService522& instance(){static AutoContinueService522 service;return service;}
 void add(void* key,std::function<void()> callback){const juce::ScopedLock guard(mutex);clients[key]=std::move(callback);notify();}
 void remove(void* key){const juce::ScopedLock guard(mutex);clients.erase(key);}
 ~AutoContinueService522()override{stopThread(-1);}
};
