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
	ray.origin = self.camera.pos + screen.u * self.camera.dir_du + screen.v * self.camera.dir_dv;
	ray.direction = normalize(
		self.camera.dir_00
//	+	screen.u * self.camera.dir_du
//	+	screen.v * self.camera.dir_dv
	);
	
	PRD prd;
	prd.depth = 0;
	prd.color = vec3f(0.0f);

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
	

	if (dot(Ng, rayDir) > 0.0f)
		Ng = -Ng;
	
	vec3f directColor = (0.2f + 0.8f * fabs(dot(rayDir, Ng))) * self.color;

	// increment hit counter stored in the triangle's SBT data
	atomicAdd(self.counter, 1);

	if (prd.depth < 1)
	{
		// ---- BOUNCE ----
        // Calculate hit
        const vec3f hitPoint = (vec3f)optixGetWorldRayOrigin()
                             + optixGetRayTmax() * rayDir;

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

        // Sekundären Ray tracen (world aus GeomData)
        owl::traceRay(self.world, secRay, secPRD);

        // Direkte Farbe + Reflection mischen (50/50)
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
	int pattern = (pixelID.x / 8) ^ (pixelID.y / 8);
	prd.color = (pattern & 1) ? self.color1 : self.color0;
}