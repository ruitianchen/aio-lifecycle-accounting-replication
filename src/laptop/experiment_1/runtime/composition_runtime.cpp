#include "supplement_trace.hpp"
#include "raw_clock.hpp"
#include "composition_runtime.hpp"
#include "bulk_io.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace composition {
namespace {
using Clock=NativeRawClock;
double seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
void demand(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
bool enabled(const char* name) {const char* v=std::getenv(name);return v && std::strcmp(v,"1")==0;}
std::atomic<uint64_t> generations{0};
struct ThreadReference {
 ContextPool* owner=nullptr;io_context_t ctx=0;uint64_t generation=0;
 std::shared_ptr<std::atomic<bool>> lifetime;
 ~ThreadReference() noexcept {
  if(owner && ctx && lifetime && lifetime->load()) {
   try{owner->reference_thread_exit(ctx,generation);}catch(...){std::terminate();}
  }
 }
};
thread_local ThreadReference reference;
bool tls_mode(){const char* v=std::getenv("GRT_COMPOSE_A");return v && std::strcmp(v,"2")==0;}
PageStore* global_store=nullptr;
bool active_store=false;
uint64_t batch_generation=0;
std::atomic<size_t> phase{0};
struct IoCounters { std::atomic<uint64_t> read_requests{0},write_requests{0},read_bytes{0},write_bytes{0},sync_write_requests{0}; } counters[5];
}
// Shared state machine for C/F. F releases only the pool lock around slow syscalls.
namespace {
bool direct_mode(){const char* p=std::getenv("GRT_ARM");return p && std::strcmp(p,"A0_DIRECT")==0;}
bool fine_mode(){return enabled("GRT_FINE_LOCK");}
struct AtomicDirect {
 std::atomic<uint64_t> setup{0},destroy{0},live{0},peak{0},borrowed{0},leases{0},pending{0};
 std::atomic<uint64_t> setup_ns{0},destroy_ns{0};
} direct_stats;
void maximum(std::atomic<uint64_t>& x,uint64_t n){auto a=x.load();while(a<n && !x.compare_exchange_weak(a,n)){} }
std::atomic<int> injected_setup{0},injected_destroy{0};
int setup_call(size_t n,io_context_t* c){if(injected_setup.exchange(0))return -EAGAIN;return io_setup(n,c);}
int destroy_call(io_context_t c){if(injected_destroy.exchange(0))return -EBUSY;return io_destroy(c);}
}
void inject_setup_failure(){injected_setup=1;}
void inject_destroy_failure(){injected_destroy=1;}
struct ContextPool::Impl {
 enum State {FREE,CREATING,BORROWED,IDLE,DESTROYING,FAILED};
 struct Slot {State state=FREE;io_context_t ctx=0;size_t depth=0;uint64_t refs=0;bool exited=false;};
 std::mutex lock;Slot slots[17];size_t limit;uint64_t pending=0,reserved=0,generation;bool closing=false;
 PoolStats stats;std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
 explicit Impl(size_t n):limit(n),generation(++generations){}
 void dispose(size_t i,std::unique_lock<std::mutex>& guard){
  auto& s=slots[i];demand(s.ctx && s.refs==0,"destroy leased or empty slot");
  s.state=DESTROYING;++pending;auto ctx=s.ctx;auto begin=Clock::now();supplement_trace::event("destroy_start",uint64_t(ctx),generation);
  if(fine_mode())guard.unlock();
  int rc=destroy_call(ctx);double elapsed=seconds(begin);supplement_trace::event("destroy_end",uint64_t(ctx),generation);
  if(!guard.owns_lock())guard.lock();
  --pending;stats.destroy_s+=elapsed;
  if(rc){s.state=FAILED;throw std::runtime_error("io_destroy failed; slot and quota remain owned");}
  ++stats.destroy;--stats.live;stats.live_capacity-=s.depth;--reserved;s=Slot{};
 }
};
ContextPool::ContextPool(size_t limit):impl_(new Impl(limit)){demand(limit>0 && limit<=17,"invalid AIO pool limit");}
ContextPool::~ContextPool(){impl_->alive->store(false);try{shutdown();}catch(...){std::terminate();}}
io_context_t ContextPool::acquire(size_t depth){
 demand(depth==1024,"supplement requires depth 1024 and single submitter");
 if(direct_mode()){
  auto live=direct_stats.live.fetch_add(1)+1;
  if(live>impl_->limit){--direct_stats.live;throw std::runtime_error("direct context ceiling");}
  ++direct_stats.pending;io_context_t ctx=0;auto start=Clock::now();int rc=setup_call(depth,&ctx);
  direct_stats.setup_ns+=uint64_t(seconds(start)*1e9);--direct_stats.pending;
  if(rc){--direct_stats.live;throw std::runtime_error("direct io_setup failed");}
  maximum(direct_stats.peak,live);++direct_stats.setup;++direct_stats.borrowed;++direct_stats.leases;return ctx;
 }
 auto& p=*impl_;supplement_trace::event("lock_wait_start");std::unique_lock<std::mutex> guard(p.lock);supplement_trace::event("lock_acquired");demand(!p.closing,"acquire during shutdown");
 if(tls_mode() && reference.owner==this && reference.generation==p.generation && reference.ctx){
  for(auto& s:p.slots)if(s.ctx==reference.ctx && !s.exited && (s.state==Impl::IDLE || s.state==Impl::BORROWED)){
   ++s.refs;s.state=Impl::BORROWED;++p.stats.borrowed;++p.stats.leases;return s.ctx;
  }
  throw std::runtime_error("TLS reference generation/registry mismatch");
 }
 if(!tls_mode())for(auto& s:p.slots)if(s.state==Impl::IDLE && s.depth==depth){
  s.refs=1;s.state=Impl::BORROWED;++p.stats.borrowed;++p.stats.leases;return s.ctx;
 }
 demand(p.reserved<p.limit,"AIO context bound exceeded");
 size_t i=0;while(i<p.limit && p.slots[i].state!=Impl::FREE)++i;demand(i<p.limit,"no free context slot");
 auto& s=p.slots[i];s.state=Impl::CREATING;s.depth=depth;++p.reserved;++p.pending;
 if(fine_mode())guard.unlock();
 io_context_t ctx=0;auto start=Clock::now();supplement_trace::event("setup_start",0,p.generation);int rc=setup_call(depth,&ctx);double elapsed=seconds(start);supplement_trace::event("setup_end",uint64_t(ctx),p.generation);
 if(!guard.owns_lock())guard.lock();
 --p.pending;p.stats.setup_s+=elapsed;
 if(rc){--p.reserved;s=Impl::Slot{};throw std::runtime_error("io_setup failed; reservation rolled back");}
 s.ctx=ctx;s.refs=1;s.state=Impl::BORROWED;++p.stats.setup;++p.stats.live;++p.stats.borrowed;++p.stats.leases;
 p.stats.live_capacity+=depth;p.stats.peak_live=std::max(p.stats.peak_live,p.stats.live);
 p.stats.peak_live_capacity=std::max(p.stats.peak_live_capacity,p.stats.live_capacity);
 if(tls_mode()){reference.owner=this;reference.ctx=ctx;reference.generation=p.generation;reference.lifetime=p.alive;}
 return ctx;
}
void ContextPool::release(io_context_t ctx,bool retain){
 if(direct_mode()){
  demand(ctx && direct_stats.borrowed.load()>0,"invalid direct release");
  ++direct_stats.pending;auto start=Clock::now();int rc=destroy_call(ctx);
  direct_stats.destroy_ns+=uint64_t(seconds(start)*1e9);--direct_stats.pending;
  demand(rc==0,"direct io_destroy failed; retained live quota");
  ++direct_stats.destroy;--direct_stats.live;--direct_stats.borrowed;return;
 }
 auto& p=*impl_;supplement_trace::event("lock_wait_start");std::unique_lock<std::mutex> guard(p.lock);supplement_trace::event("lock_acquired");
 for(size_t i=0;i<p.limit;++i){auto& s=p.slots[i];if(s.ctx!=ctx || s.state!=Impl::BORROWED)continue;
  demand(s.refs>0,"lease refcount underflow");--s.refs;--p.stats.borrowed;
  if(s.refs)return;
  if(tls_mode()?!s.exited:retain)s.state=Impl::IDLE;else p.dispose(i,guard);
  return;
 }
 throw std::runtime_error("invalid context lease release");
}
void ContextPool::reference_thread_exit(io_context_t ctx,uint64_t generation){
 auto& p=*impl_;supplement_trace::event("lock_wait_start");std::unique_lock<std::mutex> guard(p.lock);supplement_trace::event("lock_acquired");if(generation!=p.generation)return;
 for(size_t i=0;i<p.limit;++i){auto& s=p.slots[i];if(s.ctx!=ctx)continue;
  demand(s.state==Impl::BORROWED || s.state==Impl::IDLE,"TLS exit invalid state");s.exited=true;
  if(!s.refs)p.dispose(i,guard);return;
 }
}
void ContextPool::retire_current_thread_reference_context(){
 demand(tls_mode(),"retire requires TLS");
 if(reference.owner==this && reference.ctx){reference_thread_exit(reference.ctx,reference.generation);reference.ctx=0;}
}
void ContextPool::shutdown(){
 if(direct_mode()){demand(direct_stats.live==0 && direct_stats.borrowed==0 && direct_stats.pending==0,"direct shutdown before reader drain");return;}
 auto& p=*impl_;supplement_trace::event("lock_wait_start");std::unique_lock<std::mutex> guard(p.lock);supplement_trace::event("lock_acquired");
 demand(!p.stats.borrowed && !p.pending,"shutdown requires completed leases and pending operations");
 p.closing=true;for(size_t i=0;i<p.limit;++i)if(p.slots[i].ctx)p.dispose(i,guard);
 demand(!p.reserved && !p.stats.live,"shutdown resource residue");p.generation=++generations;p.closing=false;
}
PoolStats ContextPool::stats() const{
 if(direct_mode()){
  PoolStats s;s.setup=direct_stats.setup;s.destroy=direct_stats.destroy;s.live=direct_stats.live;s.peak_live=direct_stats.peak;
  s.borrowed=direct_stats.borrowed;s.leases=direct_stats.leases;s.live_capacity=s.live*1024;s.peak_live_capacity=s.peak_live*1024;
  s.setup_s=direct_stats.setup_ns.load()/1e9;s.destroy_s=direct_stats.destroy_ns.load()/1e9;return s;
 }
 std::lock_guard<std::mutex> guard(impl_->lock);return impl_->stats;
}
uint64_t ContextPool::pending() const{
 if(direct_mode())return direct_stats.pending;
 std::lock_guard<std::mutex> guard(impl_->lock);return impl_->pending;
}


PageStore::PageStore(size_t budget) {
  demand(budget>=131072,"cache budget too small");
  // Charge a conservative working reserve for <=13 concurrent 1024-request miss
  // vectors and allocator overhead. No unbounded cache map/list allocations.
  const size_t reserve=std::min(size_t(1048576),budget/16);
  capacity_=(budget-reserve-16384)/(4096+sizeof(Slot)+16);
  bucket_count_=1;while(bucket_count_<capacity_*2)bucket_count_*=2;
  size_t offset=((capacity_*sizeof(Slot)+bucket_count_*sizeof(int)+4095)/4096)*4096;
  const size_t bytes=offset+capacity_*4096;
  demand(posix_memalign(&arena_,4096,bytes)==0,"cache allocation failed");
  stats_.accounted_bytes=malloc_usable_size(arena_)+sizeof(*this)+reserve;
  if(stats_.accounted_bytes>budget){std::free(arena_);arena_=nullptr;throw std::runtime_error("cache accounting exceeds budget");}
  std::memset(arena_,0,bytes);
  slots_=static_cast<Slot*>(arena_);buckets_=reinterpret_cast<int*>(slots_+capacity_);pages_=static_cast<char*>(arena_)+offset;
  stats_.capacity=capacity_;std::fill(buckets_,buckets_+bucket_count_,-1);
}
PageStore::~PageStore() {try{finish();}catch(const std::exception& e){std::cerr<<"dirty cache final cleanup failed: "<<e.what()<<'\n';std::terminate();}std::free(arena_);}
void PageStore::bind(int fd,uint64_t generation) {
  std::lock_guard<std::mutex> guard(mutex_);struct stat st{};
  demand(fstat(fd,&st)==0 && S_ISREG(st.st_mode),"cache bind requires a regular file");
  if(fd_>=0 && dev_==uint64_t(st.st_dev) && ino_==uint64_t(st.st_ino) && generation_==generation)return;
  clear_locked();if(fd_>=0)demand(::close(fd_)==0,"close old cache fd failed");
  fd_=dup(fd);demand(fd_>=0,"duplicate cache fd failed");dev_=st.st_dev;ino_=st.st_ino;generation_=generation;
}
bool PageStore::matches(int fd) const {std::lock_guard<std::mutex> guard(mutex_);struct stat st{};return fd_>=0 && fstat(fd,&st)==0 && dev_==uint64_t(st.st_dev) && ino_==uint64_t(st.st_ino);}
int PageStore::find(uint64_t sector) const {for(int i=buckets_[sector&(bucket_count_-1)];i!=-1;i=slots_[i].hash_next)if(slots_[i].sector==sector)return i;return -1;}
void PageStore::touch(int i) {
  if(head_==i)return;
  auto& s=slots_[i];if(s.prev!=-1)slots_[s.prev].next=s.next;if(s.next!=-1)slots_[s.next].prev=s.prev;if(tail_==i)tail_=s.prev;
  s.prev=-1;s.next=head_;if(head_!=-1)slots_[head_].prev=i;head_=i;if(tail_==-1)tail_=i;
}
void PageStore::flush(int i) {
  if(!slots_[i].dirty)return;
  ssize_t done;do{done=pwrite(fd_,pages_+size_t(i)*4096,4096,slots_[i].sector*4096);}while(done<0 && errno==EINTR);
  demand(done==4096,"dirty page write failed/short");record_io(true,4096,1,true);slots_[i].dirty=false;--stats_.dirty;++stats_.dirty_flushes;
}
int PageStore::reserve(uint64_t sector) {
  int i=find(sector);if(i!=-1){touch(i);return i;}
  if(stats_.resident<capacity_)i=int(stats_.resident++);
  else {
    i=tail_;flush(i);++stats_.evictions;
    int* link=&buckets_[slots_[i].sector&(bucket_count_-1)];while(*link!=i)link=&slots_[*link].hash_next;*link=slots_[i].hash_next;
    if(slots_[i].prev!=-1) slots_[slots_[i].prev].next=-1;
    tail_=slots_[i].prev;
    if(head_==i) head_=-1;
  }
  auto& s=slots_[i];s={sector,-1,-1,buckets_[sector&(bucket_count_-1)],true,false};
  buckets_[sector&(bucket_count_-1)]=i;touch(i);return i;
}
bool PageStore::get(uint64_t sector,void* out) {
  std::lock_guard<std::mutex> guard(mutex_);demand(fd_>=0 && sector>0,"cache get outside binding or header page");++stats_.reads;
  int i=find(sector);if(i==-1)return false;touch(i);std::memcpy(out,pages_+size_t(i)*4096,4096);++stats_.hits;return true;
}
void PageStore::put(uint64_t sector,const void* data,bool dirty) {
  demand(fd_>=0 && sector>0,"cache put outside binding or header page");int i=reserve(sector);
  if(!dirty && slots_[i].dirty)return; // never overwrite a newer dirty page with a late fill
  std::memcpy(pages_+size_t(i)*4096,data,4096);
  if(dirty){++stats_.writes;if(!slots_[i].dirty){slots_[i].dirty=true;++stats_.dirty;}}
}
void PageStore::put_clean(uint64_t s,const void* p){std::lock_guard<std::mutex> guard(mutex_);put(s,p,false);}
void PageStore::put_dirty(uint64_t s,const void* p){std::lock_guard<std::mutex> guard(mutex_);put(s,p,true);}
void PageStore::clear_locked() {
  for(size_t i=0;i<stats_.resident;++i)flush(int(i));
  std::fill(buckets_,buckets_+bucket_count_,-1);head_=tail_=-1;stats_.resident=0;
}
void PageStore::boundary(bool retain){std::lock_guard<std::mutex> guard(mutex_);if(!retain)clear_locked();}
void PageStore::finish(){std::lock_guard<std::mutex> guard(mutex_);clear_locked();if(fd_>=0){demand(::close(fd_)==0,"close cache fd failed");fd_=-1;}}
CacheStats PageStore::stats() const {std::lock_guard<std::mutex> guard(mutex_);return stats_;}
ContextPool& pool(){static ContextPool instance(tls_mode()?17:13);return instance;}
bool persistent_aio(){return enabled("GRT_COMPOSE_A");}
bool cache_enabled(){const char* v=std::getenv("GRT_COMPOSE_C");return v && (std::strcmp(v,"0")==0 || std::strcmp(v,"1")==0);}
PageStore* store(){return active_store?global_store:nullptr;}
void begin_batch(const char* path){supplement_trace::event("batch_begin");demand(!active_store,"overlapping cache batch");phase.store(1);if(!cache_enabled())return;if(!global_store)global_store=new PageStore;int fd=::open(path,O_RDWR|O_DIRECT);demand(fd>=0,"open cache graph failed");try{global_store->bind(fd,++batch_generation);}catch(...){::close(fd);throw;}::close(fd);active_store=true;}
void phase_boundary(){supplement_trace::event("phase_boundary",phase.load());if(active_store)global_store->boundary(enabled("GRT_COMPOSE_C"));phase.fetch_add(1);}
void end_batch(){supplement_trace::event("batch_end");if(active_store){global_store->finish();active_store=false;}phase.store(4);}
void shutdown(){end_batch();pool().shutdown();}
void record_io(bool write,uint64_t bytes,uint64_t requests,bool sync_write){
  auto& c=counters[std::min(phase.load(),size_t(4))];
  if(write){c.write_requests.fetch_add(requests);c.write_bytes.fetch_add(bytes);if(sync_write)c.sync_write_requests.fetch_add(requests);}
  else {c.read_requests.fetch_add(requests);c.read_bytes.fetch_add(bytes);}
}
void report(const char* path) {
  if(!path) return;
  const auto p=pool().stats();CacheStats c;if(global_store)c=global_store->stats();std::ofstream out(path);
  demand(bool(out),"open runtime report failed");
  out<<"{\"aio_setup\":"<<p.setup<<",\"aio_destroy\":"<<p.destroy<<",\"aio_live\":"<<p.live<<",\"aio_borrowed\":"<<p.borrowed<<",\"aio_peak_live\":"<<p.peak_live<<",\"aio_live_capacity\":"<<p.live_capacity<<",\"aio_peak_live_capacity\":"<<p.peak_live_capacity<<",\"aio_setup_s\":"<<p.setup_s<<",\"aio_destroy_s\":"<<p.destroy_s
     <<",\"aio_pending\":"<<pool().pending()<<",\"cache_reads\":"<<c.reads<<",\"cache_hits\":"<<c.hits<<",\"cache_writes\":"<<c.writes<<",\"dirty_flushes\":"<<c.dirty_flushes<<",\"evictions\":"<<c.evictions<<",\"resident\":"<<c.resident<<",\"dirty\":"<<c.dirty<<",\"capacity\":"<<c.capacity<<",\"accounted_cache_bytes\":"<<c.accounted_bytes<<",\"reader_io_by_phase\":[";
  for(size_t i=0;i<5;++i){if(i)out<<',';const auto& v=counters[i];out<<"{\"phase\":"<<i<<",\"read_requests\":"<<v.read_requests.load()<<",\"write_requests\":"<<v.write_requests.load()<<",\"read_bytes\":"<<v.read_bytes.load()<<",\"write_bytes\":"<<v.write_bytes.load()<<",\"sync_write_requests\":"<<v.sync_write_requests.load()<<'}';}
  out<<"]}\n";
  out.close();demand(bool(out),"write runtime report failed");supplement_trace::dump(std::getenv("GRT_TRACE_PATH"));delete global_store;global_store=nullptr;
}
} // namespace composition

namespace grt_bulk {
namespace {
thread_local bool enabled=false;
io_context_t contexts[4]{};
uint64_t reads=0,writes=0,read_submits=0,write_submits=0;
uint64_t read_hash=0,write_hash=0;
}
bool active(){return enabled;}
bool parallel(){const char* p=std::getenv("GRT_EXEC_PARALLEL");return p && std::strcmp(p,"1")==0;}
void begin(){
  if(enabled)throw std::runtime_error("nested bulk scope");
  if(parallel())for(unsigned i=0;i<4;++i)contexts[i]=composition::pool().acquire(256);
  reads=writes=read_submits=write_submits=read_hash=write_hash=0;enabled=true;
}
void end(){
  if(!enabled){std::terminate();}
  enabled=false;
  for(auto& ctx:contexts)if(ctx){composition::pool().release(ctx,composition::persistent_aio());ctx=0;}
  std::cout<<"GRT_EXEC_IO "<<(parallel()?1:0)<<" "<<reads<<" "<<writes<<" "<<read_submits<<" "<<write_submits<<" "<<read_hash<<" "<<write_hash<<"\n";
}
io_context_t context(unsigned worker){if(worker>=4 || !contexts[worker])throw std::runtime_error("unleased bulk context");return contexts[worker];}
void record(bool write,uint64_t requests,uint64_t submissions,uint64_t hash){
  if(write){writes+=requests;write_submits+=submissions;write_hash=(write_hash^hash)*1099511628211ull;}
  else{reads+=requests;read_submits+=submissions;read_hash=(read_hash^hash)*1099511628211ull;}
}
}
