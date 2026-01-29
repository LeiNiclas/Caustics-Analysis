#pragma once
#include <owl/common/math/vec.h>
using namespace owl::common;

struct Sphere
{
    vec3f center;
    float radius;
};

struct RayGenData
{
    vec3f *fbPtr;
    vec2i fbSize;
    OptixTraversableHandle world;
    int sbtOffset;

    struct
    {
        vec3f origin;
        vec3f lower_left_corner;
        vec3f horizontal;
        vec3f vertical;
    } camera;
};

struct MissProgData {};
