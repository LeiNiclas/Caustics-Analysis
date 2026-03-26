#include "owl/owl.h"
#include "deviceCode.h"
#include "objLoader.h"
#include "sceneLoader.h"
#include "vtkExport.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

extern "C" char deviceCode_ptx[];

const char *sceneFileName = "scene.json";

// Image dimensions 
const int W = 512;
const int H = 512;

const vec2i fbSize(W, H);
const vec3f lookUp(0.0f, 1.0f, 0.0f);
const float orthoHeight = 1.0f;

#define LOG(message)                                            \
    std::cout << OWL_TERMINAL_BLUE;                             \
    std::cout << "context.sample(main): " << mesag << std::endl;    \
    std::cout << OWL_TERMINAL_DEFAULT;

#define LOG_OK(message)                                         \
    std::cout << OWL_TERMINAL_LIGHT_BLUE;                         \
    std::cout << "#context.sample(main): " << message << std::endl;   \
    std::cout << OWL_TERMINAL_DEFAULT;


// ---- CAMERA ----
void setupCameraFromLight(OWLRayGen rayGen, OWLBuffer frameBuffer, OWLGroup world, const LightSource& light, const vec2i& fbSize)
{
    vec3f pos = light.position;
    vec3f target = vec3f(0.0f, 0.0f, 0.0f);
    vec3f fwd = normalize(target - pos);

    float aspect = fbSize.x / float(fbSize.y);
    vec3f right = orthoHeight * aspect * normalize(cross(fwd, lookUp));
    vec3f up = orthoHeight * normalize(cross(right, fwd));

    // Origin of ray set to the bottom left corner
    vec3f origin = pos - 0.5f * right - 0.5f * up;

    owlRayGenSetBuffer(rayGen, "fbPtr", frameBuffer);
    owlRayGenSet2i(rayGen, "fbSize", (const owl2i&)fbSize);
    owlRayGenSetGroup(rayGen, "world", world);
    owlRayGenSet3f(rayGen, "camera.pos", (const owl3f&)origin);
    owlRayGenSet3f(rayGen, "camera.dir_00", (const owl3f&)fwd);
    owlRayGenSet3f(rayGen, "camera.dir_du", (const owl3f&)right);
    owlRayGenSet3f(rayGen, "camera.dir_dv", (const owl3f&)up);
}


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
        { "counter", OWL_BUFPTR, OWL_OFFSETOF(TrianglesGeomData, counter) },
        { "world", OWL_GROUP, OWL_OFFSETOF(TrianglesGeomData, world)}
    };

    
    OWLGeomType trianglesGeomType = owlGeomTypeCreate(
        context,                    // Context
        OWL_TRIANGLES,              // Geometry type
        sizeof(TrianglesGeomData),  // Size
        trianglesGeomVars,          // Variables
        5                           // # of variables
    );

    owlGeomTypeSetClosestHit(trianglesGeomType, 0, module, "TriangleMesh");


    // -------- LOAD SCENE --------
    SceneConfig scene = loadScene(sceneFileName);

    if (scene.meshes.empty())
    {
        std::cerr << "No meshes in scene.json." << std::endl;
        return 1;
    }
    if (scene.ligths.empty())
    {
        std::cerr << "No light sources in scene.json." << std::endl;
        return 1;
    }

    // Grid params
    const vec3i gridDims = scene.grid.cellCount;
    const vec3f gridOrigin = scene.grid.origin;
    const vec3f gridCellSize = scene.grid.size / vec3f((float)gridDims.x, (float)gridDims.y, (float)gridDims.z);
    const int totalCells = gridDims.x * gridDims.y * gridDims.z;

    std::cout << "Grid: " << gridDims.x << "x" << gridDims.y << "x" << gridDims.z
              << " CellSize: " << gridCellSize.x << std::endl;

    // -------- BUFFER SETUP --------
    // ---- Counter-Buffer ----
    OWLBuffer counterBuffer = owlHostPinnedBufferCreate(context, OWL_INT, 1);
    uint32_t* counterInit = (uint32_t*)owlBufferGetPointer(counterBuffer, 0);
    *counterInit = 0;

    // ---- Grid-Buffer ----
    std::vector<uint32_t> zeros(totalCells, 0u);

    OWLBuffer primaryGridBuffer = owlDeviceBufferCreate(context, OWL_INT, totalCells, zeros.data());
    OWLBuffer bounceGridBuffer = owlDeviceBufferCreate(context, OWL_INT, totalCells, zeros.data());

    
    // -------- LOAD MESHES + BUILD GEOMETRY --------
    std::vector<OWLGroup> geomGroups;
    std::vector<OWLBuffer> vertexBuffers;
    std::vector<OWLBuffer> indexBuffers;
    std::vector<OWLGeom> geoms;


    for (const MeshInstance& meshInst : scene.meshes)
    {
        // Load .obj and apply transform
        TriangleMesh mesh = loadObj(meshInst.objPath);
        applyTransform(mesh, meshInst.position, meshInst.rotation, meshInst.scale);

        OWLBuffer vb = owlDeviceBufferCreate(context, OWL_FLOAT3, mesh.vertices.size(), mesh.vertices.data());
        OWLBuffer ib = owlDeviceBufferCreate(context, OWL_INT3, mesh.indices.size(), mesh.indices.data());

        vertexBuffers.push_back(vb);
        indexBuffers.push_back(ib);

        OWLGeom geom = owlGeomCreate(context, trianglesGeomType);

        owlTrianglesSetVertices(geom, vb, mesh.vertices.size(), sizeof(vec3f), 0);
        owlTrianglesSetIndices(geom, ib, mesh.indices.size(), sizeof(vec3i), 0);

        owlGeomSetBuffer(geom, "vertex", vb);
        owlGeomSetBuffer(geom, "index", ib);
        owlGeomSetBuffer(geom, "counter", counterBuffer);
        owlGeomSet3f(geom, "color", owl3f{0.0f, 1.0f, 0.0f});

        geoms.push_back(geom);

        OWLGroup triGroup = owlTrianglesGeomGroupCreate(context, 1, &geom);
        owlGroupBuildAccel(triGroup);
        geomGroups.push_back(triGroup);
    }

    OWLGroup world = owlInstanceGroupCreate(context, (uint32_t)geomGroups.size(), geomGroups.data());

    owlGroupBuildAccel(world);

    // Set world handle for every geometry (Used for Bounce-Rays)
    for (OWLGeom geom : geoms)
        owlGeomSetGroup(geom, "world", world);
    

    // -------- RAY GENERATION SHADER SETUP --------
    // ---- Miss Program ----
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

    // ---- Ray Generation ----
    OWLVarDecl rayGenVars[] =
    {
        { "fbPtr",          OWL_BUFPTR, OWL_OFFSETOF(RayGenData, fbPtr)},
        { "fbSize",         OWL_INT2,   OWL_OFFSETOF(RayGenData, fbSize)},
        { "world",          OWL_GROUP,  OWL_OFFSETOF(RayGenData, world)},
        { "camera.pos",     OWL_FLOAT3, OWL_OFFSETOF(RayGenData, camera.pos)},
        { "camera.dir_00",  OWL_FLOAT3, OWL_OFFSETOF(RayGenData, camera.dir_00)},
        { "camera.dir_du",  OWL_FLOAT3, OWL_OFFSETOF(RayGenData, camera.dir_du)},
        { "camera.dir_dv",  OWL_FLOAT3, OWL_OFFSETOF(RayGenData, camera.dir_dv)},
        { "primaryGrid",    OWL_BUFPTR, OWL_OFFSETOF(RayGenData, primaryGrid)},
        { "bounceGrid",     OWL_BUFPTR, OWL_OFFSETOF(RayGenData, bounceGrid)},
        { "gridOrigin",     OWL_FLOAT3, OWL_OFFSETOF(RayGenData, gridOrigin)},
        { "gridCellSize",   OWL_FLOAT3, OWL_OFFSETOF(RayGenData, gridCellSize)},
        { "gridDims",       OWL_INT3,   OWL_OFFSETOF(RayGenData, gridDims)},
        { /* sentinel to mark end of list */ }
    };

    OWLRayGen rayGen = owlRayGenCreate(
        context,            // Context
        module,             // Module
        "rayGen",           // Name
        sizeof(RayGenData), // Size
        rayGenVars,         // Variables
        -1                  // ???
    );

    OWLBuffer frameBuffer = owlHostPinnedBufferCreate(context, OWL_INT, fbSize.x * fbSize.y);

    // Set grid data into the raygen once (shared across lights)
    owlRayGenSetBuffer(rayGen, "primaryGrid", primaryGridBuffer);
    owlRayGenSetBuffer(rayGen, "bounceGrid", bounceGridBuffer);
    owlRayGenSet3f(rayGen, "gridOrigin", (const owl3f&)gridOrigin);
    owlRayGenSet3f(rayGen, "gridCellSize", (const owl3f&)gridCellSize);
    owlRayGenSet3i(rayGen, "gridDims", (const owl3i&)gridDims);

    owlBuildPrograms(context);
    owlBuildPipeline(context);

    // ---- PERSPECTIVE CAMERA ----
    // vec3f camera_ddu = cosFovy * aspect * normalize(cross(camera_d00, lookUp)); //right
    // vec3f camera_ddv = cosFovy * normalize(cross(camera_ddu, camera_d00)); //up
    // camera_d00 -= 0.5f * camera_ddu;
    // camera_d00 -= 0.5f * camera_ddv;
    
    

    // -------- RENDER FOR EACH LIGHT SOURCE + SAVE PNG --------
    for (int i = 0; i < (int)scene.ligths.size(); ++i)
    {
        const LightSource& light = scene.ligths[i];

        std::cout << "Rendering Light " << i
                  << " at (" << light.position.x << ", " << light.position.y << ", " << light.position.z << ")";
        
        setupCameraFromLight(rayGen, frameBuffer, world, light, fbSize);
        owlBuildSBT(context);
        owlRayGenLaunch2D(rayGen, fbSize.x, fbSize.y);

        std::string filename = "light_" + std::to_string(i) + ".png";

        std::cout << " [Saving...] " << std::endl;
        
        const uint32_t* fb = (const uint32_t*)owlBufferGetPointer(frameBuffer, 0);
        stbi_write_png(
            filename.c_str(),
            fbSize.x,
            fbSize.y,
            4,
            fb,
            fbSize.x * sizeof(uint32_t)
        );

        std::cout << " -> " << filename << " saved." << std::endl;
    }

    // -------- READ GRID + EXPORT VTK --------
    // primaryGridBuffer/bounceGridBuffer are device buffers; read back to host
    cudaDeviceSynchronize();

    std::vector<uint32_t> hostPrimary(totalCells);
    std::vector<uint32_t> hostBounce(totalCells);

    cudaMemcpy(hostPrimary.data(), owlBufferGetPointer(primaryGridBuffer, 0),
               totalCells * sizeof(uint32_t), cudaMemcpyDeviceToHost);
    cudaMemcpy(hostBounce.data(), owlBufferGetPointer(bounceGridBuffer, 0),
               totalCells * sizeof(uint32_t), cudaMemcpyDeviceToHost);

    exportVTI("caustics_primary.vti", hostPrimary.data(), gridDims, gridOrigin, gridCellSize, "primary");
    exportVTI("caustics_bounce.vti", hostBounce.data(), gridDims, gridOrigin, gridCellSize, "bounce");

    // read back the counter from the host-pinned buffer
    const uint32_t *counterPtr = (const uint32_t*)owlBufferGetPointer(counterBuffer, 0);
    const uint32_t counterVal = *counterPtr;
    std::cout << "Total Hit-Count (all Lights): " << counterVal << std::endl;

    // -------- CLEAN UP --------
    owlBufferRelease(frameBuffer);
    owlBufferRelease(counterBuffer);
    for (auto& vb : vertexBuffers) owlBufferRelease(vb);
    for (auto& ib : indexBuffers) owlBufferRelease(ib);
    owlRayGenRelease(rayGen);
    owlModuleRelease(module);
    owlContextDestroy(context);

    return 0;
}