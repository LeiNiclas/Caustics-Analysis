#include "deviceCode.h"
#include <optix_device.h>


__device__ inline vec3f torusNormal(vec3f position, float R){
    vec3f normal;
    float sqroot = owl::sqrt(position.x * position.x + position.y * position.y);
    float factor = 2.0f * (1.0f - R / sqroot);
    normal.x = position.x * factor;
    normal.y = position.y * factor;
    normal.z = position.z * 2.0f;
    return normalize(normal);
}



__device__ inline vec3f parabolaNormal(vec3f position)
{
	vec3f normal = vec3f(2.0f * position.x, 2.0f * position.y, -1);
	return normalize(normal);
}


__device__ inline vec3f gyroidNormal(vec3f position)
{
    float x = position.x;
    float y = position.y;
    float z = position.z;

    float sx = sinf(x);
    float cx = cosf(x);
    float sy = sinf(y);
    float cy = cosf(y);
    float sz = sinf(z);
    float cz = cosf(z);

    return normalize(vec3f(cx*cy-sx*sz, cy*cz-sx*sy, cx*cz-sy*sz));
}


__device__ inline vec3f pertubedParaboloidNormal(vec3f position, float amplitude, float omega)
{
    vec3f normal;
    normal.x = 2.0f * position.x + amplitude * omega * cosf(omega * position.x);
    normal.y = 2.0f * position.y;
    normal.z = -1.0f;

    return normalize(normal);
}


__device__ inline vec3f cushionSurfaceNormal(vec3f position)
{
    float x = position.x;
    float y = position.y;
    float z = position.z;

    vec3f normal;

    normal.x = -4*x*x*x - 4*x*y*y + 2*x*z*z + 2*x;
    normal.y = -4*x*x*y - 4*y*y*y - 2*y*z*z + 4*y*z + 2*y;
    normal.z = 2*x*x*z - 2*y*y*z + 2*y*y - 4*z*z*z + 6*z*z - 4*z;

    return normalize(normal);
}


__device__ inline vec3f tanglecubeNormal(vec3f position)
{
    float x = position.x;
    float y = position.y;
    float z = position.z;

    return normalize(vec3f(2*x*(2*x*x - 5), 2*y*(2*y*y - 5), 2*z*(2*z*z - 5)));
}


__device__ inline vec3f hyperbolicParaboloidNormal(vec3f position)
{
    return normalize(vec3f(2*position.x, -2*position.y, -1));
}


__device__ inline float evalImplicit(ImplicitType type, vec3f position, float p0, float p1)
{
    return evalImplicitSurface(type, position, p0, p1);
}


__device__ inline vec3f evalImplicitNormal(ImplicitType type, vec3f position, float p0, float p1)
{
    switch (type)
    {
        case IMPLICIT_TORUS: return torusNormal(position, p0);
        case IMPLICIT_PARABOLA: return parabolaNormal(position);
        case IMPLICIT_GYROID: return gyroidNormal(position);
        case IMPLICIT_PERTUBED_PARABOLOID: return pertubedParaboloidNormal(position, p0, p1);
        case IMPLICIT_CUSHION_SURFACE: return cushionSurfaceNormal(position);
        case IMPLICIT_TANGLECUBE: return tanglecubeNormal(position);
        case IMPLICIT_HYPERBOLIC_PARABOLOID: return hyperbolicParaboloidNormal(position);
        default: return vec3f(1.0f);
    }
}