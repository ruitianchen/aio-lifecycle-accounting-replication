#pragma once
#include "parallel_release.hpp"
#include <cstdint>
#include <memory>
#include <utility>

namespace grt_reclaim {
struct WorkerStats {uint64_t started=0,joined=0,groups=0,jobs=0;};

// Only reclaim execution is retained. Handles and release results remain owned by
// the caller until a complete group barrier. No retry and no AIO context cache.
class PersistentExecutor {
 public:
  explicit PersistentExecutor(size_t count,Factory factory=spawn):count_(count){
    if(count==0 || count>13)throw std::invalid_argument("persistent worker bound");
    workers_.reserve(count);
    try {
      for(size_t i=0;i<count;++i){
        workers_.emplace_back(factory([this,i]{worker(i);}));++stats_.started;
      }
    }catch(...){
      {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
      ready_.notify_all();
      for(auto& t:workers_)if(t.joinable()){t.join();++stats_.joined;}
      throw;
    }
  }
  PersistentExecutor(const PersistentExecutor&)=delete;
  PersistentExecutor& operator=(const PersistentExecutor&)=delete;
  ~PersistentExecutor(){shutdown();}

  template<class Handle,class Release>
  Result run(const std::vector<Handle>& handles,Release release){
    // Serialize dispatch and shutdown; disjoint callers never share result storage.
    std::unique_lock<std::mutex> dispatch(dispatch_);
    if(handles.size()>count_)throw std::invalid_argument("persistent reclaim group bound");
    for(size_t i=0;i<handles.size();++i){
      if(!handles[i])throw std::invalid_argument("empty reclaim handle");
      for(size_t j=0;j<i;++j)if(handles[i]==handles[j])throw std::invalid_argument("duplicate reclaim handle");
    }
    Result result;result.success.resize(handles.size(),0);result.failures.resize(handles.size());
    std::unique_lock<std::mutex> lock(mutex_);
    if(stopping_)throw std::logic_error("persistent executor is closed");
    if(handles.empty())return result;
    // Construct the callable before advancing generation. Allocation failure leaves
    // every handle with the caller and wakes no workers.
    std::function<void(size_t)> next=[&](size_t i){
      try{release(handles[i]);result.success[i]=1;}
      catch(...){result.failures[i]=std::current_exception();}
    };
    task_=std::move(next);active_=handles.size();finished_=0;
    ++generation_;++stats_.groups;stats_.jobs+=handles.size();
    ready_.notify_all();
    complete_.wait(lock,[&]{return finished_==count_;});
    task_=nullptr;active_=0;
    return result;
  }

  void shutdown(){
    std::unique_lock<std::mutex> dispatch(dispatch_);
    {std::lock_guard<std::mutex> lock(mutex_);if(stopping_)return;stopping_=true;}
    ready_.notify_all();
    for(auto& t:workers_)if(t.joinable()){t.join();++stats_.joined;}
  }
  WorkerStats stats() const {
    std::lock_guard<std::mutex> lock(dispatch_);return stats_;
  }
 private:
  void worker(size_t index){
    uint64_t seen=0;std::unique_lock<std::mutex> lock(mutex_);
    for(;;){
      ready_.wait(lock,[&]{return stopping_ || generation_!=seen;});
      if(stopping_)return;
      seen=generation_;const bool assigned=index<active_;
      lock.unlock();
      if(assigned)task_(index);
      lock.lock();++finished_;if(finished_==count_)complete_.notify_one();
    }
  }
  const size_t count_;
  mutable std::mutex dispatch_;
  std::mutex mutex_;
  std::condition_variable ready_,complete_;
  bool stopping_=false;
  uint64_t generation_=0;
  size_t active_=0,finished_=0;
  std::function<void(size_t)> task_;
  std::vector<std::thread> workers_;
  WorkerStats stats_;
};

inline std::unique_ptr<PersistentExecutor>& persistent_storage(){
  static std::unique_ptr<PersistentExecutor> value;return value;
}
inline WorkerStats& persistent_final_stats(){static WorkerStats value;return value;}
inline void start_persistent(){
  if(persistent_storage())throw std::logic_error("persistent executor already started");
  persistent_storage().reset(new PersistentExecutor(10));
}
inline void stop_persistent(){
  auto& p=persistent_storage();
  if(p){p->shutdown();persistent_final_stats()=p->stats();p.reset();}
}
template<class Handle,class Release>
Result run_persistent(const std::vector<Handle>& handles,Release release){
  if(!persistent_storage())throw std::logic_error("persistent executor not started");
  return persistent_storage()->run(handles,std::move(release));
}
}
