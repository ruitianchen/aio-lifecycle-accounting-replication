#include "persistent_reclaim.hpp"
#include <libaio.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
unsigned long nr(){std::ifstream f("/proc/sys/fs/aio-nr");unsigned long n=0;f>>n;return n;}
int main(){
  const auto before=nr();grt_reclaim::PersistentExecutor executor(10);
  int groups=0,jobs=0;
  for(int n:{10,3,10,3}){
    std::vector<io_context_t> handles(n,0);
    for(auto& c:handles)if(io_setup(1024,&c)!=0)throw std::runtime_error("setup failed");
    auto r=executor.run(handles,[](io_context_t c){if(io_destroy(c)!=0)throw std::runtime_error("destroy failed");});
    if(r.error())std::rethrow_exception(r.error());
    for(auto x:r.success)if(x!=1)throw std::runtime_error("missing release");
    ++groups;jobs+=n;
    if(nr()!=before)throw std::runtime_error("quota not drained");
  }
  executor.shutdown();auto s=executor.stats();
  if(s.groups!=unsigned(groups)||s.jobs!=unsigned(jobs)||s.started!=10||s.joined!=10||nr()!=before)throw std::runtime_error("closure");
  std::cout<<"PASS real empty AIO:26 creations,26 destroys,4 groups,10 reused workers,quota restored\n";
}
