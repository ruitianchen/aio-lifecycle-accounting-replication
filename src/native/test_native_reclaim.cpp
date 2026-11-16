#include "parallel_release.hpp"
#include <libaio.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
unsigned long quota(){std::ifstream f("/proc/sys/fs/aio-nr");unsigned long n=0;f>>n;return n;}
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){
  check(quota()==0,"initial quota");
  std::vector<io_context_t> handles(13,0);size_t made=0;
  try {
    for(auto& h:handles){check(io_setup(1024,&h)==0,"native io_setup");++made;}
    check(quota()==13312,"native reserved quota");
  } catch(...) {
    for(size_t i=0;i<made;++i)io_destroy(handles[i]);
    throw;
  }
  auto result=grt_reclaim::run(handles,[](io_context_t h){
    check(io_destroy(h)==0,"native cross-thread io_destroy");
  });
  if(result.error())std::rethrow_exception(result.error());
  for(auto v:result.success)check(v==1,"native ownership result");
  check(quota()==0,"native final quota");
  std::cout<<"PASS_NATIVE setup=13 destroy=13 peak_quota=13312 final_quota=0 data_requests=0\n";
}
