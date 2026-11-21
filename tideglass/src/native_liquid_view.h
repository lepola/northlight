#pragma once
#include <cstdint>
struct Config;

namespace water {
// Standalone's native environment policy, before the game records any world draws. The cooperative core
// has none: it never writes to the client, and Northlight's art layer supplies above-water light underwater.
// Configuration may arrive from replay; all native reads/writes stay on the game thread.
void ConfigureNativeLiquidView(const Config&,bool rendererAvailable=true);
void DisableNativeLiquidView();
bool InstallNativeLiquidView();
bool RestoreNativeLiquidView();
void BeginNativeLiquidView();
void EndNativeLiquidView();
}
