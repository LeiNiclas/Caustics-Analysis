#pragma once
#include <vector>
#include <string>
#include <owl/common/math/vec.h>

struct TriangleMesh {
    std::vector<owl::vec3f> vertices;
    std::vector<owl::vec3i> indices;
};

TriangleMesh loadObj(const std::string& filename);

void applyTransform(TriangleMesh& mesh, const owl::vec3f& pos, const owl::vec3f& rotDeg, const owl::vec3f& scale);