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