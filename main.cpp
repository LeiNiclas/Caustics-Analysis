#include <owl/owl.h>
#include <iostream>
#include <fstream>
#include "GeomTypes.h"

extern "C" char deviceCode_ptx[];

int main() {
    const int W = 400;
    const int H = 240;

    OWLContext context = owlContextCreate(nullptr, 0);
    OWLModule module = owlModuleCreate(context, deviceCode_ptx);

    // Host-pinned framebuffer so we can write it out directly
    OWLBuffer fb = owlHostPinnedBufferCreate(context, OWL_FLOAT3, W*H);

    // Raygen variables
    OWLVarDecl rayGenVars[] = {
        { "fbPtr", OWL_BUFPTR, OWL_OFFSETOF(RayGenData,fbPtr) },
        { "fbSize", OWL_INT2,  OWL_OFFSETOF(RayGenData,fbSize) },
        { "camera.origin",         OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.origin) },
        { "camera.lower_left_corner", OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.lower_left_corner) },
        { "camera.horizontal",     OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.horizontal) },
        { "camera.vertical",       OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.vertical) },
        { /* sentinel */ }
    };

    OWLRayGen rayGen = owlRayGenCreate(context, module, "rayGen", sizeof(RayGenData), rayGenVars, -1);

    // Framebuffer and size
    owlRayGenSetBuffer(rayGen, "fbPtr", fb);
    owl2i fbSize = { W, H };
    owlRayGenSet2i(rayGen, "fbSize", (const owl2i&)fbSize);

    // Simple pinhole camera
    const vec3f origin = vec3f(0.f,0.f,0.f);
    const float aspect = float(W)/float(H);
    const float vfov = 90.f;
    const float theta = vfov * 3.14159265358979323846f / 180.0f;
    const float half_height = tanf(theta/2.0f);
    const float half_width = aspect * half_height;
    const vec3f llc = origin - vec3f(half_width, half_height, 1.f);
    const vec3f horiz = vec3f(2.f*half_width, 0.f, 0.f);
    const vec3f vert = vec3f(0.f, 2.f*half_height, 0.f);

    owlRayGenSet3f(rayGen, "camera.origin", (const owl3f&)origin);
    owlRayGenSet3f(rayGen, "camera.lower_left_corner", (const owl3f&)llc);
    owlRayGenSet3f(rayGen, "camera.horizontal", (const owl3f&)horiz);
    owlRayGenSet3f(rayGen, "camera.vertical", (const owl3f&)vert);

    // Build and run
    owlBuildPrograms(context);
    owlBuildPipeline(context);
    owlBuildSBT(context);

    owlRayGenLaunch2D(rayGen, W, H);

    // Write out a simple PPM so no external lib required
    vec3f *pixels = (vec3f*)owlBufferGetPointer(fb,0);
    std::ofstream ofs("out.ppm", std::ios::binary);
    ofs << "P6\n" << W << " " << H << "\n255\n";
    for (int y = H-1; y >= 0; --y) {
        for (int x = 0; x < W; ++x) {
            vec3f c = pixels[y*W + x];
            unsigned char r = (unsigned char)(255.99f * fminf(fmaxf(c.x,0.f),1.f));
            unsigned char g = (unsigned char)(255.99f * fminf(fmaxf(c.y,0.f),1.f));
            unsigned char b = (unsigned char)(255.99f * fminf(fmaxf(c.z,0.f),1.f));
            ofs.put(r); ofs.put(g); ofs.put(b);
        }
    }
    ofs.close();

    std::cout << "Wrote out.ppm\n";

    owlContextDestroy(context);
    return 0;
}