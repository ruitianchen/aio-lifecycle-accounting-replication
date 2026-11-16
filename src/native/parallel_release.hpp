#pragma once
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

// Caller owns unique handles and has externally quiesced all accepted I/O.
// This helper never retries a release. Each result byte has a unique writer.
namespace grt_reclaim {
using Factory = std::function<std::thread(std::function<void()>)>;
struct Result {
  std::vector<unsigned char> success;
  std::vector<std::exception_ptr> failures;
  std::exception_ptr spawn_failure;
  std::exception_ptr error() const {
    if(spawn_failure)return spawn_failure;
    for(const auto& e:failures)if(e)return e;
    return {};
  }
};
inline std::thread spawn(std::function<void()> task) {
  return std::thread(std::move(task));
}
template<class Handle, class Release>
Result run(const std::vector<Handle>& handles, Release release,
           Factory factory=spawn) {
  if(handles.size()>13)throw std::invalid_argument("parallel reclaim bound");
  for(size_t i=0;i<handles.size();++i) {
    if(!handles[i])throw std::invalid_argument("empty reclaim handle");
    for(size_t j=0;j<i;++j)
      if(handles[i]==handles[j])throw std::invalid_argument("duplicate reclaim handle");
  }
  Result result;
  result.success.resize(handles.size(),0);
  result.failures.resize(handles.size());
  std::vector<std::thread> workers;workers.reserve(handles.size());
  std::mutex mutex;std::condition_variable cv;bool open=false,cancel=false;
  try {
    for(size_t i=0;i<handles.size();++i) {
      workers.emplace_back(factory([&,i]{
        {
          std::unique_lock<std::mutex> lock(mutex);
          cv.wait(lock,[&]{return open || cancel;});
          if(cancel)return;
        }
        try {release(handles[i]);result.success[i]=1;}
        catch(...) {result.failures[i]=std::current_exception();}
      }));
    }
  } catch(...) {result.spawn_failure=std::current_exception();}
  {
    std::lock_guard<std::mutex> lock(mutex);
    cancel=bool(result.spawn_failure);open=!cancel;
  }
  cv.notify_all();
  for(auto& worker:workers)worker.join();
  return result;
}
}
