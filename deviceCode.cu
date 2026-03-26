#include "deviceCode.h"
#include <optix_device.h>


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
	uint32_t* grid,
	const vec3f& gridOrigin,
	const vec3f& cellSize,
	const vec3i& dims
)
{
	vec3f posInGrid = (origin - gridOrigin) / cellSize;

    int cx = (int)floorf(posInGrid.x);
    int cy = (int)floorf(posInGrid.y);
    int cz = (int)floorf(posInGrid.z);

    int stepX = direction.x >= 0.f ? 1 : -1;
    int stepY = direction.y >= 0.f ? 1 : -1;
    int stepZ = direction.z >= 0.f ? 1 : -1;

    float tDeltaX = cellSize.x / fmaxf(fabsf(direction.x), 1e-8f);
    float tDeltaY = cellSize.y / fmaxf(fabsf(direction.y), 1e-8f);
    float tDeltaZ = cellSize.z / fmaxf(fabsf(direction.z), 1e-8f);

    float nextX = (stepX > 0)
        ? (ceilf(posInGrid.x)  - posInGrid.x) * cellSize.x / fmaxf(fabsf(direction.x), 1e-8f)
        : (posInGrid.x - floorf(posInGrid.x)) * cellSize.x / fmaxf(fabsf(direction.x), 1e-8f);

    float nextY = (stepY > 0)
        ? (ceilf(posInGrid.y)  - posInGrid.y) * cellSize.y / fmaxf(fabsf(direction.y), 1e-8f)
        : (posInGrid.y - floorf(posInGrid.y)) * cellSize.y / fmaxf(fabsf(direction.y), 1e-8f);

    float nextZ = (stepZ > 0)
        ? (ceilf(posInGrid.z)  - posInGrid.z) * cellSize.z / fmaxf(fabsf(direction.z), 1e-8f)
        : (posInGrid.z - floorf(posInGrid.z)) * cellSize.z / fmaxf(fabsf(direction.z), 1e-8f);

    // Edgecase: Ray starts on cell boundary
    if (nextX == 0.0f) nextX = tDeltaX;
    if (nextY == 0.0f) nextY = tDeltaY;
    if (nextZ == 0.0f) nextZ = tDeltaZ;

    float t = 0.f;

    while (t < tMax)
    {
        if (cx >= 0 && cx < dims.x &&
            cy >= 0 && cy < dims.y &&
            cz >= 0 && cz < dims.z)
        {
            int idx = cx + dims.x * cy + dims.x * dims.y * cz;
            atomicAdd(&grid[idx], 1u);
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
			cx += stepX;
        }
		else if (nextY < nextZ)
		{
            t = nextY;
			nextY += tDeltaY;
			cy += stepY;
        }
		else
		{
            t = nextZ;
			nextZ += tDeltaZ;
			cz += stepZ;
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
	ray.origin = self.camera.pos + screen.u * self.camera.dir_du + screen.v * self.camera.dir_dv;
	ray.direction = normalize(
		self.camera.dir_00
//	+	screen.u * self.camera.dir_du
//	+	screen.v * self.camera.dir_dv
	);
	
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
		prd.primaryGrid,
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