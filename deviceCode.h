#pragma once

#include <owl/owl.h>
#include <owl/common/math/vec.h>

using namespace owl;


// Structs here are used in OWLVarDecl
// variables are set during the shader binding table setup

struct PRD
{
    vec3f color;
    int depth;
};


struct RayGenData
{
    uint32_t *fbPtr;
    vec2i fbSize;
    OptixTraversableHandle world;

    struct {
        vec3f pos;
        vec3f dir_00;
        vec3f dir_du;
        vec3f dir_dv;
    } camera;
};


struct TrianglesGeomData
{
    vec3f color;    // base color
    vec3f *vertex;  // vert buffer
    vec3i *index;   // vert indices buffer
    uint32_t *counter; // pointer to global hit counter
    OptixTraversableHandle world;
};


struct MissProgData
{
    vec3f color0; 
    vec3f color1;
};