#pragma once
// Process-wide hooks between the command stream (src/stream/) and the rest of the DLL. Every one is inert while the
// stream is inactive (CommandStream=0, or the replay thread failed to start): null pointers and zero thread ids.
// Header-only and portable except the Win32 file read; the stream sets these, nothing here starts a thread.
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include "quality_settings.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace NorthlightStream {
// 0.3.192 (CS): the identity world_renderer.h captureUP keys its cache on, instead of the vertex pointer. The replay
// thread sets it around a replayed DrawPrimitiveUP/DrawIndexedPrimitiveUP to the pointer the GAME passed (the recorded
// copy lives at a different address each frame). The data itself is still read from the pointer the call received.
inline thread_local const void* upIdentity=nullptr;
// Maps a pointer the game stored in its own memory (a stream proxy) to the Device-level object it wraps; set by the
// stream while active. celestial_disc_native.h applies it before the registry's rawOf. Null = identity.
inline const void* (*innerOf)(const void*)=nullptr;
inline const void* inner(const void* p){return innerOf&&p?innerOf(p):p;}
// True from before the Device is constructed until the process ends, only when the replay thread runs. It must be set
// before WorldRenderer / StaticShadowGpu are constructed (their core budgets read it once). A plain atomic with
// relaxed loads: written once on the game thread during Factory::CreateDevice, never cleared.
inline std::atomic<bool> streamActive{false};
// The memory guard's pressure decision (published by the Device's finishFrameImpl, which runs on the replay thread while
// streaming); the game side reads it at its next Present and halves the queue budget. Written only on a change.
inline std::atomic<bool> memoryPressure{false};
// Thread ids for the GATE line (0 = direct path / not started).
inline std::atomic<unsigned long> gameTid{0},replayTid{0};
// hardware_concurrency() minus the replay thread's core while the stream runs (0 = unknown stays 0). The game thread
// no longer executes D3D, but the replay thread is a full extra consumer next to it, so every worker budget loses one.
inline unsigned cores(){const unsigned hw=std::thread::hardware_concurrency();return hw>1&&streamActive.load(std::memory_order_relaxed)?hw-1:hw;}
// CommandStream from northlight-quality.ini text; the same loader and key the WorldRenderer uses later, so both agree.
// Anything unreadable gives 0 (the direct path): the stream is only ever entered on an explicit, parsed 1.
// 0.3.200 (pipeline): framesAhead (optional) receives StreamFramesAhead from the same load (1 when nothing could be read).
// 0.3.200 (frame skip): frameSkip (optional) receives StreamFrameSkip the same way (0 when nothing could be read).
inline bool commandStreamFromText(const std::string& own,bool hasFile=true,unsigned* framesAhead=nullptr,unsigned* frameSkip=nullptr){
    if(framesAhead)*framesAhead=1;if(frameSkip)*frameSkip=0;
    try{std::istringstream in(NorthlightQuality::narrow(own));std::vector<std::string> problems;
        const NorthlightQuality::Settings s=NorthlightQuality::load(hasFile?&in:nullptr,nullptr,problems);
        if(framesAhead)*framesAhead=s.streamFramesAhead;
        if(frameSkip)*frameSkip=s.streamFrameSkip;
        return s.commandStream!=0;}
    catch(...){return false;}
}
#ifdef _WIN32
// Called by Factory::CreateDevice before the real device exists: rootPath + northlight-quality.ini (wide path, as
// WorldRenderer::readSmallFile). An absent file means the code default (1).
inline bool commandStreamRequested(const wchar_t* rootPath,unsigned* framesAhead=nullptr,unsigned* frameSkip=nullptr){
    std::string text;bool has=false;if(framesAhead)*framesAhead=1;if(frameSkip)*frameSkip=0;
    try{
        const std::wstring path=std::wstring(rootPath?rootPath:L"")+L"northlight-quality.ini";
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f!=INVALID_HANDLE_VALUE){char buffer[4096];DWORD got=0;has=true;
            while(ReadFile(f,buffer,sizeof buffer,&got,nullptr)&&got&&text.size()<262144)text.append(buffer,got);
            CloseHandle(f);}
    }catch(...){return false;}
    return commandStreamFromText(text,has,framesAhead,frameSkip);
}
#endif
} // namespace NorthlightStream
