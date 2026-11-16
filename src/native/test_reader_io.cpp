#include "linux_aligned_file_reader.h"
#include "composition_runtime.hpp"
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
static std::atomic<int> fault{0};
extern "C" int __real_io_submit(io_context_t,long,iocb**);
extern "C" int __real_io_getevents(io_context_t,long,long,io_event*,timespec*);
extern "C" int __wrap_io_submit(io_context_t c,long n,iocb** p){if(fault.load()==1){fault=0;return __real_io_submit(c,n-1,p);}return __real_io_submit(c,n,p);}
extern "C" int __wrap_io_getevents(io_context_t c,long a,long b,io_event* e,timespec* t){int n=__real_io_getevents(c,a,b,e,t);if(n>0 && fault.load()==2){fault=0;e[0].res=1;}return n;}
static void check(bool x,const char*m){if(!x)throw std::runtime_error(m);}
int main(int argc,char**argv){try{
 check(argc==3,"arm file required");std::string arm=argv[1];bool tls=arm.find("TLS")==0,retain=arm.find("A1")==0;
 setenv("GRT_ARM",arm.c_str(),1);setenv("GRT_COMPOSE_A",tls?"2":retain?"1":"0",1);setenv("GRT_FINE_LOCK",arm.find("_F")!=std::string::npos?"1":"0",1);
 int fd=::open(argv[2],O_RDWR|O_CREAT|O_EXCL,0600);check(fd>=0,"new fixture");check(ftruncate(fd,8192)==0 && fsync(fd)==0,"fixture sync");::close(fd);
 void* b=nullptr;check(posix_memalign(&b,4096,8192)==0,"aligned buffer");std::memset(b,71,8192);
 {
  LinuxAlignedFileReader reader;reader.open(argv[2],true,false);reader.register_thread();auto& ctx=reader.get_ctx();
  std::vector<AlignedRead> req;req.emplace_back(0,4096,b);req.emplace_back(4096,4096,static_cast<char*>(b)+4096);
  reader.write(req,ctx);std::memset(b,0,8192);reader.read(req,ctx,false);
  for(size_t i=0;i<8192;++i)check(static_cast<unsigned char*>(b)[i]==71,"round trip bytes");
  bool fail=false;fault=1;try{reader.read(req,ctx,false);}catch(...){fail=true;}check(fail && fault==0,"partial accepted set drained and rejected");
  fail=false;fault=2;try{reader.read(req,ctx,false);}catch(...){fail=true;}check(fail && fault==0,"completed short event rejected");
  reader.read(req,ctx,false);reader.deregister_all_threads();reader.close();
 }
 composition::shutdown();check(composition::pool().stats().live==0 && composition::pool().pending()==0,"final drain");
 std::free(b);std::cout<<"PASS_IO "<<arm<<" real direct IO and completion error checks\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
