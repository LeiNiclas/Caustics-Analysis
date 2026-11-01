#include "objLoader.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <utility>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

namespace
{
struct HullFace
{
    int a;
    int b;
    int c;
    std::vector<int> outsidePoints;
};

float signedDistance(const HullFace& face, const std::vector<owl::vec3f>& points,
                     const owl::vec3f& point)
{
    const owl::vec3f normal = owl::cross(
        points[face.b] - points[face.a],
        points[face.c] - points[face.a]);
    const float normalLength = owl::length(normal);
    return normalLength > 0.0f
        ? owl::dot(normal, point - points[face.a]) / normalLength
        : 0.0f;
}
}


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

void saveObj(const TriangleMesh& mesh, const std::string& filename)
{
    std::ofstream output(filename, std::ios::trunc);
    if (!output)
        throw std::runtime_error("Failed to write OBJ: " + filename);

    output << std::setprecision(std::numeric_limits<float>::max_digits10);
    for (const owl::vec3f& vertex : mesh.vertices)
        output << "v " << vertex.x << ' ' << vertex.y << ' ' << vertex.z << '\n';
    for (const owl::vec3i& triangle : mesh.indices)
        output << "f " << triangle.x + 1 << ' ' << triangle.y + 1 << ' '
               << triangle.z + 1 << '\n';

    if (!output)
        throw std::runtime_error("Failed while writing OBJ: " + filename);
}

TriangleMesh convexHull(const TriangleMesh& mesh)
{
    std::vector<owl::vec3f> points = mesh.vertices;
    std::sort(points.begin(), points.end(), [](const owl::vec3f& a, const owl::vec3f& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    points.erase(std::unique(points.begin(), points.end(), [](const owl::vec3f& a, const owl::vec3f& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }), points.end());

    if (points.size() < 4)
        throw std::runtime_error("Cannot compute a 3D convex hull from fewer than four unique vertices.");

    owl::vec3f minPoint = points.front();
    owl::vec3f maxPoint = points.front();
    for (const owl::vec3f& point : points)
    {
        minPoint.x = std::min(minPoint.x, point.x);
        minPoint.y = std::min(minPoint.y, point.y);
        minPoint.z = std::min(minPoint.z, point.z);
        maxPoint.x = std::max(maxPoint.x, point.x);
        maxPoint.y = std::max(maxPoint.y, point.y);
        maxPoint.z = std::max(maxPoint.z, point.z);
    }
    const float scale = std::max(1.0f, owl::length(maxPoint - minPoint));
    const float epsilon = scale * 1e-6f;
    const float epsilonSquared = epsilon * epsilon;

    const int p0 = 0;
    int p1 = 1;
    float maxDistanceSquared = 0.0f;
    for (int i = 1; i < static_cast<int>(points.size()); ++i)
    {
        const owl::vec3f delta = points[i] - points[p0];
        const float distanceSquared = owl::dot(delta, delta);
        if (distanceSquared > maxDistanceSquared)
        {
            maxDistanceSquared = distanceSquared;
            p1 = i;
        }
    }
    if (maxDistanceSquared <= epsilonSquared)
        throw std::runtime_error("Cannot compute a 3D convex hull from coincident vertices.");

    const owl::vec3f line = points[p1] - points[p0];
    int p2 = -1;
    float maxLineDistanceSquared = 0.0f;
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const owl::vec3f offset = points[i] - points[p0];
        const owl::vec3f perpendicular = owl::cross(line, offset);
        const float distanceSquared = owl::dot(perpendicular, perpendicular) / maxDistanceSquared;
        if (distanceSquared > maxLineDistanceSquared)
        {
            maxLineDistanceSquared = distanceSquared;
            p2 = i;
        }
    }
    if (p2 < 0 || maxLineDistanceSquared <= epsilonSquared)
        throw std::runtime_error("Cannot compute a 3D convex hull from collinear vertices.");

    HullFace baseFace{ p0, p1, p2, {} };
    int p3 = -1;
    float maxPlaneDistance = 0.0f;
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const float distance = std::fabs(signedDistance(baseFace, points, points[i]));
        if (distance > maxPlaneDistance)
        {
            maxPlaneDistance = distance;
            p3 = i;
        }
    }
    if (p3 < 0 || maxPlaneDistance <= epsilon)
        throw std::runtime_error("Cannot compute a 3D convex hull from coplanar vertices.");

    const owl::vec3f interior =
        (points[p0] + points[p1] + points[p2] + points[p3]) * 0.25f;
    const auto makeFace = [&points, &interior](int a, int b, int c) {
        HullFace face{ a, b, c, {} };
        if (signedDistance(face, points, interior) > 0.0f)
            std::swap(face.b, face.c);
        return face;
    };

    std::vector<HullFace> faces = {
        makeFace(p0, p1, p2), makeFace(p0, p3, p1),
        makeFace(p0, p2, p3), makeFace(p1, p3, p2)
    };
    const auto assignOutsidePoint = [&points, epsilon](
        int pointIndex, std::vector<HullFace>& candidateFaces)
    {
        int bestFace = -1;
        float bestDistance = epsilon;
        for (int faceIndex = 0; faceIndex < static_cast<int>(candidateFaces.size()); ++faceIndex)
        {
            const float distance = signedDistance(
                candidateFaces[faceIndex], points, points[pointIndex]);
            if (distance > bestDistance)
            {
                bestDistance = distance;
                bestFace = faceIndex;
            }
        }
        if (bestFace >= 0)
            candidateFaces[bestFace].outsidePoints.push_back(pointIndex);
    };

    std::vector<bool> isSeed(points.size(), false);
    isSeed[p0] = isSeed[p1] = isSeed[p2] = isSeed[p3] = true;
    for (int pointIndex = 0; pointIndex < static_cast<int>(points.size()); ++pointIndex)
        if (!isSeed[pointIndex])
            assignOutsidePoint(pointIndex, faces);

    for (;;)
    {
        int seedFaceIndex = -1;
        int eyePoint = -1;
        for (int faceIndex = 0; faceIndex < static_cast<int>(faces.size()); ++faceIndex)
        {
            if (!faces[faceIndex].outsidePoints.empty())
            {
                seedFaceIndex = faceIndex;
                break;
            }
        }
        if (seedFaceIndex < 0)
            break;
        float farthestDistance = epsilon;
        for (int pointIndex : faces[seedFaceIndex].outsidePoints)
        {
            const float distance = signedDistance(
                faces[seedFaceIndex], points, points[pointIndex]);
            if (distance > farthestDistance)
            {
                farthestDistance = distance;
                eyePoint = pointIndex;
            }
        }
        if (eyePoint < 0)
        {
            faces[seedFaceIndex].outsidePoints.clear();
            continue;
        }

        std::vector<bool> visible(faces.size(), false);
        std::vector<std::pair<int, int>> horizon;
        std::vector<int> orphanedPoints;
        for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
        {
            if (signedDistance(faces[faceIndex], points, points[eyePoint]) <= epsilon)
                continue;

            visible[faceIndex] = true;
            const HullFace& face = faces[faceIndex];
            for (int pointIndex : face.outsidePoints)
                if (pointIndex != eyePoint)
                    orphanedPoints.push_back(pointIndex);

            const std::pair<int, int> edges[] = {
                { face.a, face.b }, { face.b, face.c }, { face.c, face.a }
            };
            for (const auto& edge : edges)
            {
                const auto reverse = std::find(
                    horizon.begin(), horizon.end(),
                    std::make_pair(edge.second, edge.first));
                if (reverse != horizon.end())
                    horizon.erase(reverse);
                else
                    horizon.push_back(edge);
            }
        }

        std::vector<HullFace> remainingFaces;
        remainingFaces.reserve(faces.size() + horizon.size());
        for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
            if (!visible[faceIndex])
                remainingFaces.push_back(faces[faceIndex]);
        for (const auto& edge : horizon)
            remainingFaces.push_back(makeFace(edge.first, edge.second, eyePoint));
        faces.swap(remainingFaces);

        const std::size_t firstNewFace = faces.size() - horizon.size();
        std::vector<HullFace> newFaces(
            faces.begin() + firstNewFace, faces.end());
        for (int pointIndex : orphanedPoints)
            assignOutsidePoint(pointIndex, newFaces);
        std::copy(newFaces.begin(), newFaces.end(), faces.begin() + firstNewFace);
    }

    TriangleMesh hull;
    hull.indices.reserve(faces.size());
    std::vector<int> vertexRemap(points.size(), -1);
    const auto remapVertex = [&hull, &points, &vertexRemap](int sourceIndex) {
        int& destinationIndex = vertexRemap[sourceIndex];
        if (destinationIndex < 0)
        {
            destinationIndex = static_cast<int>(hull.vertices.size());
            hull.vertices.push_back(points[sourceIndex]);
        }
        return destinationIndex;
    };
    for (const HullFace& face : faces)
        hull.indices.push_back({
            remapVertex(face.a), remapVertex(face.b), remapVertex(face.c)
        });
    return hull;
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

        // Translate. OBJ coordinates from ParaView are already in VTK world space.
        v.x = rx2 + pos.x;
        v.y = ry2 + pos.y;
        v.z = rz2 + pos.z;
    }
}