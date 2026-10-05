#pragma once
#include <d3d9.h>

namespace NorthlightUpload {
/* 0.3.151: the Lock flags of our fresh DEFAULT|WRITEONLY buffers (replay_gpu_cache.h,
   replay_gpu_batches.h). Each buffer is created by its
   upload, written once in disjoint ranges, published only after its last Unlock and
   never locked again, so no draw can read a locked range.
   DXVK maps DEFAULT|WRITEONLY buffers directly (1.10.3 d3d9_common_buffer.h:92-95,
   2.7.1 d3d9_common_buffer.cpp:81-96). A direct buffer has no CS sequence number
   (SynchronizeAll), and a flags-0 lock does not skip the wait (LockBuffer skipWait,
   1.10.3 d3d9_device.cpp:4669, 2.7.1 :5476): every lock drained the whole CS thread,
   5-41 ms spikes independent of the byte count. NOOVERWRITE skips the wait and
   returns the same mapped slice: same bytes, same image. Creation zero-fills a
   direct buffer synchronously (host-visible memset in D3D9Initializer::InitBuffer),
   so no queued command writes over the upload.
   0.3.189: DXVK 3.x (d3d9_common_buffer.cpp DetermineMapMode) maps only DYNAMIC buffers
   directly; DEFAULT|WRITEONLY buffers use BUFFER mode with a persistent host-cached staging
   copy. Lock skips the CS wait anyway (skipWait = !needsReadback && !directMapping) and
   Unlock copies through FlushBuffer (staging + ThrottleAllocation). NOOVERWRITE is kept for
   the DEFAULT pool: ineffective but safe there, and still needed for the 2.7.1 (dxvk2) backend.
   Never pass it to re-lock a buffer a draw may have read: that is a silent write race
   (test_upload_locks.py lists every buffer Lock). MANAGED buffers drop the flag.
   false: flags 0, the 0.3.150 behaviour. */
inline constexpr bool NoOverwriteFreshBuffers=true;
inline constexpr DWORD FreshBufferLock=NoOverwriteFreshBuffers?DWORD(D3DLOCK_NOOVERWRITE):DWORD(0);
}
