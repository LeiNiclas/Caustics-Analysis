#include <owl/owl_device.h>
#include "GeomTypes.h"

using namespace owl;

// Minimal raygen that performs a local ray-sphere intersection
// (no OptiX tracing) and writes a color into the framebuffer.
OPTIX_RAYGEN_PROGRAM(rayGen)()
{
	const RayGenData &self = owl::getProgramData<RayGenData>();
	const vec2i pix = owl::getLaunchIndex();
	const vec2i dims = owl::getLaunchDims();
	const int idx = pix.x + pix.y * dims.x;

	const float u = (pix.x + 0.5f) / float(dims.x);
	const float v = (pix.y + 0.5f) / float(dims.y);

	const vec3f origin = self.camera.origin;
	const vec3f dir = normalize(self.camera.lower_left_corner + u*self.camera.horizontal + v*self.camera.vertical - self.camera.origin);

	// simple analytic sphere at (0,0,-1)
	const vec3f center = vec3f(0.f,0.f,-1.f);
	const float radius = 0.5f;
	const vec3f oc = origin - center;
	const float a = dot(dir,dir);
	const float b = dot(oc,dir);
	const float c = dot(oc,oc) - radius*radius;
	const float disc = b*b - a*c;

	vec3f col;
	if (disc < 0.f) {
		float t = 0.5f*(dir.y + 1.0f);
		col = (1.0f - t) * vec3f(1.f,1.f,1.f) + t * vec3f(0.5f,0.7f,1.f);
	} else {
		const float t = (-b - sqrtf(disc)) / a;
		const vec3f P = origin + t*dir;
		const vec3f N = normalize(P - center);
		col = 0.5f*(N + vec3f(1.f));
	}

	self.fbPtr[idx] = col;
}
