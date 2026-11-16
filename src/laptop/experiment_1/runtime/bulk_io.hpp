#pragma once
#include <cstdint>
#include <libaio.h>
namespace grt_bulk {
void begin();void end();bool active();bool parallel();
io_context_t context(unsigned worker);
void record(bool write,uint64_t requests,uint64_t submissions,uint64_t hash);
struct Scope {Scope(){begin();}~Scope(){end();}};
}
