#pragma once
#include <vector>
#include <string>
#include <owl/common/math/vec.h>

struct LightSource
{
    owl::vec3f position;
    int resX, resY;
    bool isPunctual;
    float fovDeg;
    float coneAngle;
};

struct MeshInstance
{
    std::string objPath;
    bool isCausticsMesh;
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

struct SphereLightConfig
{
    bool enabled;
    int count;
    float marginFactor;
    float coneAngleMarginDeg;
};

struct ImplicitSurfaceConfig
{
    std::string type;
    float param0;
    float param1;
};

struct SceneConfig
{
    std::vector<LightSource> lights;
    std::vector<MeshInstance> meshes;
    GridConfig grid;
    SphereLightConfig sphereLights;
    ImplicitSurfaceConfig implicitSurface;
};

SceneConfig loadScene(const std::string& path);