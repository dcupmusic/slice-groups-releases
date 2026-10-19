#pragma once
#include <sys/mman.h>
#include <unistd.h>
#include <mutex>
#include <map>
#include <set>
#include <cerrno>
// Reference-counted page leases across Track engines. All VM calls run off the audio thread.
class PlaybackPageLease {
 struct Bank{std::mutex mutex;std::map<uintptr_t,int> references;std::atomic<int> error{0};};
 static Bank& bank(){static Bank state;return state;}
 std::set<uintptr_t> held;
public:
 static size_t pageSize(){static size_t size=(size_t)sysconf(_SC_PAGESIZE);return size;}
 static void add(std::set<uintptr_t>& pages,const void* address,size_t bytes){if(!address||!bytes)return;auto size=pageSize(),begin=(uintptr_t)address,end=begin+bytes;for(auto p=begin/size*size;p<end;p+=size)pages.insert(p);}
 size_t update(const std::set<uintptr_t>& desired){auto& state=bank();const std::lock_guard<std::mutex> g(state.mutex);size_t failures=0;std::set<uintptr_t> next;for(auto page:desired){if(held.count(page)){next.insert(page);continue;}auto found=state.references.find(page);if(found!=state.references.end()){++found->second;next.insert(page);}else if(mlock((const void*)page,pageSize())==0){state.references[page]=1;next.insert(page);}else{++failures;state.error=errno;}}for(auto page:held)if(!next.count(page)){auto it=state.references.find(page);if(it!=state.references.end()&&--it->second==0){munlock((const void*)page,pageSize());state.references.erase(it);}}held.swap(next);return failures;}
 static int lastError(){return bank().error.load();}
 void clear(){update({});}size_t bytes()const{return held.size()*pageSize();}~PlaybackPageLease(){clear();}
};
