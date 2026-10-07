#pragma once

#include <owl/common/math/vec.h>

#if defined(__CUDACC__)
#define IMPLICIT_SURFACE_HD __host__ __device__
#else
#define IMPLICIT_SURFACE_HD
#endif

enum ImplicitType
{
    IMPLICIT_TORUS,
    IMPLICIT_PARABOLA,
    IMPLICIT_GYROID,
    IMPLICIT_PERTUBED_PARABOLOID,
    IMPLICIT_CUSHION_SURFACE,
    IMPLICIT_TANGLECUBE,
    IMPLICIT_HYPERBOLIC_PARABOLOID
};

IMPLICIT_SURFACE_HD inline float evalImplicitSurface(
    ImplicitType type, owl::vec3f position, float p0, float p1)
{
    const float x = position.x;
    const float y = position.y;
    const float z = position.z;

    switch (type)
    {
    case IMPLICIT_TORUS:
    {
        const float radial = sqrtf(x * x + y * y) - p0;
        return radial * radial + z * z - p1 * p1;
    }
    case IMPLICIT_PARABOLA:
        return x * x + y * y - z;
    case IMPLICIT_GYROID:
        return sinf(x) * sinf(y) + sinf(y) * sinf(z) + sinf(z) * sinf(x) - p0;
    case IMPLICIT_PERTUBED_PARABOLOID:
        return x * x + y * y + p0 * sinf(p1 * x) - z;
    case IMPLICIT_CUSHION_SURFACE:
        return z * z * x * x - z * z * z * z - 2.0f * z * x * x + 2.0f * z * z * z
             + x * x - z * z - (x * x - z * z) * (x * x - z * z)
             - y * y * y * y - 2.0f * x * x * y * y - y * y * z * z
             + 2.0f * y * y * z + y * y;
    case IMPLICIT_TANGLECUBE:
        return x * x * x * x - 5.0f * x * x
             + y * y * y * y - 5.0f * y * y
             + z * z * z * z - 5.0f * z * z + 11.8f;
    case IMPLICIT_HYPERBOLIC_PARABOLOID:
        return x * x - y * y - z;
    default:
        return 1.0f;
    }
}

#undef IMPLICIT_SURFACE_HD
