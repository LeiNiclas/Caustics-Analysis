#pragma once
#include <vector>
#include <string>
#include <owl/common/math/vec.h>

struct LightSource
{
    owl::vec3f position;
    int resX, resY;
};

struct MeshInstance
{
    std::string objPath;
    owl::vec3f position;
    owl::vec3f rotation;
    owl::vec3f scale;
};

struct GridConfig
{
    owl::vec3f origin;
    owl::vec3f size;
    owl::vec3i cellCount;
};

struct SceneConfig
{
    std::vector<LightSource> ligths;
    std::vector<MeshInstance> meshes;
    GridConfig grid;
};

SceneConfig loadScene(const std::string& path);