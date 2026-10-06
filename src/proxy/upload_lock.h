#pragma once
#include <d3d9.h>
#include <atomic>
// 0.3.192 (CS): these Locks run on the replay thread in stream mode; the flags and ranges are the ones below, unchanged.

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
/* 0.3.192 (DXVK3): the Lock flags of our READONLY read-backs of game buffers (draw_snapshot.h,
   geometry_capture.h, terrain_capture_bounds.h).
   DXVK 3.1.1 LockBuffer (d3d9_device.cpp:5493) strips D3DLOCK_READONLY for every non-MANAGED pool.
   A DYNAMIC (direct-mapped) buffer then has skipWait=false (:5564): every read-back waits through
   WaitForResource (CS sync, flush, GPU wait). NOOVERWRITE survives for the DEFAULT pool and forces
   skipWait, which removes the wait. Whatever the flags, 3.1.1 still marks the locked range dirty and sets
   m_vbSlotTracking.needsUpload (:5509-5519), and an Unlock of a BUFFER-mode (non-DYNAMIC) DEFAULT buffer
   still runs FlushBuffer (:5597-5660: ThrottleAllocation, staging, memcpy, copyBuffer): no flag avoids
   that; the LOCK METER read-back classes (dynamic / defaultStatic) show how much of it is left.
   DXVK 2.7.1 and 1.10.3 keep READONLY (so skipWait is already true and no dirty range is set): the
   flag would only add the skip of the needsReadback wait of a buffer that ProcessVertices wrote, so it
   is enabled only when the backend that actually loaded is DXVK >= 3 (renderer.cpp, after the
   BACKEND selected= log; a fallback to the system d3d9, a non-DXVK BackendPath or the dxvk -> dxvk2
   marker fallback leave it off). MANAGED buffers strip NOOVERWRITE: harmless. A direct-mapped Unlock returns
   before SetMapFlags(0), so the OR-ed map flags are never read. The NOOVERWRITE promise (no write
   through the pointer) holds trivially: these locks only read. The READONLY bit stays, tracked_buffers.h
   keys the revision on it. Set once before CreateDevice, relaxed loads afterwards. */
inline std::atomic<bool> ReadBackNoOverwrite{false};
inline DWORD readBackLock(){return DWORD(D3DLOCK_READONLY)|(ReadBackNoOverwrite.load(std::memory_order_relaxed)?DWORD(D3DLOCK_NOOVERWRITE):0u);}
}
