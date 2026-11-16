#include "persistent_reclaim.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

void check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
int main(){
  int tests=0;
  {
    bool rejected=false;
    try{grt_reclaim::PersistentExecutor p(0);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"zero worker pool accepted");++tests;
  }
  {
    std::atomic<int> alive{0};int attempts=0;
    grt_reclaim::Factory fail=[&](std::function<void()> f){
      if(attempts++==2)throw std::runtime_error("injected spawn failure");
      return std::thread([&,f]{++alive;f();--alive;});
    };
    bool rejected=false;
    try{grt_reclaim::PersistentExecutor p(10,fail);}catch(...){rejected=true;}
    check(rejected && alive==0,"constructor leaked workers");++tests;
  }
  {
    grt_reclaim::PersistentExecutor p(10);
    check(p.stats().started==10,"wrong worker count");
    auto r=p.run(std::vector<int>{},[](int){});
    check(!r.error() && r.success.empty() && p.stats().groups==0,"empty group changed state");++tests;
    for(const auto& handles:std::vector<std::vector<int>>{{0},{1,1},std::vector<int>(11,1)}){
      int calls=0;bool rejected=false;
      try{p.run(handles,[&](int){++calls;});}catch(const std::invalid_argument&){rejected=true;}
      check(rejected && calls==0,"invalid handles reached worker");++tests;
    }
    std::mutex m;std::set<std::thread::id> first,second;
    for(int repeat=0;repeat<2;++repeat){
      std::condition_variable cv;int arrived=0;
      auto r=p.run(std::vector<int>{1,2,3,4,5,6,7,8,9,10},[&](int){
        std::unique_lock<std::mutex> lock(m);
        (repeat?second:first).insert(std::this_thread::get_id());
        ++arrived;cv.notify_all();
        check(cv.wait_for(lock,std::chrono::seconds(5),[&]{return arrived==10;}),"pool did not fan out");
      });
      check(!r.error() && arrived==10,"group failed or returned early");
      for(auto value:r.success)check(value==1,"successful handle missing");
    }
    check(first.size()==10 && first==second,"workers recreated across groups");++tests;
    for(int n:{3,10,3,10,3}){
      std::vector<int> v;for(int i=1;i<=n;++i)v.push_back(i);
      std::vector<std::atomic<int>> calls(n);for(auto& x:calls)x=0;
      auto r=p.run(v,[&](int h){++calls[h-1];});
      check(!r.error(),"alternating group failure");
      for(auto& x:calls)check(x==1,"not exactly once");
    }++tests;
    {
      std::atomic<int> calls{0};
      auto r=p.run(std::vector<int>{1,2,3},[&](int h){++calls;if(h==2)throw std::runtime_error("release failure");});
      check(calls==3 && r.error() && r.success==std::vector<unsigned char>({1,0,1}),"failure lost ownership");++tests;
      auto next=p.run(std::vector<int>{7},[](int){});
      check(!next.error() && next.success[0]==1,"failed job poisoned next group");++tests;
    }
    {
      std::atomic<int> calls{0};
      auto run=[&]{for(int i=0;i<7;++i){auto r=p.run(std::vector<int>{1,2,3},[&](int){++calls;});check(!r.error(),"concurrent dispatch");}};
      std::thread a(run),b(run);a.join();b.join();check(calls==42,"concurrent dispatch lost jobs");++tests;
    }
    p.shutdown();auto s=p.stats();
    check(s.started==10 && s.joined==10 && s.groups==23 && s.jobs==95,"counter closure");++tests;
    p.shutdown();check(p.stats().joined==10,"double join");++tests;
    bool rejected=false;try{p.run(std::vector<int>{1},[](int){});}catch(const std::logic_error&){rejected=true;}
    check(rejected,"accepted work after shutdown");++tests;
  }
  std::cout<<"PASS "<<tests<<" persistent-worker ownership/lifetime/failure tests\n";
}
