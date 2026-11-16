#include "parallel_release.hpp"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

void check(bool ok, const char* msg) { if(!ok) throw std::runtime_error(msg); }
int main() {
  int tests=0;
  {
    auto r=grt_reclaim::run(std::vector<int>{}, [](int){});
    check(r.success.empty() && !r.error(), "empty group"); ++tests;
  }
  for(int n: {1,3,10,13}) {
    std::vector<int> handles(n); std::iota(handles.begin(),handles.end(),1);
    std::vector<std::atomic<int>> calls(n); for(auto& v:calls)v=0;
    auto r=grt_reclaim::run(handles,[&](int h){++calls[h-1];});
    check(!r.error(),"normal worker failed");
    for(int i=0;i<n;++i)check(calls[i]==1 && r.success[i]==1,"not exactly once");
    ++tests;
  }
  {
    int calls=0; bool rejected=false;
    try { grt_reclaim::run(std::vector<int>{1,1},[&](int){++calls;}); }
    catch(const std::invalid_argument&) {rejected=true;}
    check(rejected && calls==0,"duplicate accepted"); ++tests;
  }
  {
    int calls=0; bool rejected=false;
    try { grt_reclaim::run(std::vector<int>(14,1),[&](int){++calls;}); }
    catch(const std::invalid_argument&) {rejected=true;}
    check(rejected && calls==0,"bound exceeded"); ++tests;
  }
  {
    std::atomic<int> calls{0},running{0};
    auto r=grt_reclaim::run(std::vector<int>{1,2,3},[&](int h){
      ++running; ++calls; --running;
      if(h==2)throw std::runtime_error("injected release failure");
    });
    check(r.error() && calls==3 && running==0,"failure ownership/join");
    check(r.success==std::vector<unsigned char>({1,0,1}),"failed handle lost"); ++tests;
  }
  {
    std::atomic<int> calls{0},alive{0}; int attempts=0;
    grt_reclaim::Factory fail=[&](std::function<void()> task){
      if(attempts++==2)throw std::runtime_error("injected thread construction failure");
      return std::thread([&,task]{++alive;task();--alive;});
    };
    auto r=grt_reclaim::run(std::vector<int>{1,2,3,4},[&](int){++calls;},fail);
    check(r.error() && calls==0 && alive==0,"spawn failure released or leaked workers");
    for(auto s:r.success){check(s==0,"spawn failure lost handle");}
    ++tests;
  }
  {
    // A barrier proves fanout without an execution-speed threshold.
    std::mutex m;std::condition_variable cv;int arrived=0;
    auto r=grt_reclaim::run(std::vector<int>{1,2,3},[&](int){
      std::unique_lock<std::mutex> lock(m);++arrived;cv.notify_all();
      check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return arrived==3;}),"workers serialized");
    });
    check(!r.error() && arrived==3,"parallel barrier");++tests;
  }
  std::cout<<"PASS "<<tests<<" ownership/gate/failure tests\n";
}
