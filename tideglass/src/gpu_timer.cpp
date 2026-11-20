#include "gpu_timer.h"

#include "log.h"

#include <cstdint>
#include <cstdio>

namespace
{
const char* const kPassNames[GpuTimer::kPasses] = {
    "ambient occlusion", "rain rings", "puddles", "liquid snapshot", "slime capture",
    "slime",             "lava",       "water",      "fog",     "sky memory, underwater view, FXAA",
    "ripples and contact foam", "surface detail (wakes, foam)", "reflection views and resolve",
    "underwater picture", "water composite",
    "sky memory", "underwater view",
    "owned scene",
    "lava stir and contact", "lava halo", "lava surface", "lava glow",
    "slime owned reflection", "slime level and ripples", "slime reflection rays", "slime shading",
};

template <typename T>
bool Result(IDirect3DQuery9* query, T& value)
{
    return query && query->GetData(&value, sizeof(value), 0) == S_OK;
}

void SafeRelease(IDirect3DQuery9*& query)
{
    if (query)
    {
        query->Release();
        query = nullptr;
    }
}
}

const char* GpuTimer::Name(Pass pass){return pass<kPasses?kPassNames[pass]:"unknown";}

bool GpuTimer::Ensure(IDirect3DQuery9*& query, D3DQUERYTYPE type)
{
    if (!query && FAILED(m_dev->CreateQuery(type, &query)))
    {
        query = nullptr;
        m_unsupported = true;
        WATER_LOG_INFO("GPU timestamp queries are unavailable; pass timings are off");
        Release();
        return false;
    }
    return true;
}

void GpuTimer::BeginFrame(IDirect3DDevice9* dev,uint64_t tag)
{
    if (m_unsupported || !dev)
        return;
    if (dev != m_dev)
    {
        Release();
        m_dev = dev;
    }
    if (m_inFrame)
        EndFrame();
    m_current = (m_current + 1) % static_cast<int>(m_frames.size());
    Frame& frame = m_frames[m_current];
    if (frame.issued)
        Collect(frame);
    frame.tag=tag;
    frame.spanCount = 0;
    for (int& open : m_open)
        open = -1;
    if (!Ensure(frame.disjoint, D3DQUERYTYPE_TIMESTAMPDISJOINT) || !Ensure(frame.frequency, D3DQUERYTYPE_TIMESTAMPFREQ) ||
        !Ensure(frame.start, D3DQUERYTYPE_TIMESTAMP) || !Ensure(frame.end, D3DQUERYTYPE_TIMESTAMP))
        return;
    frame.disjoint->Issue(D3DISSUE_BEGIN);
    frame.start->Issue(D3DISSUE_END);
    m_inFrame = true;
}

void GpuTimer::EndFrame()
{
    if (!m_inFrame)
        return;
    Frame& frame = m_frames[m_current];
    for (int pass = 0; pass < kPasses; ++pass)
        if (m_open[pass] >= 0)
            End(static_cast<Pass>(pass));
    m_inFrame = false;
    frame.end->Issue(D3DISSUE_END);
    frame.frequency->Issue(D3DISSUE_END);
    frame.disjoint->Issue(D3DISSUE_END);
    frame.issued = true;
}

bool GpuTimer::Begin(Pass pass)
{
    if (!m_inFrame || m_open[pass] >= 0) return false;
    Frame& frame = m_frames[m_current];
    // Draw sampling leaves room for the ordinary water/fog passes.
    if (frame.spanCount >= kMaxSpans) {
        ++m_spanOverflow;return false;
    }
    Span& span = frame.spans[frame.spanCount];
    if (!Ensure(span.start, D3DQUERYTYPE_TIMESTAMP) || !Ensure(span.end, D3DQUERYTYPE_TIMESTAMP))
        return false;
    span.pass = pass;
    span.start->Issue(D3DISSUE_END);
    m_open[pass] = frame.spanCount++;
    return true;
}

void GpuTimer::End(Pass pass)
{
    if (!m_inFrame || m_open[pass] < 0)
        return;
    m_frames[m_current].spans[m_open[pass]].end->Issue(D3DISSUE_END);
    m_open[pass] = -1;
}

// A frame issued kFramesInFlight frames ago; if the GPU has not finished it yet, it is left out rather than waited for.
void GpuTimer::Collect(Frame& frame)
{
    frame.issued = false;
    BOOL disjoint = TRUE;
    UINT64 frequency = 0;
    UINT64 start = 0;
    UINT64 end = 0;
    if (!Result(frame.disjoint, disjoint) || !Result(frame.frequency, frequency) || !Result(frame.start, start) ||
        !Result(frame.end, end) || disjoint || frequency == 0)
    {
        ++m_dropped;
        if(m_observer)m_observer(frame.tag,false,0,nullptr);
        if(m_detailedObserver)m_detailedObserver(frame.tag,false,0,nullptr,0);
        return;
    }
    double passMs[kPasses] = {};
    bool ran[kPasses] = {};
    for (int i = 0; i < frame.spanCount; ++i)
    {
        UINT64 from = 0;
        UINT64 to = 0;
        if (!Result(frame.spans[i].start, from) || !Result(frame.spans[i].end, to))
        {
            ++m_dropped;
            if(m_observer)m_observer(frame.tag,false,0,nullptr);
        if(m_detailedObserver)m_detailedObserver(frame.tag,false,0,nullptr,0);
            return;
        }
        passMs[frame.spans[i].pass] += static_cast<double>(to - from) * 1000.0 / frequency;
        ran[frame.spans[i].pass] = true;
    }
    const double elapsed=static_cast<double>(end-start)*1000.0/frequency;
    if(m_observer)m_observer(frame.tag,true,elapsed,passMs);
    uint64_t mask=0;for(unsigned i=0;i<kPasses;++i)if(ran[i])mask|=uint64_t(1)<<i;
    if(m_detailedObserver)m_detailedObserver(frame.tag,true,elapsed,passMs,mask);
    m_frameMs += elapsed;
    if(m_frameSamples.size()<4096) m_frameSamples.push_back(elapsed);
    ++m_frameCount;
    for (int pass = 0; pass < kPasses; ++pass)
        if (ran[pass])
        {
            m_passMs[pass] += passMs[pass];
            ++m_passFrames[pass];
        }
}

std::string GpuTimer::Report(bool detailed)
{
    // Offline caller has already waited on its complete-window query. Collect
    // every pending frame without a new wait. Live default remains unchanged.
    if (detailed) for (auto& frame : m_frames) if (frame.issued) Collect(frame);
    std::string report;
    char part[96];
    if (m_frameCount == 0)
        report = m_unsupported ? "unavailable" : "no frames measured";
    else
    {
        std::snprintf(part, sizeof(part), detailed ? "world pass %.6f ms over %u frames" : "world pass %.2f ms over %u frames", m_frameMs / m_frameCount, m_frameCount);
        report = part;
        double ours = 0.0;
        for (int pass = 0; pass < kPasses; ++pass)
            if (m_passFrames[pass])
            {
                // Per frame it ran in, and how often it ran.
                std::snprintf(part, sizeof(part), detailed ? "; %s%s %.6f ms (%u%%)" : "; %s%s %.2f ms (%u%%)", pass == kFirstPart ? "water's parts: " : "",
                              kPassNames[pass], m_passMs[pass] / m_passFrames[pass],
                              m_passFrames[pass] * 100 / m_frameCount);
                report += part;
                if (pass < kFirstPart)
                    ours += m_passMs[pass];
            }
        std::snprintf(part, sizeof(part), detailed ? "; renderer total %.6f ms" : "; renderer total %.2f ms", ours / m_frameCount);
        report += part;
    }
    if(!m_frameSamples.empty()) {
        std::sort(m_frameSamples.begin(),m_frameSamples.end());
        std::snprintf(part,sizeof(part),"; world median %.3f ms, p95 %.3f ms (first %u ready frames)",
            m_frameSamples[m_frameSamples.size()/2],m_frameSamples[(m_frameSamples.size()-1)*95/100],
            unsigned(m_frameSamples.size()));report+=part;
    }
    if(m_spanOverflow) {
        std::snprintf(part,sizeof(part),"; %u spans refused by query budget",m_spanOverflow);report+=part;
    }
    m_spanOverflow=0;m_frameSamples.clear();
    if (m_dropped)
    {
        std::snprintf(part, sizeof(part), "; %u frames not ready in time", m_dropped);
        report += part;
    }
    m_frameMs = 0.0;
    m_frameCount = 0;
    m_dropped = 0;
    for (int pass = 0; pass < kPasses; ++pass)
    {
        m_passMs[pass] = 0.0;
        m_passFrames[pass] = 0;
    }
    return report;
}

void GpuTimer::Release()
{
    for (Frame& frame : m_frames)
    {
        SafeRelease(frame.disjoint);
        SafeRelease(frame.frequency);
        SafeRelease(frame.start);
        SafeRelease(frame.end);
        for (Span& span : frame.spans)
        {
            SafeRelease(span.start);
            SafeRelease(span.end);
        }
        frame.spanCount = 0;
        frame.issued = false;
    }
    m_inFrame = false;
}
