#pragma once
// In-DLL settings window (Dear ImGui), drawn by the Device over the finished frame and toggled with Ctrl+Shift+F11
// ([Renderer] SettingsOverlayKey).
// Input comes from a subclass of the device window's procedure, chained to the previous one; nothing here reads,
// writes or calls the game's memory or code. Threads: create() on the device creation thread; draw() and invalidate()
// on the thread that executes the Device (the replay thread with the command stream), under the device gate; the
// window procedure on the game's window thread only queues messages (overlay_input.h). Destroy before the device.
#include <memory>
#include <d3d9.h>

namespace NorthlightSettings { class Service; }

namespace NorthlightOverlay {
using Log=void(*)(const char* line);
class Overlay {
public:
    virtual ~Overlay()=default;
    // Relaxed atomic loads: false while the window is closed and the panel has nothing pending (nothing else runs then).
    // While closed with an unsaved draft it stays true: draw() then keeps the service work moving and shows a one-line reminder.
    virtual bool visible()const=0;
    // Returns whether the window hook owns F10; drains presses even without foreground focus.
    // A missing/failed hook leaves the renderer's polling fallback in charge.
    virtual bool masterHotkey(bool foreground,bool& pressed)=0;
    // Draws onto back buffer 0 of the implicit swap chain, in its own BeginScene/EndScene. Call only for a Present of that
    // swap chain. The caller holds a RawScope and a SavedState: state need not be restored.
    // uiWhite=0 uses SDR; positive nits select linear scRGB output.
    virtual void draw(float uiWhite=0,IDirect3DSurface9* output=nullptr)=0;
    // Before the device's Reset: releases every D3DPOOL_DEFAULT object (recreated lazily by the next draw).
    virtual void invalidate()=0;
};
// enabled=false ([Renderer] SettingsOverlay=0): logs that and returns nullptr; no window subclass. hotkey: the
// SettingsOverlayKey text ("Ctrl+Shift+F11"; empty or null = that default; unusable = the default and one log line).
// service may be null (the window then says the settings are unavailable). device is the real (extension) device; it is AddRef'ed.
std::unique_ptr<Overlay> create(IDirect3DDevice9* device,bool enabled,const char* hotkey,std::shared_ptr<NorthlightSettings::Service> service,Log log);
} // namespace NorthlightOverlay
