#include "owl/owl.h"
#include "deviceCode.h"
#include "objLoader.h"
#include "sceneLoader.h"
#include "vtkExport.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <algorithm>
#include <stdexcept>

extern "C" char deviceCode_ptx[];

const char *sceneFileName = "scene.json";

// Light dimensions 
const int W = pow(2, 11);
const int H = pow(2, 11);

const vec2i fbSize(W, H);
const vec3f lookUp(0.0f, 1.0f, 0.0f);
const float orthoHeight = 1.0f;


struct SceneVars{
    SceneConfig config;
    std::vector<LightSource> activeLights;
};


// ---- CAMERA ----
void setupCameraFromLight(OWLRayGen rayGen, OWLBuffer frameBuffer, OWLGroup world, const LightSource& light, const vec2i& fbSize)
{
    vec3f pos = light.position;
    vec3f target = vec3f(0.0f, 0.0f, 0.0f);
    vec3f fwd = normalize(target - pos);
    float aspect = fbSize.x / float(fbSize.y);
    
    vec3f right(0.0f), up(0.0f), origin;

    if (light.isPunctual)
    {
        origin = pos;
    }
    else
    {
        right = orthoHeight * aspect * normalize(cross(fwd, lookUp));
        up = orthoHeight * normalize(cross(right, fwd));
        origin = pos - 0.5f * right - 0.5f * up;
    }

    owlRayGenSetBuffer(rayGen, "fbPtr", frameBuffer);
    owlRayGenSet2i(rayGen, "fbSize", (const owl2i&)fbSize);
    owlRayGenSetGroup(rayGen, "world", world);
    owlRayGenSet3f(rayGen, "camera.pos", (const owl3f&)origin);
    owlRayGenSet3f(rayGen, "camera.dir_00", (const owl3f&)fwd);
    owlRayGenSet3f(rayGen, "camera.dir_du", (const owl3f&)right);
    owlRayGenSet3f(rayGen, "camera.dir_dv", (const owl3f&)up);
    owlRayGenSet1i(rayGen, "isPunctual", light.isPunctual ? 1 : 0);
    owlRayGenSet1f(rayGen, "coneAngle", light.coneAngle);
}


std::vector<LightSource> generateSphereLights(int count, vec3f gridCenter, float boundingRadius,float marginFactor, float coneAngleMarginDeg)
{
    std::vector<LightSource> lights;
    lights.reserve(count);

    const float radius = marginFactor * boundingRadius;
    const float coneAngle = asinf(std::min(1.0f, boundingRadius / radius)) + coneAngleMarginDeg * (M_PI / 180.0f);
    const float goldenAngle = M_PI * (3.0f - sqrtf(5.0f));

    for (int i = 0; i < count; ++i)
    {
        float y = (count > 1) ? 1.0f - (i / float(count - 1)) * 2.0f : 0.0f;
        float r = sqrtf(std::max(0.0f, 1.0f - y * y));
        float theta = goldenAngle * i;

        vec3f pos = gridCenter + radius * vec3f(cosf(theta) * r, y, sinf(theta) * r);

        lights.push_back({pos, 100, 100, true, 90.0f, coneAngle});
    }

    return lights;
}

SceneVars createScene(){
    SceneConfig scene = loadScene(sceneFileName);

    std::vector<LightSource> activeLights;

    if (scene.sphereLights.enabled)
    {
        const vec3f gridCenter = scene.grid.origin + 0.5f * scene.grid.size;
        const float boundingRadius = 0.5f * length(scene.grid.size);

        activeLights = generateSphereLights(
            scene.sphereLights.count, gridCenter, boundingRadius,
            scene.sphereLights.marginFactor, scene.sphereLights.coneAngleMarginDeg
        );
    }
    else
    {
        activeLights = scene.lights;
    }

    if (activeLights.empty())
    {
        std::cerr << "No light sources in scene.json." << std::endl;
        return SceneVars();
    }

    SceneVars sceneVars = SceneVars();
    sceneVars.config = scene;
    sceneVars.activeLights = std::vector<LightSource>(activeLights);

    return sceneVars;
}



int main(int ac, char **av){
    // Initialize CUDA and Optix
    OWLContext context = owlContextCreate(nullptr, 1);

    OWLVarDecl implicitGeomVars[] = {
        { "type", OWL_INT, OWL_OFFSETOF(ImplicitGeomData, type) },
        { "param0", OWL_FLOAT, OWL_OFFSETOF(ImplicitGeomData, param0) },
        { "param1", OWL_FLOAT, OWL_OFFSETOF(ImplicitGeomData, param1) },
        { "world", OWL_GROUP, OWL_OFFSETOF(ImplicitGeomData, world) }
    };

    OWLGeomType implicitGeomType = owlGeomTypeCreate(
        context,
        OWL_GEOM_USER,
        sizeof(implicitGeomVars),
        implicitGeomVars,
        4
    );

    OWLVarDecl trianglesGeomVars[] = {
        { "index", OWL_BUFPTR, OWL_OFFSETOF(TrianglesGeomData, index) },
        { "vertex", OWL_BUFPTR, OWL_OFFSETOF(TrianglesGeomData, vertex) },
        { "world", OWL_GROUP, OWL_OFFSETOF(TrianglesGeomData, world)}
    };

    OWLGeomType trianglesGeomType = owlGeomTypeCreate(
        context,                    // Context
        OWL_TRIANGLES,              // Geometry type
        sizeof(TrianglesGeomData),  // Size
        trianglesGeomVars,          // Variables
        3                           // # of variables
    );


    // PTX = converted CUDA code
    OWLModule module = owlModuleCreate(context, deviceCode_ptx);

    //declare programs
    owlGeomTypeSetIntersectProg(implicitGeomType, 0, module, "Implicit");
    owlGeomTypeSetClosestHit(implicitGeomType, 0, module, "Implicit");
    owlGeomTypeSetBoundsProg(implicitGeomType, module, "Implicit");
    owlGeomTypeSetClosestHit(trianglesGeomType, 0, module, "TriangleMesh");

    // -------- DEFINE IMPLICIT SURFACE --------
    OWLGeom implicitGeom = owlGeomCreate(context, implicitGeomType);
    owlGeomSet1i(implicitGeom, "type", IMPLICIT_TORUS);
    owlGeomSet1f(implicitGeom, "param0", 0.75f);
    owlGeomSet1f(implicitGeom, "param1", 0.5f);
    owlGeomSetPrimCount(implicitGeom, 1);


    // Load scene from config
    SceneVars scene = createScene();

    const MeshInstance* causticsMeshConfig = nullptr;
    for (const MeshInstance& mesh : scene.config.meshes)
    {
        if (!mesh.isCausticsMesh)
            continue;
        if (causticsMeshConfig)
            throw std::runtime_error("scene.json must mark at most one mesh as isCausticsMesh.");
        causticsMeshConfig = &mesh;
    }

    TriangleMesh causticsMesh;
    if (causticsMeshConfig)
    {
        causticsMesh = loadObj(causticsMeshConfig->objPath);
        applyTransform(causticsMesh, causticsMeshConfig->position,
                       causticsMeshConfig->rotation, causticsMeshConfig->scale);
        if (causticsMesh.vertices.empty() || causticsMesh.indices.empty())
            throw std::runtime_error("The mesh marked isCausticsMesh has no triangles.");
        std::cout << "Loaded caustics ridge mesh " << causticsMeshConfig->objPath
                  << " (" << causticsMesh.vertices.size() << " vertices, "
                  << causticsMesh.indices.size() << " triangles)\n";
    }

    // Set grid params
    const vec3i gridDims = scene.config.grid.cellCount;
    const vec3f gridOrigin = scene.config.grid.origin;
    const vec3f gridCellSize = scene.config.grid.size / vec3f((float)gridDims.x, (float)gridDims.y, (float)gridDims.z);
    const int totalCells = gridDims.x * gridDims.y * gridDims.z;

    std::cout << "Grid: " << gridDims.x << "x" << gridDims.y << "x" << gridDims.z
              << " CellSize: " << gridCellSize.x << std::endl;

    // -------- BUFFER SETUP --------
    // ---- Reflection capture buffers ----
    const uint32_t reflectionCapacity = static_cast<uint32_t>(
        fbSize.x * fbSize.y * scene.activeLights.size());
    const int reflectionBufferSize = 3 * static_cast<int>(reflectionCapacity);
    std::vector<float> zerosReflection(reflectionBufferSize, 0.f);
    OWLBuffer reflectionPoints = owlDeviceBufferCreate(
        context, OWL_FLOAT, reflectionBufferSize, zerosReflection.data());
    const int zeroCounter = 0;
    OWLBuffer counterBuffer = owlDeviceBufferCreate(context, OWL_INT, 1, &zeroCounter);

    // ---- Grid-Buffer ----
    std::vector<double> zeros(totalCells, 0.f);

    OWLBuffer primaryGridBuffer = owlDeviceBufferCreate(context, OWL_USER_TYPE(double), totalCells, zeros.data());
    OWLBuffer bounceGridBuffer = owlDeviceBufferCreate(context, OWL_USER_TYPE(double), totalCells, zeros.data());

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
        { "isPunctual",     OWL_INT,    OWL_OFFSETOF(RayGenData, isPunctual)},
        { "coneAngle",      OWL_FLOAT,  OWL_OFFSETOF(RayGenData, camera.coneAngle)},
        { "reflectionPoints", OWL_BUFPTR, OWL_OFFSETOF(RayGenData, reflectionPoints)},
        { "reflectionCounter", OWL_BUFPTR, OWL_OFFSETOF(RayGenData, reflectionCounter)},
        { "reflectionCapacity", OWL_INT, OWL_OFFSETOF(RayGenData, reflectionCapacity)},
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
    owlRayGenSetBuffer(rayGen, "reflectionPoints", reflectionPoints);
    owlRayGenSetBuffer(rayGen, "reflectionCounter", counterBuffer);
    owlRayGenSet1i(rayGen, "reflectionCapacity", static_cast<int>(reflectionCapacity));
    owlRayGenSet3f(rayGen, "gridOrigin", (const owl3f&)gridOrigin);
    owlRayGenSet3f(rayGen, "gridCellSize", (const owl3f&)gridCellSize);
    owlRayGenSet3i(rayGen, "gridDims", (const owl3i&)gridDims);

    owlBuildPrograms(context);
    owlBuildPipeline(context);

    OWLGroup implicitGroup = owlUserGeomGroupCreate(context, 1, &implicitGeom);
    owlGroupBuildAccel(implicitGroup);

    OWLGroup world = owlInstanceGroupCreate(context, 1, &implicitGroup);
    owlGroupBuildAccel(world);

    // Primary rays only see the implicit surface. Secondary rays additionally
    // see the imported ridge mesh, so it cannot block the incoming rays.
    owlGeomSetGroup(implicitGeom, "world", world);
    if (causticsMeshConfig)
    {
        OWLBuffer vertexBuffer = owlDeviceBufferCreate(
            context, OWL_FLOAT3, causticsMesh.vertices.size(), causticsMesh.vertices.data());
        OWLBuffer indexBuffer = owlDeviceBufferCreate(
            context, OWL_INT3, causticsMesh.indices.size(), causticsMesh.indices.data());

        OWLGeom causticsGeom = owlGeomCreate(context, trianglesGeomType);
        owlTrianglesSetVertices(causticsGeom, vertexBuffer,
                               causticsMesh.vertices.size(), sizeof(vec3f), 0);
        owlTrianglesSetIndices(causticsGeom, indexBuffer,
                              causticsMesh.indices.size(), sizeof(vec3i), 0);
        owlGeomSetBuffer(causticsGeom, "vertex", vertexBuffer);
        owlGeomSetBuffer(causticsGeom, "index", indexBuffer);

        OWLGroup causticsGeomGroup = owlTrianglesGeomGroupCreate(context, 1, &causticsGeom);
        owlGroupBuildAccel(causticsGeomGroup);
        OWLGroup queryGroups[] = { implicitGroup, causticsGeomGroup };
        OWLGroup secondaryWorld = owlInstanceGroupCreate(context, 2, queryGroups);
        owlGroupBuildAccel(secondaryWorld);
        owlGeomSetGroup(implicitGeom, "world", secondaryWorld);
        owlGeomSetGroup(causticsGeom, "world", secondaryWorld);
    }

    // -------- RENDER FOR EACH LIGHT SOURCE --------
    for (int i = 0; i < scene.activeLights.size(); ++i)
    {
        const LightSource& light = scene.activeLights[i];

        std::cout << "Rendering Light " << i
                  << " at (" << light.position.x << ", " << light.position.y << ", " << light.position.z << ")";
        
        setupCameraFromLight(rayGen, frameBuffer, world, light, fbSize);
        owlBuildSBT(context);
        owlRayGenLaunch2D(rayGen, fbSize.x, fbSize.y);
    }
    std::cout << "Done\n";

    // -------- READ GRID + EXPORT VTK --------
    // primaryGridBuffer/bounceGridBuffer are device buffers; read back to host
    cudaDeviceSynchronize();

    std::vector<double> hostPrimary(totalCells);
    std::vector<double> hostBounce(totalCells);
    std::vector<float>  hostReflections(reflectionBufferSize);
    int hostReflectionCount = 0;

    cudaMemcpy(hostPrimary.data(), owlBufferGetPointer(primaryGridBuffer, 0),
               totalCells * sizeof(double), cudaMemcpyDeviceToHost);
    cudaMemcpy(hostBounce.data(), owlBufferGetPointer(bounceGridBuffer, 0),
               totalCells * sizeof(double), cudaMemcpyDeviceToHost);
    cudaMemcpy(hostReflections.data(), owlBufferGetPointer(reflectionPoints, 0),
               reflectionBufferSize * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(&hostReflectionCount, owlBufferGetPointer(counterBuffer, 0),
               sizeof(int), cudaMemcpyDeviceToHost);

    const std::size_t exportedReflectionCount = static_cast<std::size_t>(
        std::max(0, std::min(hostReflectionCount, static_cast<int>(reflectionCapacity))));
    
    const double invN = 1.0 / (double)scene.activeLights.size();
    //for (auto& v : hostPrimary) v *= invN;
    //for (auto& v : hostBounce)  v *= invN;

    exportVTI("caustics_primary.vtu", hostPrimary.data(), gridDims, gridOrigin, gridCellSize, "primary");
    exportVTI("caustics_bounce.vtu", hostBounce.data(), gridDims, gridOrigin, gridCellSize, "bounce");
    exportPointsVTP("caustic_reflection_points.vtp", hostReflections.data(), exportedReflectionCount);
    
    std::cout << "Reflected rays hitting the caustics mesh: "
              << exportedReflectionCount << std::endl;
    if (hostReflectionCount > static_cast<int>(reflectionCapacity))
        std::cerr << "Warning: reflection-point capture reached its capacity; some points were not exported.\n";

    // -------- CLEAN UP --------
    owlBufferRelease(frameBuffer);
    owlBufferRelease(counterBuffer);
    // for (auto& vb : vertexBuffers) owlBufferRelease(vb);
    // for (auto& ib : indexBuffers) owlBufferRelease(ib);
    owlRayGenRelease(rayGen);
    owlModuleRelease(module);
    owlContextDestroy(context);

    return 0;
}