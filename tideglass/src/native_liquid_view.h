#pragma once
#include <cstdint>
struct Config;

namespace water {
// Shared native environment policy, before the game records any world draws.
// Configuration may arrive from replay; all native reads/writes stay on the game thread.
void ConfigureNativeLiquidView(const Config&,bool rendererAvailable=true);
void DisableNativeLiquidView();
bool InstallNativeLiquidView();
bool RestoreNativeLiquidView();
void BeginNativeLiquidView();
void EndNativeLiquidView();
}
