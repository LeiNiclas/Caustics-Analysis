#pragma once
#include <vector>
#include <string>
#include <owl/common/math/vec.h>

struct TriangleMesh {
    std::vector<owl::vec3f> vertices;
    std::vector<owl::vec3i> indices;
};

TriangleMesh loadObj(const std::string& filename);
