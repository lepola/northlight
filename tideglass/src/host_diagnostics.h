#pragma once
#include "host_api.h"
// Optional companion ABI: existing hosts/render ABI remain compatible. Present
// callbacks are on the context's device owner. They neither retain game
// pointers nor submit rendering work. Raw QPC/thread/frame values correlate
// with host logs.
constexpr uint32_t kWaterDiagnosticsABI = 1;
struct WaterHostDiagnosticsAPI {
  uint32_t abi, size;
  int(__cdecl *present_begin)(WaterHostContext *, uint32_t source,
                              uint64_t hostFrame);
  int(__cdecl *present_ready)(WaterHostContext *);
  int(__cdecl *present_end)(WaterHostContext *, int32_t result);
};
// Optional outer-call capability, independent of render and Present ABI 1.
constexpr uint32_t kWaterTraceABI = 1;
struct WaterHostTraceAPI {
  uint32_t abi, size;
  uint64_t(__cdecl *begin_call)(WaterHostContext *);
  int(__cdecl *end_call)(WaterHostContext *, uint64_t, const char *);
};
using WaterHostGetTraceAPIFn = const WaterHostTraceAPI *(__cdecl *)(uint32_t,
                                                                    uint32_t);
using WaterHostGetDiagnosticsAPIFn =
    const WaterHostDiagnosticsAPI *(__cdecl *)(uint32_t, uint32_t);
