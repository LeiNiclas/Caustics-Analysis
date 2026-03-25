#include "objLoader.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"


TriangleMesh loadObj(const std::string& filename)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str());
    if (!ret) throw std::runtime_error("Failed to load OBJ: " + err);

    TriangleMesh mesh;
    for (auto& shape : shapes) {
        for (size_t i = 0; i < shape.mesh.indices.size(); i += 3) {
            mesh.indices.push_back({
                shape.mesh.indices[i+0].vertex_index,
                shape.mesh.indices[i+1].vertex_index,
                shape.mesh.indices[i+2].vertex_index
            });
        }
    }

    for (size_t i = 0; i < attrib.vertices.size(); i += 3) {
        mesh.vertices.push_back({
            attrib.vertices[i+0],
            attrib.vertices[i+1],
            attrib.vertices[i+2]
        });
    }

    return mesh;
}

// Scale -> RotateX -> RotateY -> RotateZ -> Translate
// rotDeg = Euler-Angle in Degrees (Unity export)
void applyTransform(TriangleMesh& mesh, const owl::vec3f& pos, const owl::vec3f& rotDeg, const owl::vec3f& scale)
{
    const float toRad = 3.1415926535f / 180.0f;

    float rx = rotDeg.x * toRad;
    float ry = rotDeg.y * toRad;
    float rz = rotDeg.z * toRad;

    float cX = cosf(rx);
    float sX = sinf(rx);
    float cY = cosf(ry);
    float sY = sinf(ry);
    float cZ = cosf(rz);
    float sZ = sinf(rz);
    
    // Rotation matrix
    // Row 0
    float r00 = cY * cZ;
    float r01 = cZ * sX * sY - cX * sZ;
    float r02 = cX * cZ * sY + sX * sZ;

    // Row 1
    float r10 = cY * sZ;
    float r11 = cX * cZ + sX * sY * sZ;
    float r12 = cX * sY * sZ - cZ * sX;

    // Row 2
    float r20 = -sY;
    float r21 = cY * sX;
    float r22 = cX * cY;

    for (auto& v : mesh.vertices)
    {
        // Scale
        float x = v.x * scale.x;
        float y = v.y * scale.y;
        float z = v.z * scale.z;

        // Rotate
        float rx2 = r00 * x + r01 * y + r02 * z;
        float ry2 = r10 * x + r11 * y + r12 * z;
        float rz2 = r20 * x + r21 * y + r22 * z;

        // Translate
        // Unity -> OptiX: Flip Z-Axis (Left to Righthanded system)
        v.x = rx2 + pos.x;
        v.y = ry2 + pos.y;
        v.z = rz2 - pos.z;
    }
}