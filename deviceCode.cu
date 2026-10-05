#include "deviceCode.h"
#include "deviceCode.cuh"
#include <optix_device.h>

// Taken from https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html?highlight=float2#atomic-functions
#if __CUDA_ARCH__ < 600
__device__ double atomicAdd(double* address, double val)
{
    unsigned long long int* address_as_ull =
                              (unsigned long long int*)address;
    unsigned long long int old = *address_as_ull, assumed;

    do {
        assumed = old;
        old = atomicCAS(address_as_ull, assumed,
                        __double_as_longlong(val +
                               __longlong_as_double(assumed)));

    // Note: uses integer comparison to avoid hang in case of NaN (since NaN != NaN)
    } while (assumed != old);

    return __longlong_as_double(old);
}
#endif


// DDA Grid Traversal
// origin:		start of ray in world coordinates
// direction:	normalized direction
// tMax:		intersection point of ray & mesh OR grid end
// grid:		target buffer (primaryGrid OR bounceGrid)
// gridOrigin, cellSize, dims: grid params
__device__ void traverseGrid(
    const vec3f& origin, const vec3f& direction, float tMax,
    double* grid, const vec3f& gridOrigin, const vec3f& cellSize, const vec3i& dims)
{
    const vec3f gridMin = gridOrigin;
    const vec3f gridMax = gridOrigin + vec3f((float)dims.x, (float)dims.y, (float)dims.z) * cellSize;

    // Slab-Test: Entry point where the ray initially hits the grid
    float tEnter = 0.0f, tExit = tMax;

    if (fabsf(direction.x) < 1e-8f) { if (origin.x < gridMin.x || origin.x > gridMax.x) return; }
    else {
        float t0 = (gridMin.x - origin.x) / direction.x;
        float t1 = (gridMax.x - origin.x) / direction.x;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        tEnter = fmaxf(tEnter, t0); tExit = fminf(tExit, t1);
    }
    if (fabsf(direction.y) < 1e-8f) { if (origin.y < gridMin.y || origin.y > gridMax.y) return; }
    else {
        float t0 = (gridMin.y - origin.y) / direction.y;
        float t1 = (gridMax.y - origin.y) / direction.y;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        tEnter = fmaxf(tEnter, t0); tExit = fminf(tExit, t1);
    }
    if (fabsf(direction.z) < 1e-8f) { if (origin.z < gridMin.z || origin.z > gridMax.z) return; }
    else {
        float t0 = (gridMin.z - origin.z) / direction.z;
        float t1 = (gridMax.z - origin.z) / direction.z;
        if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
        tEnter = fmaxf(tEnter, t0); tExit = fminf(tExit, t1);
    }

    if (tEnter >= tExit || tExit <= 0.0f) return;   // Ray misses the grid
    tEnter = fmaxf(tEnter, 0.0f);                    // if origin already lies inside of the grid

    // start DDA at the actual entrypoint
    vec3f entryPoint = origin + tEnter * direction;
    vec3f posInGrid = (entryPoint - gridOrigin) / cellSize;

    int cellX = min(max((int)floorf(posInGrid.x), 0), dims.x - 1);
    int cellY = min(max((int)floorf(posInGrid.y), 0), dims.y - 1);
    int cellZ = min(max((int)floorf(posInGrid.z), 0), dims.z - 1);

    int stepX = direction.x >= 0.f ? 1 : -1;
    int stepY = direction.y >= 0.f ? 1 : -1;
    int stepZ = direction.z >= 0.f ? 1 : -1;

    double tDeltaX = cellSize.x / fmaxf(fabsf(direction.x), 1e-8f);
    double tDeltaY = cellSize.y / fmaxf(fabsf(direction.y), 1e-8f);
    double tDeltaZ = cellSize.z / fmaxf(fabsf(direction.z), 1e-8f);

    double nextX = (gridOrigin.x + (cellX + (stepX > 0 ? 1 : 0)) * cellSize.x - origin.x) / direction.x;
    double nextY = (gridOrigin.y + (cellY + (stepY > 0 ? 1 : 0)) * cellSize.y - origin.y) / direction.y;
    double nextZ = (gridOrigin.z + (cellZ + (stepZ > 0 ? 1 : 0)) * cellSize.z - origin.z) / direction.z;

    double t = tEnter;
    const double tStop = fmin((double)tExit, (double)tMax);

    while (t < tStop)
    {
        double tNext = fmin(fmin(nextX, nextY), nextZ);
        tNext = fmin(tNext, tStop);

        int idx = cellX + dims.x * cellY + dims.x * dims.y * cellZ;
        atomicAdd(&grid[idx], tNext - t);   // Length of the current segment

        t = tNext;
        if (nextX <= nextY && nextX <= nextZ) { nextX += tDeltaX; cellX += stepX; }
        else if (nextY <= nextZ)              { nextY += tDeltaY; cellY += stepY; }
        else                                   { nextZ += tDeltaZ; cellZ += stepZ; }

        if (cellX < 0 || cellX >= dims.x || cellY < 0 || cellY >= dims.y || cellZ < 0 || cellZ >= dims.z)
            break;
    }
}

__device__ inline float radicalInverse(uint32_t bits, uint32_t base)
{
	float f = 1.0f;
	float r = 0.0f;

	while (bits > 0)
	{
		f /= (float)base;
		r += f * (bits % base);
		bits /= base;
	}

	return r;
}


__device__ inline void buildONB(const vec3f& n, vec3f& b1, vec3f& b2)
{
	float sign = n.z >= 0.0f ? 1.0f : -1.0f;
	float a = -1.0f / (sign + n.z);
	float b = n.x * n.y * a;
	b1 = vec3f(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
	b2 = vec3f(b, sign + n.y * n.y * a, -n.y);
}


OPTIX_RAYGEN_PROGRAM(rayGen)() // Name in parantheses must match name given in main
{
	// Read Program data set (RayGenData struct from deviceCode.h)
	const RayGenData& self = owl::getProgramData<RayGenData>();
	// Get pixel ID
	const vec2i pixelID = owl::getLaunchIndex();
	const vec2f screen = (vec2f(pixelID) + vec2f(0.5f, 0.5f)) / vec2f(self.fbSize);

	// Ray setup
	owl::Ray ray;

	if (self.isPunctual)
	{
		const uint32_t pixelIndex = pixelID.x + self.fbSize.x * pixelID.y;

		float u1 = radicalInverse(pixelIndex + 1u, 2u);
		float u2 = radicalInverse(pixelIndex + 1u, 3u);

		vec3f fwd = normalize(self.camera.dir_00);
		vec3f right, up;
		buildONB(fwd, right, up);

		float cosMax = cosf(self.camera.coneAngle);
		float cosTheta = 1.0f - u1 * (1.0f - cosMax);
		float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - cosTheta * cosTheta));
		float phi = 2.0f * (float)M_PI * u2;

		vec3f localDir(sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
		vec3f worldDir = localDir.x * right + localDir.y * up + localDir.z * fwd;

		ray.origin = self.camera.pos;
		ray.direction = normalize(worldDir);
	}
	else
	{
		ray.origin = self.camera.pos + screen.u * self.camera.dir_du + screen.v * self.camera.dir_dv;
		ray.direction = normalize(self.camera.dir_00);
	}

	
	PRD prd;
	prd.depth = 0;
	prd.color = vec3f(0.0f);
	prd.primaryGrid = self.primaryGrid;
	prd.bounceGrid = self.bounceGrid;
	prd.gridOrigin = self.gridOrigin;
	prd.gridCellSize = self.gridCellSize;
	prd.gridDims = self.gridDims;

	owl::traceRay(
		self.world,		// Traceable acceleration structure
		ray,			// Ray
		prd				// PRD
	);

	// Write result to file buffer
	const int fbOfs = pixelID.x + self.fbSize.x * pixelID.y;
	self.fbPtr[fbOfs] = owl::make_rgba(prd.color);
}


OPTIX_MISS_PROGRAM(miss)()
{
	const vec2i pixelID = owl::getLaunchIndex();
	const MissProgData &self = owl::getProgramData<MissProgData>();

	PRD &prd = owl::getPRD<PRD>();

	// Traverse bounce grid if the given ray is a bounce-ray
	// -> tMax for miss = max ray length
	// -> Only traverse to the edge of the grid
	const vec3f rayDir = normalize((vec3f)optixGetWorldRayDirection());
	const vec3f rayOrigin = (vec3f)optixGetWorldRayOrigin();
	float tMax = length((vec3f)prd.gridDims * prd.gridCellSize) * 2.0f;

	if (prd.depth == 0)
	{
		// Primary ray didn't hit anything
		// -> traverse primary grid
		traverseGrid(
			rayOrigin, rayDir, tMax,
			prd.primaryGrid,
			prd.gridOrigin, prd.gridCellSize, prd.gridDims
		);
	}
	else
	{
		// Bounce ray didn't hit anything
		// -> traverse bounce grid
		traverseGrid(
			rayOrigin, rayDir, tMax,
			prd.bounceGrid,
			prd.gridOrigin, prd.gridCellSize, prd.gridDims
		);
	}

	int pattern = (pixelID.x / 8) ^ (pixelID.y / 8);
	prd.color = (pattern & 1) ? self.color1 : self.color0;

}


__device__ vec3f getPositionAlongRay(vec3f origin, vec3f dir, float t)
{
	return origin + t * dir;
}


OPTIX_INTERSECT_PROGRAM(Implicit)()
{
	const ImplicitGeomData& self = owl::getProgramData<ImplicitGeomData>();

	vec3f rayOrigin = optixGetObjectRayOrigin();
	vec3f rayDirection = optixGetObjectRayDirection();
	
	const float eps = 1e-6;
	const float tMax = 50;
	float t1 = 0.0;
	float t2;
	float tIncrementStep = 0.25;
	
	float val1 = evalImplicit(self.type, getPositionAlongRay(rayOrigin, rayDirection, t1), self.param0, self.param1); //torus(getPositionAlongRay(rayOrigin, rayDirection, t1), majorRadius, minorRadius);
	float val2;

	int maxSteps = 100;
	int maxBisectionSteps = 50;

	bool signChangeIntervalFound = false;
	
	// March to find interval with sign change
	for (int step = 0; step < maxSteps; step++)
	{
		t2 = t1 + tIncrementStep;

		if (t2 > tMax)
			t2 = tMax;
		
		val2 = evalImplicit(self.type, getPositionAlongRay(rayOrigin, rayDirection, t2), self.param0, self.param1); //torus(getPositionAlongRay(rayOrigin, rayDirection, t2), majorRadius, minorRadius);

		if (signbit(val1) != signbit(val2))
		{
			signChangeIntervalFound = true;
			break;
		}
		
		t1 = t2;
		val1 = val2;
	}

	// No sign change found
	if (!signChangeIntervalFound)
		return;
	
	// Bisection
	float tMid;

	for (int i = 0; i < maxBisectionSteps; i++)
	{
		tMid = (t1 + t2) * 0.5;
		float valMid = parabola(getPositionAlongRay(rayOrigin, rayDirection, tMid)); //torus(getPositionAlongRay(rayOrigin, rayDirection, tMid), majorRadius, minorRadius);

		if (abs(valMid) < eps)
			break;
		
		if (signbit(valMid) != signbit(val1))
		{
			t2 = tMid;
			val2 = valMid;
		}
		else
		{
			t1 = tMid;
			val1 = valMid;
		}
	}

	float tHit = tMid;
	optixReportIntersection(tHit, 0);
}


OPTIX_CLOSEST_HIT_PROGRAM(Implicit)()
{
    PRD& prd = owl::getPRD<PRD>();

    const ImplicitGeomData& self = owl::getProgramData<ImplicitGeomData>();

    const vec3f rayOrigin = optixGetWorldRayOrigin();
    const vec3f rayDir    = optixGetWorldRayDirection();
    const float tHit      = optixGetRayTmax();
    vec3f hitPoint = rayOrigin + tHit * rayDir;

    vec3f normal = evalImplicitNormal(self.type, hitPoint, self.param0, self.param1);
    if (dot(normal, rayDir) > 0.f) normal = -normal;

    vec3f directColor = (0.2f + 0.8f * fabsf(dot(rayDir, normal))) * vec3f(0.2f, 0.6f, 1.0f);

    traverseGrid(
        rayOrigin, rayDir, tHit,
        prd.depth == 0 ? prd.primaryGrid : prd.bounceGrid,
        prd.gridOrigin, prd.gridCellSize, prd.gridDims
    );

    if (prd.depth < 1)
    {
        vec3f reflected = rayDir - 2.f * dot(rayDir, normal) * normal;

        owl::Ray secRay;
        secRay.origin    = hitPoint + 1e-3f * normal;
        secRay.direction = normalize(reflected);

        PRD secPRD;
        secPRD.depth        = prd.depth + 1;
        secPRD.color        = vec3f(0.f);
        secPRD.primaryGrid  = prd.primaryGrid;
        secPRD.bounceGrid   = prd.bounceGrid;
        secPRD.gridOrigin   = prd.gridOrigin;
        secPRD.gridCellSize = prd.gridCellSize;
        secPRD.gridDims     = prd.gridDims;

        owl::traceRay(self.world, secRay, secPRD);
        prd.color = 0.5f * directColor + 0.5f * secPRD.color;
    }
    else
    {
        prd.color = directColor;
    }
}


OPTIX_BOUNDS_PROGRAM(Implicit)(const void* geomData, box3f& bounds, int primID)
{
	bounds.lower = vec3f(-10.0f, -10.0f, -10.0f);
	bounds.upper = vec3f(10.0f, 10.0f, 10.0f);
    //const TorusGeomData& self = *(const TorusGeomData*)geomData;
    //float outer = self.majorRadius + self.minorRadius;
    //float tube  = self.minorRadius;
	//
    //bounds.lower = vec3f(-outer, -outer, -tube);
    //bounds.upper = vec3f( outer,  outer,  tube);
}
