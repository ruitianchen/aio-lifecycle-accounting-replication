#pragma once
#include "raw_clock.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <sys/syscall.h>
#include <unistd.h>
namespace supplement_trace {
inline bool enabled(){static const bool yes=[](){auto p=std::getenv("GRT_TRACE");return p && !std::strcmp(p,"1");}();return yes;}
struct Event{long long ns;const char* name;uint64_t context,generation,thread;};
struct Buffer{std::vector<Event> events;};
struct Registry{std::mutex lock,phase_lock;std::vector<std::shared_ptr<Buffer>> buffers;std::vector<Event> phases;std::atomic<uint64_t> dropped{0};};
inline Registry& registry(){static auto* r=new Registry;return *r;}
inline uint64_t tid(){static thread_local const uint64_t t=uint64_t(::syscall(SYS_gettid));return t;}
inline void event(const char* name,uint64_t context=0,uint64_t generation=0){
 if(!enabled())return;
 static thread_local auto buffer=[](){auto b=std::make_shared<Buffer>();b->events.reserve(8192);auto& r=registry();std::lock_guard<std::mutex> g(r.lock);r.buffers.push_back(b);return b;}();
 if(buffer->events.size()>=524288){++registry().dropped;return;}
 buffer->events.push_back({NativeRawClock::now().time_since_epoch().count(),name,context,generation,tid()});
}
inline void phase(const char* name,uint64_t context=0,uint64_t generation=0){
 if(!std::getenv("GRT_PHASE_PATH"))return;
 auto& r=registry();std::lock_guard<std::mutex> g(r.phase_lock);
 if(r.phases.size()>=16384){++r.dropped;return;}
 r.phases.push_back({NativeRawClock::now().time_since_epoch().count(),name,context,generation,tid()});
}
struct Span{const char* end;uint64_t id;Span(const char* begin,const char* finish,uint64_t context=0):end(finish),id(context){phase(begin,id);}~Span(){phase(end,id);}};
inline void dump(const char* path){
 auto& r=registry();
 if(enabled() && path){
  std::ofstream f(path);if(!f)throw std::runtime_error("trace output open failed");
  std::lock_guard<std::mutex> g(r.lock);
  for(auto& b:r.buffers)for(auto& e:b->events)f<<e.ns<<'\t'<<e.thread<<'\t'<<e.name<<'\t'<<e.context<<'\t'<<e.generation<<'\n';
  f.close();if(!f)throw std::runtime_error("trace output write failed");
 }
 if(auto p=std::getenv("GRT_PHASE_PATH")){
  std::ofstream f(p);if(!f)throw std::runtime_error("phase output open failed");
  std::lock_guard<std::mutex> g(r.phase_lock);
  for(auto& e:r.phases)f<<e.ns<<'\t'<<e.thread<<'\t'<<e.name<<'\t'<<e.context<<'\t'<<e.generation<<'\n';
  f<<NativeRawClock::now().time_since_epoch().count()<<'\t'<<tid()<<"\ttrace_dropped\t"<<r.dropped.load()<<"\t0\n";
  f.close();if(!f)throw std::runtime_error("phase output write failed");
 }
 if(r.dropped)throw std::runtime_error("bounded trace events lost");
}
}
