#include <omp.h>
#include "supplement_trace.hpp"
#include "linux_aligned_file_reader.h"
#include "composition_runtime.hpp"
#include "bulk_io.hpp"
#include "parallel_release.hpp"
#include "persistent_reclaim.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <exception>

namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void execute_checked(IOContext ctx,int fd,std::vector<AlignedRead>& requests,bool write) {
  for(size_t base=0;base<requests.size();base+=1024) {
    const size_t n=std::min(size_t(1024),requests.size()-base);
    std::vector<iocb> cbs(n);std::vector<iocb*> ptrs(n);std::vector<io_event> events(n);
    for(size_t i=0;i<n;++i) {
      auto& req=requests[base+i];
      require(req.offset%512==0 && req.len%512==0 && uintptr_t(req.buf)%512==0,"unaligned direct I/O");
      if(write)io_prep_pwrite(&cbs[i],fd,req.buf,req.len,req.offset);
      else io_prep_pread(&cbs[i],fd,req.buf,req.len,req.offset);
      ptrs[i]=&cbs[i];
    }
    supplement_trace::event("submit_start",uint64_t(ctx));int submitted=io_submit(ctx,n,ptrs.data());supplement_trace::event("submit_end",uint64_t(ctx));
    // A partial submission is fatal to this attempt; drain accepted requests before
    // throwing so their buffers are never released while I/O remains in flight.
    require(submitted>=0,"io_submit failed");
    size_t done=0;supplement_trace::event("completion_wait_start",uint64_t(ctx));
    while(done<size_t(submitted)) {
      int rc=io_getevents(ctx,1,submitted-done,events.data()+done,nullptr);
      if(rc==-EINTR)continue;
      require(rc>0,"io_getevents failed");done+=size_t(rc);
    }
    supplement_trace::event("completion_wait_end",uint64_t(ctx));require(size_t(submitted)==n,"partial AIO submission");
    uint64_t bytes=0;
    for(size_t i=0;i<n;++i) {
      require(events[i].res2==0 && events[i].res==events[i].obj->u.c.nbytes,"AIO completion error or short I/O");
      bytes+=requests[base+i].len;
    }
    composition::record_io(write,bytes,n);
  }
}
bool use_cache(int fd) {auto* cache=composition::store();return cache && cache->matches(fd);}
void execute_dispatch(IOContext ctx,int fd,std::vector<AlignedRead>& requests,bool write){
  if(!grt_bulk::active()){execute_checked(ctx,fd,requests,write);return;}
  std::vector<uint64_t> offsets;offsets.reserve(requests.size());
  for(auto& req:requests){require(req.len==4096 && req.offset%4096==0,"bulk non-page request");offsets.push_back(req.offset);}
  std::sort(offsets.begin(),offsets.end());require(std::adjacent_find(offsets.begin(),offsets.end())==offsets.end(),"bulk duplicate pages");
  uint64_t hash=1469598103934665603ull,submits=0;
  for(auto value:offsets){hash^=value;hash*=1099511628211ull;}
  if(!grt_bulk::parallel()){execute_checked(ctx,fd,requests,write);submits=(requests.size()+1023)/1024;}
  else for(size_t base=0;base<requests.size();base+=1024){
    const size_t count=std::min(size_t(1024),requests.size()-base);
    std::vector<AlignedRead> children[4];std::exception_ptr errors[4];
    for(size_t worker=0;worker<4;++worker){size_t lo=std::min(worker*256,count),hi=std::min((worker+1)*256,count);
      children[worker].assign(requests.begin()+base+lo,requests.begin()+base+hi);submits+=!children[worker].empty();}
    #pragma omp parallel for num_threads(4) schedule(static,1)
    for(int worker=0;worker<4;++worker)if(!children[worker].empty()){
      try{execute_checked(grt_bulk::context(worker),fd,children[worker],write);}catch(...){errors[worker]=std::current_exception();}
    }
    for(auto error:errors)if(error)std::rethrow_exception(error);
  }
  grt_bulk::record(write,requests.size(),submits,hash);
}
}
LinuxAlignedFileReader::LinuxAlignedFileReader(){file_desc=-1;}
LinuxAlignedFileReader::~LinuxAlignedFileReader(){supplement_trace::Span span("reader_destruct_begin","reader_destruct_end",uint64_t(this));deregister_all_threads();close();}
IOContext& LinuxAlignedFileReader::get_ctx(){
  std::lock_guard<std::mutex> lock(ctx_mut);auto it=ctx_map.find(std::this_thread::get_id());
  require(it!=ctx_map.end(),"unregistered AIO worker");return ctx_map[std::this_thread::get_id()];
}
void LinuxAlignedFileReader::register_thread(){supplement_trace::phase("reader_register",uint64_t(this),uint64_t(omp_get_num_threads()));
  std::lock_guard<std::mutex> lock(ctx_mut);auto id=std::this_thread::get_id();
  if(ctx_map.find(id)==ctx_map.end())ctx_map[id]=composition::pool().acquire();
}
void LinuxAlignedFileReader::deregister_thread(){
  std::lock_guard<std::mutex> lock(ctx_mut);auto it=ctx_map.find(std::this_thread::get_id());
  require(it!=ctx_map.end(),"unregistered AIO release");composition::pool().release(it->second,composition::persistent_aio());ctx_map.erase(it);
}
void LinuxAlignedFileReader::deregister_all_threads(){
  std::lock_guard<std::mutex> lock(ctx_mut);
  const char* flag=std::getenv("GRT_RECLAIM_PARALLEL");
  require(!flag || !std::strcmp(flag,"0") || !std::strcmp(flag,"1") || !std::strcmp(flag,"2"),"invalid parallel reclaim flag");
  if(flag && std::strcmp(flag,"0")) {
    const char* arm=std::getenv("GRT_ARM");
    require(arm && !std::strcmp(arm,"A0_DIRECT") && !composition::persistent_aio(),"parallel reclaim requires direct lifetime");
    if(ctx_map.empty())return;
    supplement_trace::Span span("parallel_reclaim_begin","parallel_reclaim_end",uint64_t(this));
    supplement_trace::phase("parallel_reclaim_group",uint64_t(this),ctx_map.size());
    std::vector<std::thread::id> keys;std::vector<IOContext> handles;
    keys.reserve(ctx_map.size());handles.reserve(ctx_map.size());
    for(const auto& entry:ctx_map){keys.push_back(entry.first);handles.push_back(entry.second);}
    // No request outlives the blocking read/write methods; the owner quiesces
    // readers before entering teardown. Workers never enter reader metadata.
    auto release=[](IOContext handle){composition::pool().release(handle,false);};
    auto result=!std::strcmp(flag,"2") ? grt_reclaim::run_persistent(handles,release)
                                        : grt_reclaim::run(handles,release);
    for(size_t i=0;i<keys.size();++i)if(result.success[i])ctx_map.erase(keys[i]);
    if(result.error())std::rethrow_exception(result.error());
    return;
  }
  for(const auto& entry:ctx_map) composition::pool().release(entry.second,composition::persistent_aio());
  ctx_map.clear();
}
void LinuxAlignedFileReader::open(const std::string& name,bool writes,bool create){
  const int next=::open(name.c_str(),O_DIRECT|O_LARGEFILE|(writes?O_RDWR:O_RDONLY)|(create?O_CREAT:0),0644);
  require(next>=0,"open direct-I/O file failed");
  // No I/O outlives these blocking reader methods. The page store has its own
  // duplicate descriptor and file-identity binding across a same-file reload.
  if(file_desc>=0) require(::close(file_desc)==0,"close previous reader fd failed");
  file_desc=next;
}
void LinuxAlignedFileReader::close(){if(file_desc>=0){require(::close(file_desc)==0,"reader close failed");file_desc=-1;}}
void LinuxAlignedFileReader::read(std::vector<AlignedRead>& requests,IOContext& ctx,bool async){
  require(!async && file_desc>=0,"unsupported asynchronous read/closed fd");
  if(!use_cache(file_desc)){execute_dispatch(ctx,file_desc,requests,false);return;}
  auto* cache=composition::store();std::vector<AlignedRead> misses;misses.reserve(1024);
  auto complete=[&](){if(misses.empty())return;execute_checked(ctx,file_desc,misses,false);for(auto& m:misses)if(m.offset)cache->put_clean(m.offset/4096,m.buf);misses.clear();};
  for(auto& req:requests) {
    require(req.offset%4096==0 && req.len%4096==0,"cached graph requires complete aligned pages");
    for(uint64_t delta=0;delta<req.len;delta+=4096) {
      const auto offset=req.offset+delta;void* dest=static_cast<char*>(req.buf)+delta;
      if(offset==0 || !cache->get(offset/4096,dest)) {
        misses.emplace_back(offset,4096,dest);if(misses.size()==1024)complete();
      }
    }
  }
  complete();
}
void LinuxAlignedFileReader::write(std::vector<AlignedRead>& requests,IOContext& ctx){
  require(file_desc>=0,"write on closed fd");
  if(!use_cache(file_desc)){execute_dispatch(ctx,file_desc,requests,true);return;}
  auto* cache=composition::store();
  for(auto& req:requests) {
    require(req.offset>0 && req.offset%4096==0 && req.len%4096==0,"cached graph write requires data pages");
    for(uint64_t delta=0;delta<req.len;delta+=4096)cache->put_dirty((req.offset+delta)/4096,static_cast<char*>(req.buf)+delta);
  }
}
void LinuxAlignedFileReader::sequential_write(AlignedRead& request,IOContext& ctx){std::vector<AlignedRead> requests{request};write(requests,ctx);}
