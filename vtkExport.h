#pragma once
#include <string>
#include <owl/common/math/vec.h>

void exportVTI(
    const std::string& filename,
    const uint32_t* grid,
    owl::vec3i dims,
    owl::vec3f origin,
    owl::vec3f cellSize,
    const std::string& fieldName
);