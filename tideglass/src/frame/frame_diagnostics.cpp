#include "frame/frame_diagnostics.h"
#include "base/log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace water {
std::atomic<unsigned> FrameDiagnostics::s_active{0};
uint64_t FrameDiagnostics::Now() {
  LARGE_INTEGER q{};
  QueryPerformanceCounter(&q);
  return q.QuadPart;
}
FrameDiagnostics::FrameDiagnostics() {
  static std::atomic<uint64_t> ids{0};
  m_identity = ++ids;
  LARGE_INTEGER f{};
  QueryPerformanceFrequency(&f);
  m_frequency = f.QuadPart;
}
FrameDiagnostics::~FrameDiagnostics() {
  Finish("shutdown");
  if (m_writer.joinable())
    m_writer.join();
}
void FrameDiagnostics::Configure(const Config &c, const std::string &directory,
                                 unsigned source) {
  if (GetCurrentThreadId() != m_owner)
    return;
  if (m_active && (c.profileRequest != m_seen || !c.profileRequest))
    Finish("request-change");
  if (m_active && source)
    m_source = source;
  if (m_active || !c.profileRequest || c.profileRequest == m_seen)
    return;
  if (m_writing.load())
    return; // Retry a changed request after the bounded writer retires.
  m_seen = c.profileRequest;
  if (m_writer.joinable())
    m_writer.join();
  m_source = source;
  m_mode = c.profileMode;
  m_calls = c.profileCalls && (m_mode & 1);
  m_nestedCalls = c.profileCalls >= 2;
  m_callDepth = 0;
  m_limit = std::clamp(c.profileFrames, 16, 60000);
  m_seconds = std::clamp(c.profileSeconds, 1, 600);
  try {
    m_rows.clear();
    m_rows.reserve(m_limit);
    m_prefix = directory + "water-profile-" + std::to_string(m_seen) + "-" +
               std::to_string(GetCurrentProcessId()) + "-" +
               std::to_string(m_identity);
  } catch (...) {
    WATER_LOG_INFO("water profile rejected: allocation request=%d", m_seen);
    return;
  }
  const auto a = Now();
  for (unsigned i = 0; i < 1024; ++i)
    Now();
  const auto b = Now();
  m_clockNs = double(b - a) * 1e9 / m_frequency / 1024;
  FILETIME wall{};
  GetSystemTimeAsFileTime(&wall);
  m_wall = (uint64_t(wall.dwHighDateTime) << 32) | wall.dwLowDateTime;
  m_started = Now();
  m_row = {};
  m_stage = -1;
  m_sequence = 0;
  m_active = true;
  if (m_mode & 1)
    ++s_active;
  WATER_LOG_INFO(
      "water profile armed: %s mode=%u seconds=%u capacity=%u source=%u",
      m_prefix.c_str(), m_mode, m_seconds, m_limit, m_source);
}
void FrameDiagnostics::Begin(FrameStamp frame, const FrameInputs *input) {
  if (!Active())
    return;
  if (m_row.world && m_row.world != frame.frame)
    Commit();
  if (!Active())
    return;
  if (!m_row.begin) {
    m_row.begin = Now();
    ++m_row.clockReads;
    m_row.thread = GetCurrentThreadId();
    m_row.tick = GetTickCount();
  }
  if (m_mode & 2)
    m_row.gpuStatus = 0;
  m_row.session = frame.session;
  m_row.generation = frame.generation;
  m_row.world = frame.frame;
  if (input)
    Input(*input);
}
void FrameDiagnostics::Input(const FrameInputs &in) {
  if (!Active())
    return;
  double norm = 0;
  for (unsigned i = 0; i < 3; ++i) {
    if (!std::isfinite(in.cameraPosition[i]) ||
        !std::isfinite(in.cameraRelativeView[i * 4 + 2]))
      return;
    norm += double(in.cameraRelativeView[i * 4 + 2]) *
            in.cameraRelativeView[i * 4 + 2];
  }
  if (!in.viewport.Width || !in.viewport.Height || norm < .000001)
    return;
  m_row.camera = true;
  m_row.map = in.mapId;
  m_row.underwater = in.cameraInWater;
  m_row.slime = in.cameraInSlime;
  m_row.movers = in.moverCount;
  m_row.boats = in.boatCount;
  m_row.splashes = in.splashCount > 0 ? unsigned(in.splashCount) : 0;
  m_row.fog = in.fogColor;
  m_row.daylight = in.daylight;
  m_row.rain = in.rain;
  m_row.fogStart = in.fogStart;
  m_row.fogEnd = in.fogEnd;
  for (unsigned i = 0; i < 3; ++i) {
    m_row.position[i] = in.cameraPosition[i];
    m_row.direction[i] = in.cameraRelativeView[i * 4 + 2];
  }
}
void FrameDiagnostics::Capture(uint64_t end, uint64_t ticks, unsigned thread) {
  if (!Active())
    return;
  m_row.capture = end;
  m_row.captureTicks = ticks;
  m_row.captureThread = thread;
}
int FrameDiagnostics::Enter(Stage stage) {
  if (!Cpu())
    return -2;
  const auto now = Now();
  ++m_row.clockReads;
  const int old = m_stage;
  Charge(now);
  m_stage = int(stage);
  m_last = now;
  return old;
}
void FrameDiagnostics::Leave(int previous) {
  if (previous == -2 || !m_active)
    return;
  const auto now = Now();
  ++m_row.clockReads;
  Charge(now);
  m_stage = previous;
  m_last = now;
}
void FrameDiagnostics::Charge(uint64_t now) {
  if (m_stage >= 0 && now >= m_last)
    m_row.cpu[m_stage] += now - m_last;
}
uint64_t FrameDiagnostics::BeginCall() {
  if (!Cpu() || !m_calls || (m_callDepth && !m_nestedCalls))
    return 0;
  if (!m_callDepth) {
    m_innerTicks = 0;
    m_innerName[0] = 0;
  }
  ++m_callDepth;
  ++m_row.clockReads;
  return Now();
}
void FrameDiagnostics::EndCall(uint64_t token, const char *name) {
  if (!token || !Active() || !m_callDepth || token < m_started)
    return;
  const auto now = Now();
  ++m_row.clockReads;
  const auto elapsed = now >= token ? now - token : 0;
  ++m_row.calls;
  const auto label = [](char (&out)[96], const char *value) {
    std::snprintf(out, sizeof(out), "%s", value ? value : "unknown");
    for (char &c : out)
      if (c == '"' || c == ',' || c == '\n' || c == '\r')
        c = '_';
  };
  if (--m_callDepth) {
    if (elapsed > m_innerTicks) {
      m_innerTicks = elapsed;
      label(m_innerName, name);
    }
    return;
  }
  m_row.callTicks += elapsed;
  if (elapsed > m_row.largestCallTicks) {
    m_row.largestCallTicks = elapsed;
    label(m_row.largestCall, name);
    m_row.largestInnerTicks = m_innerTicks;
    label(m_row.largestInner, m_innerName);
  }
}
void FrameDiagnostics::PresentBegin(uint64_t hostFrame) {
  if (!Active())
    return;
  m_row.hostFrame = hostFrame;
  if (!m_row.begin)
    m_row.begin = Now();
  m_row.present = Now();
  m_row.clockReads += 2;
  m_row.thread = GetCurrentThreadId();
  m_row.tick = GetTickCount();
}
void FrameDiagnostics::PresentReady() {
  if (Active()) {
    m_row.submit = Now();
    ++m_row.clockReads;
  }
}
void FrameDiagnostics::PresentEnd(HRESULT hr) {
  if (!Active())
    return;
  if (Cpu()) {
    FILETIME created{}, exited{}, kernel{}, user{};
    const auto ticks = [](FILETIME value) {
      return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
      m_row.threadCpu = ticks(kernel) + ticks(user);
      m_row.cpuValid |= 1;
    }
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel,
                        &user)) {
      m_row.processCpu = ticks(kernel) + ticks(user);
      m_row.cpuValid |= 2;
    }
  }
  m_row.returned = Now();
  ++m_row.clockReads;
  m_row.result = hr;
  m_row.presented = true;
  Commit();
}
void FrameDiagnostics::Commit() {
  if (!m_active || (!m_row.begin && !m_row.world))
    return;
  m_row.frame = ++m_sequence;
  m_row.source = m_source;
  m_row.mode = m_mode;
  m_row.callTiming = m_calls;
  m_rows.push_back(m_row);
  m_row = {};
  m_stage = -1;
  if (m_rows.size() >= m_limit)
    Finish("capacity");
  else if (Now() - m_started >= m_seconds * m_frequency)
    Finish("duration");
}
void FrameDiagnostics::GPU(uint64_t tag, bool valid, double window,
                           const double *passes, uint64_t mask) {
  auto assign = [&](Row &row) {
    row.gpuStatus = valid ? 1 : 0;
    row.gpuMask = mask;
    row.gpuWindow = window;
    if (valid && passes)
      std::copy(passes, passes + GpuTimer::kPasses, row.gpu.begin());
  };
  if (!Active() || !(m_mode & 2))
    return;
  if (m_row.world == tag) {
    assign(m_row);
    return;
  }
  const auto begin = m_rows.size() > 16 ? m_rows.size() - 16 : 0;
  for (auto i = m_rows.size(); i > begin; --i)
    if (m_rows[i - 1].world == tag) {
      assign(m_rows[i - 1]);
      return;
    }
}
void FrameDiagnostics::Finish(const char *reason) {
  if (!Active())
    return;
  if (m_row.begin || m_row.world) {
    m_row.frame = ++m_sequence;
    m_row.source = m_source;
    m_row.mode = m_mode;
    m_row.callTiming = m_calls;
    if (m_rows.size() < m_limit)
      m_rows.push_back(m_row);
  }
  m_active = false;
  if (m_mode & 1)
    --s_active;
  m_row = {};
  m_stage = -1;
  try {
    Write(reason);
  } catch (...) {
    m_writing.store(false);
    WATER_LOG_INFO("water profile write rejected: allocation %s",
                   m_prefix.c_str());
  }
}
void FrameDiagnostics::Write(const char *reason) {
  auto rows = std::make_shared<std::vector<Row>>(std::move(m_rows));
  const auto prefix = m_prefix;
  const auto hz = m_frequency, started = m_started, wall = m_wall;
  const auto mode = m_mode, seconds = m_seconds, capacity = m_limit;
  const unsigned callLevel = m_calls ? (m_nestedCalls ? 2 : 1) : 0;
  const auto clockNs = m_clockNs;
  const std::string why = reason;
  m_writing.store(true);
  try {
    m_writer = std::thread([this, rows, prefix, hz, started, wall, mode,
                            seconds, capacity, callLevel, clockNs, why] {
      try {
        const auto tmp = prefix + "-frames.csv.tmp",
                   path = prefix + "-frames.csv";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        bool ok = f != nullptr;
        if (f) {
          std::fputs(
              "schema,frame,host_frame,source,mode,session,generation,world_"
              "frame,thread,tick,begin_qpc,present_qpc,submit_qpc,return_qpc,"
              "capture_qpc,capture_cpu_ms,capture_thread,capture_age_ms,"
              "present_"
              "ms,host_finish_ms,present_outer_ms,presented,present_result,"
              "water_"
              "enabled,camera_valid,camera_x,camera_y,camera_z,view_x,view_y,"
              "view_z,producer,reason,preparation_state,preparation_ms,source_"
              "bytes,request,active_cpu_bytes,replacement_cpu_bytes,gpu_bytes,"
              "staging_bytes,uploads,draws,work,clock_reads,estimated_clock_us,"
              "gpu_status,gpu_window_ms",
              f);
          static const char *names[] = {
              "selection", "upload",     "owned_draw", "snapshot",
              "composite", "world_done", "ambient",    "puddles",
              "rings",     "slime",      "lifecycle",  "config_reload"};
          for (const char *name : names)
            std::fprintf(f, ",cpu_%s_ms", name);
          for (unsigned i = 0; i < GpuTimer::kPasses; ++i)
            std::fprintf(f, ",gpu_pass_%u_ms", i);
          std::fputs(
              ",map_id,daylight,rain,fog_color,camera_in_water,camera_in_"
              "slime,mover_count,boat_count,cpu_counter_valid,thread_cpu_100ns,"
              "process_cpu_100ns,call_timing,call_count,outer_calls_ms,largest_"
              "call_ms,largest_call,largest_call_inner_ms,largest_call_inner,"
              "fog_start,fog_end,owned_drawn,owned_terrain_cells,owned_instances,"
              "owned_fog_skipped,owned_past_fog,owned_triangles,splash_count\n",
              f);
          for (const auto &r : *rows) {
            const auto ms = [hz](uint64_t ticks) {
              return double(ticks) * 1000 / hz;
            };
            std::fprintf(
                f,
                "1,%llu,%llu,%u,%u,%llu,%llu,%llu,%u,%u,%llu,%llu,%llu,%llu,%"
                "llu,",
                (unsigned long long)r.frame, (unsigned long long)r.hostFrame,
                r.source, r.mode, (unsigned long long)r.session,
                (unsigned long long)r.generation, (unsigned long long)r.world,
                r.thread, r.tick, (unsigned long long)r.begin,
                (unsigned long long)r.present, (unsigned long long)r.submit,
                (unsigned long long)r.returned, (unsigned long long)r.capture);
            if (r.capture)
              std::fprintf(f, "%.6f", ms(r.captureTicks));
            std::fprintf(f, ",%u,", r.captureThread);
            if (r.capture && r.begin >= r.capture)
              std::fprintf(f, "%.6f", ms(r.begin - r.capture));
            std::fputc(',', f);
            if (r.presented && r.submit && r.returned >= r.submit)
              std::fprintf(f, "%.6f", ms(r.returned - r.submit));
            std::fputc(',', f);
            if (r.submit && r.present && r.submit >= r.present)
              std::fprintf(f, "%.6f", ms(r.submit - r.present));
            std::fputc(',', f);
            if (r.presented && r.present && r.returned >= r.present)
              std::fprintf(f, "%.6f", ms(r.returned - r.present));
            std::fprintf(
                f,
                ",%u,%ld,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,\"%s\",%u,%.6f,"
                "%"
                "llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%.6f,%d,",
                unsigned(r.presented), long(r.result), unsigned(r.water),
                unsigned(r.camera), r.position[0], r.position[1], r.position[2],
                r.direction[0], r.direction[1], r.direction[2], r.producer,
                r.reason, r.preparation, r.workerMilliseconds,
                (unsigned long long)r.sourceBytes,
                (unsigned long long)r.request,
                (unsigned long long)r.activeBytes,
                (unsigned long long)r.replacementBytes,
                (unsigned long long)r.gpuBytes,
                (unsigned long long)r.stagingBytes, r.uploads, r.draws, r.work,
                r.clockReads, r.clockReads * clockNs / 1000, r.gpuStatus);
            if (r.gpuStatus == 1)
              std::fprintf(f, "%.6f", r.gpuWindow);
            for (auto ticks : r.cpu) {
              std::fputc(',', f);
              if (r.mode & 1)
                std::fprintf(f, "%.6f", ms(ticks));
            }
            for (unsigned i = 0; i < GpuTimer::kPasses; ++i) {
              std::fputc(',', f);
              if (r.gpuStatus == 1 && (r.gpuMask & (uint64_t(1) << i)))
                std::fprintf(f, "%.6f", r.gpu[i]);
            }
            std::fprintf(
                f,
                ",%d,%.6f,%.6f,%u,%u,%u,%u,%u,%u,%llu,%llu,%u,%u,%.6f,"
                "%.6f,\"%s\",%.6f,\"%s\",%.3f,%.3f,%u,",
                r.map, r.daylight, r.rain, r.fog, r.underwater, r.slime,
                r.movers, r.boats, r.cpuValid, (unsigned long long)r.threadCpu,
                (unsigned long long)r.processCpu, unsigned(r.callTiming),
                r.calls, ms(r.callTicks), ms(r.largestCallTicks), r.largestCall,
                ms(r.largestInnerTicks), r.largestInner, r.fogStart, r.fogEnd,
                unsigned(r.ownedDrawn));
            if (r.ownedDrawn)
              std::fprintf(f, "%u,%u,%u,%u,%llu", r.ownedCells,
                           r.ownedInstances, r.ownedFogSkipped, r.ownedPastFog,
                           (unsigned long long)r.ownedTriangles);
            else
              std::fputs(",,,,", f);
            std::fprintf(f, ",%u\n", r.splashes);
          }
          ok = std::ferror(f) == 0;
          if (std::fclose(f) != 0)
            ok = false;
          if (ok)
            ok = std::rename(tmp.c_str(), path.c_str()) == 0;
        }
        FILE *meta = std::fopen((prefix + "-status.json.tmp").c_str(), "wb");
        if (meta) {
          std::fprintf(
              meta,
              "{\"schema\":1,\"reason\":\"%s\",\"write_ok\":%s,\"rows\":%zu,"
              "\"qpc_frequency\":%llu,\"start_qpc\":%llu,\"utc_filetime_"
              "100ns\":%"
              "llu,\"mode\":%u,\"seconds_limit\":%u,\"frame_capacity\":%u,"
              "\"call_level\":%u,\"clock_read_ns\":%.3f,\"gpu_pass_names\":[",
              why.c_str(), ok ? "true" : "false", rows->size(),
              (unsigned long long)hz, (unsigned long long)started,
              (unsigned long long)wall, mode, seconds, capacity, callLevel,
              clockNs);
          for (unsigned i = 0; i < GpuTimer::kPasses; ++i)
            std::fprintf(meta, "%s\"%s\"", i ? "," : "",
                         GpuTimer::Name(GpuTimer::Pass(i)));
          std::fputs(
              "],\"limitations\":\"CPU elapsed includes API waits. GPU "
              "intervals "
              "overlap CPU and nested GPU passes. Missing GPU values are "
              "unavailable/pending, not zero. End rows may lack delayed GPU "
              "results. Bypass retains module/hooks. No per-frame file writes; "
              "final writer runs asynchronously. Present callback scope "
              "excludes "
              "adapter device-lock acquisition.\"}\n",
              meta);
          if (std::fclose(meta) == 0)
            std::rename((prefix + "-status.json.tmp").c_str(),
                        (prefix + "-status.json").c_str());
        }
      } catch (...) {
        WATER_LOG_INFO("water profile writer failed: %s", prefix.c_str());
      }
      m_writing.store(false);
    });
  } catch (...) {
    m_writing.store(false);
    WATER_LOG_INFO("water profile write failed: worker allocation %s",
                   prefix.c_str());
  }
}
} // namespace water
