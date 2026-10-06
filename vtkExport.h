#pragma once
#include <cstddef>
#include <string>
#include <owl/common/math/vec.h>

void exportVTI(
    const std::string& filename,
    const double* grid,
    owl::vec3i dims,
    owl::vec3f origin,
    owl::vec3f cellSize,
    const std::string& fieldName
);

void exportPointsVTP(
    const std::string& filename,
    const float* xyzPoints,
    std::size_t pointCount
);