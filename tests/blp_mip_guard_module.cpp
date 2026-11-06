#include "blp_mip_guard.h"

// Lifetime oracle: the exact production installer in a module with no other
// features that could retain it. No new exports are added to Northlight.
extern "C" __declspec(dllexport) BOOL ApplyBlpGuard(HMODULE image){
    return !std::strcmp(NorthlightBlpMipGuard::install(image),"active");
}
