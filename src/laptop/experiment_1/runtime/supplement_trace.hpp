#pragma once
#include "raw_clock.hpp"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <fstream>
#include <stdexcept>
namespace supplement_trace {
inline bool enabled(){static const bool yes=[](){auto p=std::getenv("GRT_TRACE");return p && !std::strcmp(p,"1");}();return yes;}
struct Event{long long ns;const char* name;uint64_t context,generation,thread;};
struct Buffer{std::vector<Event> events;};
struct Registry{std::mutex lock;std::vector<std::shared_ptr<Buffer>> buffers;};
inline Registry& registry(){static auto* r=new Registry;return *r;}
inline void event(const char* name,uint64_t context=0,uint64_t generation=0){
 if(!enabled())return;
 static thread_local auto buffer=[](){auto b=std::make_shared<Buffer>();b->events.reserve(8192);auto& r=registry();std::lock_guard<std::mutex> g(r.lock);r.buffers.push_back(b);return b;}();
 buffer->events.push_back({NativeRawClock::now().time_since_epoch().count(),name,context,generation,std::hash<std::thread::id>{}(std::this_thread::get_id())});
}
inline void dump(const char* path){
 if(!enabled() || !path)return;
 std::ofstream f(path);if(!f)throw std::runtime_error("trace output open failed");
 auto& r=registry();std::lock_guard<std::mutex> g(r.lock);
 for(auto& b:r.buffers)for(auto& e:b->events)f<<e.ns<<'\t'<<e.thread<<'\t'<<e.name<<'\t'<<e.context<<'\t'<<e.generation<<'\n';
 f.close();if(!f)throw std::runtime_error("trace output write failed");
}
}
