#pragma once

#include <cmath>

// Holds the game's own fog off while Tideglass draws: remembers the two native fog ranges, pushes them out to the
// given start, and puts the saved values back afterwards. Reading and writing them is the caller's.
namespace water
{
struct NativeFogRanges
{
    float start[2] = {};
    float end[2] = {};
};

class NativeFogOverride
{
public:
    bool Active() const { return m_active; }
    const NativeFogRanges& Saved() const { return m_saved; }

    template<class Read, class Write>
    bool Suppress(Read&& read, Write&& write, float start)
    {
        if (m_active)
            return true;
        if (!std::isfinite(start) || start <= 0 || !std::isfinite(start * 2))
            return false;
        NativeFogRanges original;
        if (!read(original))
            return false;
        for (unsigned i = 0; i != 2; ++i)
            if (!std::isfinite(original.start[i]) || !std::isfinite(original.end[i]))
                return false;
        m_saved = original;
        m_active = true;
        const NativeFogRanges suppressed = {{start, start}, {start * 2, start * 2}};
        write(suppressed);
        return true;
    }

    template<class Write>
    void Restore(Write&& write)
    {
        if (m_active)
        {
            write(m_saved);
            m_active = false;
        }
    }

private:
    NativeFogRanges m_saved;
    bool m_active = false;
};
}
