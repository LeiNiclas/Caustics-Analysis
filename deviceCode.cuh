#include "deviceCode.h"
#include <optix_device.h>


__device__ inline float torus(vec3f position, float R, float r)
{
	float term = (owl::sqrt(position.x * position.x + position.y * position.y) - R);
	term *= term;
	term += position.z * position.z;
	term -= r * r;

	return term;
}


__device__ inline vec3f torusNormal(vec3f position, float R){
    vec3f normal;
    float sqroot = owl::sqrt(position.x * position.x + position.y * position.y);
    float factor = 2.0f * (1.0f - R / sqroot);
    normal.x = position.x * factor;
    normal.y = position.y * factor;
    normal.z = position.z * 2.0f;
    return normalize(normal);
}



__device__ inline float parabola(vec3f position)
{
	float term	= position.x * position.x
				+ position.y * position.y
				- position.z;
	
	return term;
}


__device__ inline vec3f parabolaNormal(vec3f position)
{
	vec3f normal = vec3f(2.0f * position.x, 2.0f * position.y, -1);
	return normalize(normal);
}


__device__ inline float gyroid(vec3f position, float a)
{
    float x = position.x;
    float y = position.y;
    float z = position.z;

    return sinf(x)*sinf(y) + sinf(y)*sinf(z) + sinf(z)*sinf(x) - a;
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

    return vec3f(cx*cy-sx*sz, cy*cz-sx*sy, cx*cz-sy*sz);
}


__device__ inline float evalImplicit(ImplicitType type, vec3f position, float p0, float p1)
{
    switch (type)
    {
        case IMPLICIT_TORUS: return torus(position, p0, p1);
        case IMPLICIT_PARABOLA: return parabola(position);
        case IMPLICIT_GYROID: return gyroid(position, p0);
        default: return 1.0f;
    }
}


__device__ inline vec3f evalImplicitNormal(ImplicitType type, vec3f position, float p0, float p1)
{
    switch (type)
    {
        case IMPLICIT_TORUS: return torusNormal(position, p0);
        case IMPLICIT_PARABOLA: return parabolaNormal(position);
        case IMPLICIT_GYROID: return gyroidNormal(position);
        default: return vec3f(1.0f);
    }
}