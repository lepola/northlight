#pragma once
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <type_traits>

namespace water {
// Recenter a numeric ground grid in place. Native samples fill exposed cells;
// derived shore fields extend their edge until the next rebuild. Moving rows
// before columns preserves the original two-dimensional shift in both cases,
// without a temporary full grid or a clamp for every derived-field cell.
template<int Size, class T>
void ShiftGroundCells(T* cells, int dx, int dy, T outside, bool extend)
{
    static_assert(Size > 0 && std::is_trivially_copyable_v<T>);
    if (extend) {
        dx = std::clamp(dx, 1 - Size, Size - 1);
        dy = std::clamp(dy, 1 - Size, Size - 1);
    } else if (dx <= -Size || dx >= Size || dy <= -Size || dy >= Size) {
        std::fill(cells, cells + Size * Size, outside);
        return;
    }

    const int rows = Size - std::abs(dy);
    if (dy) {
        T* destination = cells + std::max(-dy, 0) * Size;
        const T* source = cells + std::max(dy, 0) * Size;
        std::memmove(destination, source, size_t(rows) * Size * sizeof(T));
        const int first = dy > 0 ? rows : 0;
        const int end = dy > 0 ? Size : -dy;
        const T* edge = cells + (dy > 0 ? rows - 1 : -dy) * Size;
        for (int y = first; y < end; ++y) {
            T* row = cells + y * Size;
            if (extend) std::memcpy(row, edge, Size * sizeof(T));
            else std::fill(row, row + Size, outside);
        }
    }
    if (dx) {
        const int columns = Size - std::abs(dx);
        for (int y = 0; y < Size; ++y) {
            T* row = cells + y * Size;
            const T edge = extend ? row[dx > 0 ? Size - 1 : 0] : outside;
            std::memmove(row + std::max(-dx, 0), row + std::max(dx, 0), columns * sizeof(T));
            if (dx > 0) std::fill(row + columns, row + Size, edge);
            else std::fill(row, row - dx, edge);
        }
    }
}
}
