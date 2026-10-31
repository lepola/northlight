#pragma once
#include <atomic>
namespace NorthlightDiagnosticCapture {
enum Scope : unsigned { World=1, AO=2, Materials=4, Fog=8 };
inline std::atomic<unsigned>& pending(){static std::atomic<unsigned> value{0};return value;}
inline void request(unsigned scope){pending().fetch_or(scope&15,std::memory_order_relaxed);}
inline unsigned take(){return pending().exchange(0,std::memory_order_relaxed);}
}
