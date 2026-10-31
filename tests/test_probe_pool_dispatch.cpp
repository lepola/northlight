#include "world_probe_cache.h"
#include "gi_solve_pool.h"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <stdexcept>
static std::atomic<unsigned> allocations{0};
#if !__has_feature(thread_sanitizer)
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#endif
int main(){unsigned cases=0;
 for(unsigned helpers=0;helpers<5;++helpers){NorthlightGI::SolvePool pool(helpers,nullptr);
  for(unsigned generation=0;generation<2000;++generation){std::array<unsigned,128> hits{};std::array<unsigned,12> capture{};capture[7]=generation;
   const unsigned count=generation%129;const auto job=[&,capture](size_t i){assert(capture[7]==generation);++hits[i];};static_assert(sizeof job>2*sizeof(void*),"exercise heap-sized captures");
   const auto before=allocations.load();pool.run(count,job);assert(allocations.load()==before);
   for(unsigned i=0;i<128;++i)assert(hits[i]==unsigned(i<count));++cases;
  }
  bool thrown=false;try{pool.run(128,[](size_t i){if(i==17)throw std::runtime_error("expected");});}catch(const std::runtime_error&){thrown=true;}assert(thrown);
  std::array<unsigned,128> hits{};pool.run(128,[&](size_t i){++hits[i];});for(auto h:hits)assert(h==1);
 }
#if __has_feature(thread_sanitizer)
 const unsigned allocationCounter=0;
#else
 const unsigned allocationCounter=1;
#endif
 std::printf("PROBE_POOL_DISPATCH_PASS generations=%u allocationCounter=%u exactOnce=1 exceptions=1 timings=0\n",cases,allocationCounter);
}
