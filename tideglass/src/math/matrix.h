#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

namespace water::math
{
inline bool Invert4x4(const float* input, float* output)
{
    double rows[4][8] = {};
    for (unsigned row = 0; row != 4; ++row)
        for (unsigned column = 0; column != 4; ++column)
        {
            const float value = input[row * 4 + column];
            if (!std::isfinite(value))
                return false;
            rows[row][column] = value;
            rows[row][column + 4] = row == column ? 1.0 : 0.0;
        }
    double determinant = 1;
    for (unsigned column = 0; column != 4; ++column)
    {
        unsigned pivot = column;
        for (unsigned row = column + 1; row != 4; ++row)
            if (std::fabs(rows[row][column]) > std::fabs(rows[pivot][column]))
                pivot = row;
        if (rows[pivot][column] == 0)
            return false;
        if (pivot != column)
        {
            for (unsigned i = 0; i != 8; ++i)
                std::swap(rows[column][i], rows[pivot][i]);
            determinant = -determinant;
        }
        const double divisor = rows[column][column];
        determinant *= divisor;
        for (double& value : rows[column])
            value /= divisor;
        for (unsigned row = 0; row != 4; ++row)
        {
            if (row == column)
                continue;
            const double factor = rows[row][column];
            for (unsigned i = 0; i != 8; ++i)
                rows[row][i] -= factor * rows[column][i];
        }
    }
    if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12)
        return false;
    float inverse[16];
    for (unsigned row = 0; row != 4; ++row)
        for (unsigned column = 0; column != 4; ++column)
        {
            const float value = static_cast<float>(rows[row][column + 4]);
            if (!std::isfinite(value))
                return false;
            inverse[row * 4 + column] = value;
        }
    std::memcpy(output, inverse, sizeof(inverse));
    return true;
}
}
