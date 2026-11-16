#include "composition_runtime.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <stdexcept>
void check(bool b,const char* x){if(!b)throw std::runtime_error(x);}
uint64_t nr(){std::ifstream f("/proc/sys/fs/aio-nr");uint64_t n;f>>n;return n;}
int main(int argc,char**argv){try{
 check(argc==2,"arm required");std::string arm=argv[1];
 bool tls=arm.find("TLS")==0,retain=arm.find("A1")==0;
 setenv("GRT_ARM",arm.c_str(),1);setenv("GRT_COMPOSE_A",tls?"2":retain?"1":"0",1);
 setenv("GRT_FINE_LOCK",arm.find("_F")!=std::string::npos?"1":"0",1);
 check(nr()==0,"start aio quota");composition::ContextPool p(13);
 composition::inject_setup_failure();bool fail=false;try{p.acquire();}catch(...){fail=true;}
 check(fail && p.pending()==0 && p.stats().live==0 && nr()==0,"setup rollback");
 auto a=p.acquire();auto b=p.acquire();check(tls?(a==b):(a!=b),"TLS alias versus per-reader context");
 fail=false;try{p.shutdown();}catch(...){fail=true;}check(fail,"borrowed shutdown rejected");
 p.release(a,retain);p.release(b,retain);p.shutdown();check(nr()==0,"first cleanup");
 // Same owner and thread, new generation must not reuse a destroyed TLS handle.
 auto fresh=p.acquire();p.release(fresh,retain);p.shutdown();check(nr()==0,"generation cleanup");
 // Thread exit may precede final cross-thread reader lease return.
 if(tls){io_context_t c=0;std::thread owner([&]{c=p.acquire();});owner.join();
  check(p.stats().borrowed==1 && p.stats().live==1,"exit defers while borrowed");p.release(c,false);check(nr()==0,"cross-thread final release");}
 // Concurrent creation/last-release, no more than 13 live contexts.
 std::atomic<int> ready{0};std::atomic<bool> go{false};std::atomic<bool> error{false};
 std::vector<std::thread> ts;
 for(int i=0;i<13;++i)ts.emplace_back([&]{io_context_t c=0;try{c=p.acquire();}catch(...){error=true;}
  ++ready;while(!go.load())std::this_thread::yield();if(c){try{p.release(c,retain);}catch(...){error=true;}}});
 while(ready!=13)std::this_thread::yield();check(!error && p.stats().borrowed==13,"parallel reservation");
 go=true;for(auto& t:ts)t.join();p.shutdown();check(nr()==0 && p.pending()==0 && p.stats().peak_live<=13,"parallel drain");
 // Destroy failure must retain quota and be recoverable by explicit cleanup, never reused early.
 auto c=p.acquire();composition::inject_destroy_failure();fail=false;
 try{if(tls){p.release(c,false);p.retire_current_thread_reference_context();}else p.release(c,false);}catch(...){fail=true;}
 check(fail && p.stats().live==1 && nr()==1024,"destroy failure retained quota");
 if(arm=="A0_DIRECT")p.release(c,false);else p.shutdown();
 p.shutdown();auto s=p.stats();check(!s.live && !s.borrowed && !p.pending() && nr()==0,"final zero");
 std::cout<<"PASS "<<arm<<" setup="<<s.setup<<" destroy="<<s.destroy<<" peak="<<s.peak_live<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
