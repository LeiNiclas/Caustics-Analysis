#include "owl/owl.h"
#include "deviceCode.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

extern "C" char deviceCode_ptx[];


const char *outFileName = "simpleTriangles.png";

// Image dimensions 
const int W = 100;
const int H = 100;

const vec2i fbSize(W, H);


#define LOG(message)                                            \
    std::cout << OWL_TERMINAL_BLUE;                             \
    std::cout << "context.sample(main): " << mesag << std::endl;    \
    std::cout << OWL_TERMINAL_DEFAULT;

#define LOG_OK(message)                                         \
    std::cout << OWL_TERMINAL_LIGHT_BLUE;                         \
    std::cout << "#context.sample(main): " << message << std::endl;   \
    std::cout << OWL_TERMINAL_DEFAULT;


// ---- CAMERA ----
const vec3f lookFrom(0.0f, 0.0f, -2.0f);
const vec3f lookAt(0.0f, 0.0f, 0.0f);
const vec3f lookUp(0.0f, 1.0f, 0.0f);
const float cosFovy = 0.66f;


// ---- VERTS & INDICES ----
const int NUM_VERTS = 8;
vec3f verts[NUM_VERTS] = {
    { -1.0f, -1.0f, -1.0f }, // Front   Bottom  Left
    {  1.0f, -1.0f, -1.0f }, // Front   Bottom  Right
    { -1.0f,  1.0f, -1.0f }, // Front   Top     left
    {  1.0f,  1.0f, -1.0f }, // Front   Top     Right
    { -1.0f, -1.0f,  1.0f }, // Back    Bottom  Left
    {  1.0f, -1.0f,  1.0f }, // Back    Bottom  Right
    { -1.0f,  1.0f,  1.0f }, // Back    Top     left
    {  1.0f,  1.0f,  1.0f }, // Back    Top     Right
};

const int NUM_INDICES = 12;
vec3i indices[NUM_INDICES] = {
    { 0, 1, 3 }, { 2, 3, 0 },
    { 5, 7, 6 }, { 5, 6, 4 },
    { 0, 4, 5 }, { 0, 5, 1 },
    { 2, 3, 7 }, { 2, 7, 6 },
    { 1, 5, 7 }, { 1, 7, 3 },
    { 4, 0, 2 }, { 4, 2, 6 }
};


int main(int ac, char **av){
    // Initialize CUDA and Optix
    OWLContext context = owlContextCreate(nullptr, 1);
    
    // Module creation
    // PTX = converted CUDA code
    OWLModule module = owlModuleCreate(context, deviceCode_ptx);

    // -------- VAR DECLARATIONS & GEOMETRY INITIALIZATION --------
    // Geometry type needs to be in deviceCode.h
    OWLVarDecl trianglesGeomVars[] = {
        { "index", OWL_BUFPTR, OWL_OFFSETOF(TrianglesGeomData, index) },
        { "vertex", OWL_BUFPTR, OWL_OFFSETOF(TrianglesGeomData, vertex) },
        { "color", OWL_FLOAT3, OWL_OFFSETOF(TrianglesGeomData, color) },
        { "counter", OWL_BUFPTR, OWL_OFFSETOF(TrianglesGeomData, counter) }
    };
    
    OWLGeomType trianglesGeomType = owlGeomTypeCreate(
        context,                    // Context
        OWL_TRIANGLES,              // Geometry type
        sizeof(TrianglesGeomData),  // Size
        trianglesGeomVars,          // Variables
        4                           // # of variables
    );

    owlGeomTypeSetClosestHit(
        trianglesGeomType,  // Geometry type
        0,                  // Ray type
        module,             // Module
        "TriangleMesh"      // Program name
    );

    // -------- BUILD MESHES --------
    // owlDeviceBufferCreate() creates a device buffer
    // where every device has its own local copy of the given buffer

    // Vertex Buffer
    OWLBuffer vertexBuffer = owlDeviceBufferCreate(
        context,        // Context
        OWL_FLOAT3,     // Datatype
        NUM_VERTS,      // Count
        verts           // Data
    );

    // Index Buffer
    OWLBuffer indexBuffer = owlDeviceBufferCreate(
        context,
        OWL_INT3,
        NUM_INDICES,
        indices
    );

    int zero = 0;

    // Use a host-pinned buffer for the counter so the host can read it after launch
    OWLBuffer counterBuffer = owlHostPinnedBufferCreate(
        context,
        OWL_INT,
        1
    );
    // initialize counter to zero
    uint32_t *counterInit = (uint32_t*)owlBufferGetPointer(counterBuffer, 0);
    *counterInit = 0;
    
    OWLBuffer frameBuffer = owlHostPinnedBufferCreate(
        context,
        OWL_INT,
        fbSize.x * fbSize.y
    );

    // Create triangle geometry
    OWLGeom trianglesGeom = owlGeomCreate(context, trianglesGeomType);

    owlTrianglesSetVertices(
        trianglesGeom,  // Triangle geometry
        vertexBuffer,   // Vertices
        NUM_VERTS,   // # of vertices
        sizeof(vec3f),  // Stride
        0               // Offset
    );
    
    owlTrianglesSetIndices(
        trianglesGeom,  // Triangle geometry
        indexBuffer,    // Indices
        NUM_INDICES,    // # of indices
        sizeof(vec3i),  // Stride
        0               // Offset
    );

    owlGeomSetBuffer(
        trianglesGeom,
        "vertex",
        vertexBuffer
    );

    owlGeomSetBuffer(
        trianglesGeom,
        "index",
        indexBuffer
    );

    owlGeomSet3f(trianglesGeom, "color", owl3f{0, 1, 0});

    // provide the counter buffer to the geometry so the closest-hit can increment it
    owlGeomSetBuffer(
        trianglesGeom,
        "counter",
        counterBuffer
    );

    // Group + Acceleration structure
    OWLGroup trianglesGroup = owlTrianglesGeomGroupCreate(
        context,
        1,
        &trianglesGeom
    );
    owlGroupBuildAccel(trianglesGroup);

    // Instance the group
    OWLGroup world = owlInstanceGroupCreate(
        context,
        1,
        &trianglesGroup
    );
    owlGroupBuildAccel(world);

    // -------- RAY GENERATION SHADER SETUP --------
    OWLVarDecl missProgVars[] = {
        { "color0", OWL_FLOAT3, OWL_OFFSETOF(MissProgData, color0) },
        { "color1", OWL_FLOAT3, OWL_OFFSETOF(MissProgData, color1) },
        { /* Sentinel */ }
    };

    OWLMissProg missProg = owlMissProgCreate(
        context,
        module,
        "miss",
        sizeof(MissProgData),
        missProgVars,
        -1
    );

    owlMissProgSet3f(missProg, "color0", owl3f{0.8f, 0.0f, 0.0f});
    owlMissProgSet3f(missProg, "color1", owl3f{0.8f, 0.8f, 0.8f});


    OWLVarDecl rayGenVars[] =
    {
        { "fbPtr",         OWL_BUFPTR, OWL_OFFSETOF(RayGenData,fbPtr)},
        { "fbSize",        OWL_INT2,   OWL_OFFSETOF(RayGenData,fbSize)},
        { "world",         OWL_GROUP,  OWL_OFFSETOF(RayGenData,world)},
        { "camera.pos",    OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.pos)},
        { "camera.dir_00", OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.dir_00)},
        { "camera.dir_du", OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.dir_du)},
        { "camera.dir_dv", OWL_FLOAT3, OWL_OFFSETOF(RayGenData,camera.dir_dv)},
        { /* sentinel to mark end of list */ }
    };

    OWLRayGen rayGen = owlRayGenCreate(
        context,            // Context
        module,             // Module
        "simpleRayGen",     // Name
        sizeof(RayGenData), // Size
        rayGenVars,         // Variables
        -1                  // ???
    );


    // Compute camera variable values
    vec3f camera_pos = lookFrom;
    vec3f camera_d00 = normalize(lookAt - lookFrom);

    float aspect = fbSize.x / float(fbSize.y);

    vec3f camera_ddu = cosFovy * aspect * normalize(cross(camera_d00, lookUp));
    vec3f camera_ddv = cosFovy * normalize(cross(camera_ddu, camera_d00));

    camera_d00 -= 0.5f * camera_ddu;
    camera_d00 -= 0.5f * camera_ddv;

    // -------- SHADER BINDING TABLE TO TRACE GROUPS --------
    // Set RayGen variables
    owlRayGenSetBuffer(rayGen, "fbPtr", frameBuffer);
    owlRayGenSet2i(rayGen, "fbSize", (const owl2i&)fbSize);
    owlRayGenSetGroup(rayGen, "world", world);
    owlRayGenSet3f(rayGen, "camera.pos", (const owl3f&)camera_pos);
    owlRayGenSet3f(rayGen, "camera.dir_00", (const owl3f&)camera_d00);
    owlRayGenSet3f(rayGen, "camera.dir_du", (const owl3f&)camera_ddu);
    owlRayGenSet3f(rayGen, "camera.dir_dv", (const owl3f&)camera_ddv);

    owlBuildPrograms(context);
    owlBuildPipeline(context);
    owlBuildSBT(context);

    // -------- LAUNCH RAY GENERATION --------
    owlRayGenLaunch2D(rayGen, fbSize.x, fbSize.y);
    
    // Write results to file
    const uint32_t *fb = (const uint32_t*)owlBufferGetPointer(frameBuffer, 0);
    stbi_write_png(
        outFileName,
        fbSize.x,
        fbSize.y,
        4,
        fb,
        fbSize.x * sizeof(uint32_t)
    );

    // read back the counter from the host-pinned buffer
    const uint32_t *counterPtr = (const uint32_t*)owlBufferGetPointer(counterBuffer, 0);
    const uint32_t counterVal = *counterPtr;
    std::cout << counterVal << std::endl;

    // -------- CLEAN UP --------
    owlModuleRelease(module);
    owlRayGenRelease(rayGen);
    owlBufferRelease(frameBuffer);
    owlContextDestroy(context);
}