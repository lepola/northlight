#pragma once
#include "config.h"
#include "frame_inputs.h"
#include "frame_lifetime.h"
#include "gpu_timer.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace water {
// Common bounded capture, independent of any host. CPU stages are exclusive
// elapsed intervals (including API waits); GPU pass intervals can overlap them
// and each other.
class FrameDiagnostics {
public:
  enum Stage {
    Selection,
    Upload,
    OwnedDraw,
    Snapshot,
    Composite,
    WorldDone,
    Ambient,
    Puddles,
    Rings,
    Slime,
    Lifecycle,
    ConfigReload,
    Count
  };
  struct Row {
    uint64_t hostFrame = 0, frame = 0, session = 0, generation = 0, world = 0,
             begin = 0, present = 0, submit = 0, returned = 0;
    uint64_t gpuMask = 0, capture = 0, captureTicks = 0;
    unsigned captureThread = 0, thread = 0, tick = 0;
    std::array<uint64_t, Count> cpu{};
    std::array<double, GpuTimer::kPasses> gpu{};
    double gpuWindow = 0, workerMilliseconds = 0;
    uint64_t activeBytes = 0, replacementBytes = 0, gpuBytes = 0,
             stagingBytes = 0, sourceBytes = 0, request = 0;
    unsigned uploads = 0, draws = 0, work = 0, clockReads = 0, preparation = 0;
    int producer = -1, gpuStatus = -1;
    unsigned source = 0, mode = 0;
    bool camera = false, water = false, presented = false;
    int map = -1;
    unsigned underwater = 0, slime = 0, movers = 0, boats = 0, fog = 0, splashes = 0;
    float daylight = 0, rain = 0, fogStart = 0, fogEnd = 0;
    // The reflected scene's draw this frame: what was drawn and what each fog skip left out.
    bool ownedDrawn = false;
    unsigned ownedCells = 0, ownedInstances = 0, ownedFogSkipped = 0, ownedPastFog = 0;
    uint64_t ownedTriangles = 0;
    float position[3]{}, direction[3]{};
    uint64_t threadCpu = 0, processCpu = 0, callTicks = 0, largestCallTicks = 0;
    unsigned cpuValid = 0, calls = 0;
    bool callTiming = false;
    char largestCall[96]{};
    uint64_t largestInnerTicks = 0;
    char largestInner[96]{};
    HRESULT result = S_OK;
    char reason[96]{};
  };
  class Scope {
  public:
    Scope(FrameDiagnostics &p, Stage stage)
        : owner(p), previous(p.Enter(stage)) {}
    ~Scope() { owner.Leave(previous); }

  private:
    FrameDiagnostics &owner;
    int previous;
  };
  class Call {
  public:
    Call(FrameDiagnostics &p, const char *name)
        : owner(p), label(name), token(p.BeginCall()) {}
    ~Call() { owner.EndCall(token, label); }

  private:
    FrameDiagnostics &owner;
    const char *label;
    uint64_t token;
  };
  uint64_t BeginCall();
  void EndCall(uint64_t, const char *);
  FrameDiagnostics();
  ~FrameDiagnostics();
  void Configure(const Config &, const std::string &directory, unsigned source);
  void Begin(FrameStamp, const FrameInputs * = nullptr);
  void Input(const FrameInputs &);
  void Capture(uint64_t end, uint64_t ticks, unsigned thread);
  void PresentBegin(uint64_t hostFrame = 0);
  void PresentReady();
  void PresentEnd(HRESULT);
  void GPU(uint64_t, bool, double, const double *, uint64_t mask = 0);
  void Finish(const char *reason);
  bool Active() const { return GetCurrentThreadId() == m_owner && m_active; }
  bool Cpu() const { return Active() && (m_mode & 1); }
  bool Gpu() const { return Active() && (m_mode & 2); }
  bool Bypass(const Config &c) const { return Active() && c.profileBypass; }
  Row *Current() { return Active() ? &m_row : nullptr; }
  static uint64_t Now();
  static bool CapturingInputs() {
    return s_active.load(std::memory_order_relaxed) != 0;
  }

private:
  int Enter(Stage);
  void Leave(int);
  void Charge(uint64_t);
  void Commit();
  void Write(const char *);
  static std::atomic<unsigned> s_active;
  std::vector<Row> m_rows;
  Row m_row{};
  std::thread m_writer;
  std::atomic<bool> m_writing{false};
  const DWORD m_owner = GetCurrentThreadId();
  uint64_t m_frequency = 0, m_started = 0, m_last = 0, m_identity = 0,
           m_sequence = 0, m_wall = 0;
  unsigned m_source = 0, m_mode = 0, m_limit = 0, m_seconds = 0;
  int m_seen = 0, m_stage = -1;
  bool m_active = false, m_calls = false, m_nestedCalls = false;
  unsigned m_callDepth = 0;
  uint64_t m_innerTicks = 0;
  char m_innerName[96]{};
  std::string m_prefix;
  double m_clockNs = 0;
};
} // namespace water
