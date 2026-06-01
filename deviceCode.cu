#include "deviceCode.h"
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
	const vec3f& origin,
	const vec3f& direction,
	float tMax,
	double* grid,
	const vec3f& gridOrigin,
	const vec3f& cellSize,
	const vec3i& dims
)
{
	vec3f posInGrid = (origin - gridOrigin) / cellSize;

    int cellX = (int)floorf(posInGrid.x);
    int cellY = (int)floorf(posInGrid.y);
    int cellZ = (int)floorf(posInGrid.z);
	
    int stepX = direction.x >= 0.f ? 1 : -1;
    int stepY = direction.y >= 0.f ? 1 : -1;
    int stepZ = direction.z >= 0.f ? 1 : -1;

    double tDeltaX = cellSize.x / fmaxf(fabsf(direction.x), 1e-8f);
    double tDeltaY = cellSize.y / fmaxf(fabsf(direction.y), 1e-8f);
    double tDeltaZ = cellSize.z / fmaxf(fabsf(direction.z), 1e-8f);

    double nextX = (stepX > 0)
        ? (ceilf(posInGrid.x)  - posInGrid.x) * tDeltaX
        : (posInGrid.x - floorf(posInGrid.x)) *	tDeltaX;

    double nextY = (stepY > 0)
        ? (ceilf(posInGrid.y)  - posInGrid.y) * tDeltaY
        : (posInGrid.y - floorf(posInGrid.y)) * tDeltaY;

    double nextZ = (stepZ > 0)
        ? (ceilf(posInGrid.z)  - posInGrid.z) * tDeltaZ
        : (posInGrid.z - floorf(posInGrid.z)) * tDeltaZ;

    // Edgecase: Ray starts on cell boundary
    if (nextX == 0.0f) nextX = tDeltaX;
    if (nextY == 0.0f) nextY = tDeltaY;
    if (nextZ == 0.0f) nextZ = tDeltaZ;

    double t = 0.0;
	double tPrevious = 0.0;

    while (t < tMax)
    {
        if (cellX >= 0 && cellX < dims.x &&
            cellY >= 0 && cellY < dims.y &&
            cellZ >= 0 && cellZ < dims.z)
        {
            int idx = cellX + dims.x * cellY + dims.x * dims.y * cellZ;
			double length;

			if (tPrevious == 0.0)
				length = t;
			else
				length = t - tPrevious;
			
			tPrevious = t;

            atomicAdd(&grid[idx], length); 
        }
        else if (t > 0.0f)
        {
            // Ray left the grid
            break;
        }

        if (nextX < nextY && nextX < nextZ)
		{
            t = nextX;
			nextX += tDeltaX;
			cellX += stepX;
        }
		else if (nextY < nextZ)
		{
            t = nextY;
			nextY += tDeltaY;
			cellY += stepY;
        }
		else
		{
            t = nextZ;
			nextZ += tDeltaZ;
			cellZ += stepZ;
        }
    }
}


OPTIX_RAYGEN_PROGRAM(rayGen)() // Name in parantheses must match name given in main
{
	// Read Program data set (RayGenData struct from deviceCode.h)
	const RayGenData& self = owl::getProgramData<RayGenData>();
	// Get pixel ID
	const vec2i pixelID = owl::getLaunchIndex();
	const vec2f screen = (vec2f(pixelID) + vec2f(0.5f)) / vec2f(self.fbSize);

	// Ray setup
	owl::Ray ray;

	if (self.isPunctual)
	{
		vec3f corner = self.camera.dir_00 - 0.5f * self.camera.dir_du - 0.5f * self.camera.dir_dv;
		ray.origin = self.camera.pos;
		ray.direction = normalize(corner + screen.u * self.camera.dir_du + screen.v * self.camera.dir_dv);
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

// Closest Hit Program for the triangle mesh
OPTIX_CLOSEST_HIT_PROGRAM(TriangleMesh)()
{
	PRD &prd = owl::getPRD<PRD>();

	//self = face 
	const TrianglesGeomData &self = owl::getProgramData<TrianglesGeomData>();

	// Compute normal
	const int primitiveID = optixGetPrimitiveIndex();
	const vec3i index = self.index[primitiveID];
	const vec3f &A = self.vertex[index.x];
	const vec3f &B = self.vertex[index.y];
	const vec3f &C = self.vertex[index.z];
	vec3f Ng = normalize(cross(B-A, C-A));

	const vec3f rayDir = optixGetWorldRayDirection();
	const vec3f rayOrigin = optixGetWorldRayOrigin();
	const float tHit = optixGetRayTmax();
	
	if (dot(Ng, rayDir) > 0.0f)
		Ng = -Ng;
	
	vec3f hitPoint = rayOrigin + tHit * rayDir;
	vec3f directColor = (0.2f + 0.8f * fabs(dot(rayDir, Ng))) * self.color;

	traverseGrid(
		rayOrigin, rayDir, tHit,
		prd.depth == 0 ? prd.primaryGrid : prd.bounceGrid,
		prd.gridOrigin, prd.gridCellSize, prd.gridDims
	);

	// increment hit counter stored in the triangle's SBT data
	atomicAdd(self.counter, 1u);

	if (prd.depth < 1)
	{
		// ---- BOUNCE ----
        // Reflection: r = d - 2*(d·n)*n
        const vec3f reflected = rayDir - 2.f * dot(rayDir, Ng) * Ng;

        // Setup seconday ray
        // small offset along normal
        owl::Ray secRay;
        secRay.origin    = hitPoint + 1e-3f * Ng;
        secRay.direction = normalize(reflected);

        PRD secPRD;
        secPRD.depth = prd.depth + 1;  // = 1, no more bounces
        secPRD.color = vec3f(0.f);
		secPRD.primaryGrid = prd.primaryGrid;
		secPRD.bounceGrid = prd.bounceGrid;
		secPRD.gridOrigin = prd.gridOrigin;
		secPRD.gridCellSize = prd.gridCellSize;
		secPRD.gridDims = prd.gridDims;

        // Trace secondary ray (world from GeomData)
        owl::traceRay(self.world, secRay, secPRD);

        prd.color = 0.5f * directColor + 0.5f * secPRD.color;
    }
	else
	{
        // Max bounces reached
        prd.color = directColor;
	}
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


__device__ double torus(vec3d position, double R, double r)
{
	double term = (sqrt(position.x * position.x + position.y * position.y) - R);
	term *= term;
	term += position.z * position.z;
	term -= r * r;

	return term;
}


__device__ vec3d getPositionAlongRay(vec3d origin, vec3d dir, double t)
{
	return origin + t * dir;
}



OPTIX_INTERSECT_PROGRAM(ImplicitTorus)()
{
	const TorusGeomData& self = owl::getProgramData<TorusGeomData>();

	vec3d rayOrigin = optixGetObjectRayOrigin();
	vec3d rayDirection = optixGetObjectRayDirection();
	
	double minorRadius = self.minorRadius;
	double majorRadius = self.majorRadius;

	const double eps = 1e-9;
	const double tMax = 10;
	double t1 = 0.5;
	double t2;
	double tIncrementStep = 0.25;
	
	double val1 = torus(getPositionAlongRay(rayOrigin, rayDirection, t1), majorRadius, minorRadius);
	double val2;

	int maxSteps = 30;
	int maxBisectionSteps = 10;

	bool signChangeIntervalFound = false;
	
	// March to find interval with sign change
	for (int step = 0; step < maxSteps; step++)
	{
		t2 = t1 + tIncrementStep;

		if (t2 > tMax)
			t2 = tMax;
		
		val2 = torus(getPositionAlongRay(rayOrigin, rayDirection, t2), majorRadius, minorRadius);

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
	double tMid;

	for (int i = 0; i < maxBisectionSteps; i++)
	{
		tMid = (t1 + t2) * 0.5;
		double valMid = torus(getPositionAlongRay(rayOrigin, rayDirection, tMid), majorRadius, minorRadius);

		if (abs(valMid) < eps)
			break;
		
		if (signbit(valMid) != signbit(val1))
		{
			val2 = tMid;
		}
		else
		{
			val1 = tMid;
		}
	}

	double tHit = tMid;
	optixReportIntersection(tHit, 0);
}
