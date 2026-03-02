#include "deviceCode.h"
#include <optix_device.h>


OPTIX_RAYGEN_PROGRAM(simpleRayGen)() // Name in parantheses must match name given in main
{
	// Read Program data set (RayGenData struct from deviceCode.h)
	const RayGenData& self = owl::getProgramData<RayGenData>();
	// Get pixel ID
	const vec2i pixelID = owl::getLaunchIndex();
	const vec2f screen = (vec2f(pixelID) + vec2f(0.5f)) / vec2f(self.fbSize);

	// Ray setup
	owl::Ray ray;
	ray.origin = self.camera.pos;
	ray.direction = normalize(
		self.camera.dir_00
	+	screen.u * self.camera.dir_du
	+	screen.v * self.camera.dir_dv
	);
	
	vec3f color;

	owl::traceRay(
		self.world,		// Traceable acceleration structure
		ray,			// Ray
		color			// PRD
	);

	// Write result to file buffer
	const int fbOfs = pixelID.x + self.fbSize.x * pixelID.y;
	self.fbPtr[fbOfs] = owl::make_rgba(color);
}

// Closest Hit Program for the triangle mesh
OPTIX_CLOSEST_HIT_PROGRAM(TriangleMesh)()
{
	vec3f &prd = owl::getPRD<vec3f>();

	const TrianglesGeomData &self = owl::getProgramData<TrianglesGeomData>();

	// Compute normal
	const int primitiveID = optixGetPrimitiveIndex();
	const vec3i index = self.index[primitiveID];
	const vec3f &A = self.vertex[index.x];
	const vec3f &B = self.vertex[index.y];
	const vec3f &C = self.vertex[index.z];
	const vec3f Ng = normalize(cross(B-A, C-A));

	const vec3f rayDir = optixGetWorldRayDirection();
	prd = (0.2f + 0.8f * fabs(dot(rayDir, Ng))) * self.color;
	
	// increment hit counter stored in the triangle's SBT data
	atomicAdd(self.counter, 1);
}


OPTIX_MISS_PROGRAM(miss)()
{
	const vec2i pixelID = owl::getLaunchIndex();

	const MissProgData &self = owl::getProgramData<MissProgData>();

	vec3f &prd = owl::getPRD<vec3f>();
	int pattern = (pixelID.x / 8) ^ (pixelID.y / 8);
	prd = (pattern & 1) ? self.color1 : self.color0;
}