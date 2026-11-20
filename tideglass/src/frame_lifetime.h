#pragma once
#include <atomic>
#include <cstdint>

namespace water
{
struct FrameStamp
{
    uint64_t session = 0, generation = 0, frame = 0;
};

// Numeric bookkeeping only; the host serialises calls on its device thread.
class FrameLifetime
{
public:
    FrameLifetime()
    {
        static std::atomic<uint64_t> identities{0};
        m_stamp.session = identities.fetch_add(1, std::memory_order_relaxed) + 1;
        m_stamp.generation = 1;
    }
    FrameLifetime(const FrameLifetime&) = delete;
    FrameLifetime& operator=(const FrameLifetime&) = delete;
    FrameStamp Begin()
    {
        if (!m_available) return {};
        ++m_stamp.frame;
        m_open = true;
        return m_stamp;
    }
    void End() { m_open = false; }
    void Lose() { End(); m_available = false; ++m_stamp.generation; }
    void Restore() { m_available = true; }
    FrameStamp Current() const { return m_open ? m_stamp : FrameStamp{}; }
    bool Accepts(FrameStamp stamp) const
    {
        return m_open && m_available && stamp.session == m_stamp.session &&
               stamp.generation == m_stamp.generation && stamp.frame == m_stamp.frame;
    }
private:
    FrameStamp m_stamp;
    bool m_open = false, m_available = true;
};
}
