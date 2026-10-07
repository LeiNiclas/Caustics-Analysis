#include "vtkExport.h"
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

#define VERTICES_PER_CELL 8
#define CELL_TYPE 12

void exportVTI(
    const std::string& filename,
    const double* grid,
    owl::vec3i dims,
    owl::vec3f origin,
    owl::vec3f cellSize,
    const std::string& fieldName
)
{
    std::ofstream f(filename, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("Could not open VTK file: " + filename);

    int nx = dims.x, ny = dims.y, nz = dims.z;
    int totalCells  = nx * ny * nz;
    int totalPoints = (nx+1) * (ny+1) * (nz+1);

    std::vector<float> gridF(totalCells);
    for (int i = 0; i < totalCells; i++)
      gridF[i] = static_cast<float>(grid[i]);

    // Points
    uint32_t pointBytes = totalPoints * 3 * sizeof(float);
    std::vector<float> points;
    points.reserve(totalPoints * 3);
    for (int k = 0; k <= nz; k++)
    for (int j = 0; j <= ny; j++)
    for (int i = 0; i <= nx; i++)
    {
        points.push_back(origin.x + i * cellSize.x);
        points.push_back(origin.y + j * cellSize.y);
        points.push_back(origin.z + k * cellSize.z);
    }

    // Connectivity
    // Explaination of data structures:
    // https://resinsight.org/reference-manual/surfaces/vtksurface/index.html
    uint32_t connBytes = totalCells * VERTICES_PER_CELL * sizeof(int32_t);
    std::vector<int32_t> conn;
    conn.reserve(totalCells * VERTICES_PER_CELL);
    auto idx = [&](int i, int j, int k) {
        return i + j*(nx+1) + k*(nx+1)*(ny+1);
    };
    for (int k = 0; k < nz; k++)
    for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++)
    {
        conn.push_back(idx(i,   j,   k  ));
        conn.push_back(idx(i+1, j,   k  ));
        conn.push_back(idx(i+1, j+1, k  ));
        conn.push_back(idx(i,   j+1, k  ));
        conn.push_back(idx(i,   j,   k+1));
        conn.push_back(idx(i+1, j,   k+1));
        conn.push_back(idx(i+1, j+1, k+1));
        conn.push_back(idx(i,   j+1, k+1));
    }

    // Offsets
    uint32_t offsetBytes = totalCells * sizeof(int32_t);
    std::vector<int32_t> offsets;
    offsets.reserve(totalCells);

    for (int c = 1; c <= totalCells; c++)
        offsets.push_back(c * VERTICES_PER_CELL); //sparse graph representation. 8 edges per cell

    // Celltypes
    uint32_t typeBytes = totalCells * sizeof(uint8_t);
    std::vector<uint8_t> types(totalCells, CELL_TYPE); // https://github.com/Kitware/VTK/blob/master/Common/DataModel/vtkCellType.h

    // Data
    uint32_t fieldBytes = totalCells * sizeof(float);

    // Appended-Data Offsets
    uint32_t off_points  = 0;
    uint32_t off_conn    = off_points  + sizeof(uint32_t) + pointBytes;
    uint32_t off_offsets = off_conn    + sizeof(uint32_t) + connBytes;
    uint32_t off_types   = off_offsets + sizeof(uint32_t) + offsetBytes;
    uint32_t off_field   = off_types   + sizeof(uint32_t) + typeBytes;

    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    f << "  <UnstructuredGrid>\n";
    f << "    <Piece NumberOfPoints=\"" << totalPoints
      << "\" NumberOfCells=\"" << totalCells << "\">\n";

    f << "      <Points>\n";
    f << "        <DataArray type=\"Float32\" NumberOfComponents=\"3\""
      << " format=\"appended\" offset=\"" << off_points << "\"/>\n";
    f << "      </Points>\n";

    f << "      <Cells>\n";
    f << "        <DataArray type=\"Int32\" Name=\"connectivity\""
      << " format=\"appended\" offset=\"" << off_conn << "\"/>\n";
    f << "        <DataArray type=\"Int32\" Name=\"offsets\""
      << " format=\"appended\" offset=\"" << off_offsets << "\"/>\n";
    f << "        <DataArray type=\"UInt8\" Name=\"types\""
      << " format=\"appended\" offset=\"" << off_types << "\"/>\n";
    f << "      </Cells>\n";

    f << "      <CellData Scalars=\"" << fieldName << "\">\n";
    f << "        <DataArray type=\"Float32\" Name=\"" << fieldName << "\""
      << " format=\"appended\" offset=\"" << off_field << "\"/>\n";
    f << "      </CellData>\n";

    f << "    </Piece>\n";
    f << "  </UnstructuredGrid>\n";
    f << "  <AppendedData encoding=\"raw\">\n_";

    // Binary Data
    f.write(reinterpret_cast<const char*>(&pointBytes),  sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(points.data()), pointBytes);

    f.write(reinterpret_cast<const char*>(&connBytes),   sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(conn.data()),   connBytes);

    f.write(reinterpret_cast<const char*>(&offsetBytes), sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(offsets.data()), offsetBytes);

    f.write(reinterpret_cast<const char*>(&typeBytes),   sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(types.data()),  typeBytes);

    f.write(reinterpret_cast<const char*>(&fieldBytes),  sizeof(uint32_t));
    f.write(reinterpret_cast<const char*>(gridF.data()),  fieldBytes);

    f << "\n  </AppendedData>\n";
    f << "</VTKFile>\n";
}

void exportPointsVTP(
    const std::string& filename,
    const float* xyzPoints,
    std::size_t pointCount
)
{
    if (pointCount > 0 && xyzPoints == nullptr)
        throw std::invalid_argument("Point data pointer is null.");
    if (pointCount > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        throw std::invalid_argument("Point count exceeds VTK Int32 connectivity capacity.");

    std::ofstream f(filename);
    if (!f.is_open())
        throw std::runtime_error("Could not open VTK file: " + filename);

    f << std::setprecision(std::numeric_limits<float>::max_digits10);
    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"PolyData\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    f << "  <PolyData>\n";
    f << "    <Piece NumberOfPoints=\"" << pointCount
        << "\" NumberOfVerts=\"" << pointCount
        << "\" NumberOfLines=\"0\" NumberOfStrips=\"0\" NumberOfPolys=\"0\">\n";

    f << "      <Points>\n";
    f << "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (std::size_t i = 0; i < pointCount; ++i)
        f << "          " << xyzPoints[3 * i] << ' '
            << xyzPoints[3 * i + 1] << ' ' << xyzPoints[3 * i + 2] << '\n';
    f << "        </DataArray>\n";
    f << "      </Points>\n";

    f << "      <Verts>\n";
    f << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
    for (std::size_t i = 0; i < pointCount; ++i)
        f << "          " << i << '\n';
    f << "        </DataArray>\n";
    f << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    for (std::size_t i = 1; i <= pointCount; ++i)
        f << "          " << i << '\n';
    f << "        </DataArray>\n";
    f << "      </Verts>\n";

    f << "    </Piece>\n";
    f << "  </PolyData>\n";
    f << "</VTKFile>\n";

    if (!f)
        throw std::runtime_error("Failed while writing VTK file: " + filename);
}

void exportImplicitSurfaceVTP(
    const std::string& filename,
    ImplicitType type,
    float param0,
    float param1,
    owl::vec3f origin,
    owl::vec3f size,
    int resolution
)
{
    if (resolution < 8 || resolution > 256)
        throw std::invalid_argument("Implicit surface preview resolution must be between 8 and 256.");
    if (!(size.x > 0.0f && size.y > 0.0f && size.z > 0.0f))
        throw std::invalid_argument("Implicit surface preview bounds must have positive size.");
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)
        || !std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z)
        || !std::isfinite(param0) || !std::isfinite(param1))
        throw std::invalid_argument("Implicit surface preview bounds and parameters must be finite.");

    const int sampleCount = resolution + 1;
    const std::size_t totalSamples = static_cast<std::size_t>(sampleCount) * sampleCount * sampleCount;
    const owl::vec3f step = size / static_cast<float>(resolution);
    std::vector<float> values(totalSamples);
    auto sampleIndex = [sampleCount](int x, int y, int z) {
        return static_cast<std::size_t>(x)
             + static_cast<std::size_t>(sampleCount) * y
             + static_cast<std::size_t>(sampleCount) * sampleCount * z;
    };
    auto samplePosition = [&](int x, int y, int z) {
        return origin + owl::vec3f(
            static_cast<float>(x) * step.x,
            static_cast<float>(y) * step.y,
            static_cast<float>(z) * step.z);
    };
    auto positionForSample = [&](int index) {
        const int x = index % sampleCount;
        const int y = (index / sampleCount) % sampleCount;
        const int z = index / (sampleCount * sampleCount);
        return samplePosition(x, y, z);
    };

    for (int z = 0; z < sampleCount; ++z)
    for (int y = 0; y < sampleCount; ++y)
    for (int x = 0; x < sampleCount; ++x)
        values[sampleIndex(x, y, z)] =
            evalImplicitSurface(type, samplePosition(x, y, z), param0, param1);

    constexpr int cornerX[8] = { 0, 1, 1, 0, 0, 1, 1, 0 };
    constexpr int cornerY[8] = { 0, 0, 1, 1, 0, 0, 1, 1 };
    constexpr int cornerZ[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };
    constexpr int tetrahedra[6][4] = {
        { 0, 5, 1, 6 }, { 0, 1, 2, 6 }, { 0, 2, 3, 6 },
        { 0, 3, 7, 6 }, { 0, 7, 4, 6 }, { 0, 4, 5, 6 }
    };
    std::vector<owl::vec3f> vertices;

    auto interpolate = [&](int a, int b) {
        const float va = values[static_cast<std::size_t>(a)];
        const float vb = values[static_cast<std::size_t>(b)];
        const float t = std::max(0.0f, std::min(1.0f, va / (va - vb)));
        const owl::vec3f pa = positionForSample(a);
        return pa + t * (positionForSample(b) - pa);
    };
    auto addTriangle = [&](owl::vec3f a, owl::vec3f b, owl::vec3f c, owl::vec3f outward) {
        const owl::vec3f ab = b - a;
        const owl::vec3f ac = c - a;
        owl::vec3f normal = owl::cross(ab, ac);
        if (normal.x * normal.x + normal.y * normal.y + normal.z * normal.z <= 1.0e-20f)
            return;
        if (normal.x * outward.x + normal.y * outward.y + normal.z * outward.z < 0.0f)
            std::swap(b, c);
        vertices.push_back(a);
        vertices.push_back(b);
        vertices.push_back(c);
    };

    for (int z = 0; z < resolution; ++z)
    for (int y = 0; y < resolution; ++y)
    for (int x = 0; x < resolution; ++x)
    {
        std::array<int, 8> cubeIndices;
        for (int corner = 0; corner < 8; ++corner)
        {
            cubeIndices[corner] = static_cast<int>(sampleIndex(
                x + cornerX[corner], y + cornerY[corner], z + cornerZ[corner]));
        }

        for (const auto& tetra : tetrahedra)
        {
            int inside[4], outside[4];
            int insideCount = 0, outsideCount = 0;
            for (int vertex = 0; vertex < 4; ++vertex)
            {
                const int index = cubeIndices[tetra[vertex]];
                if (values[static_cast<std::size_t>(index)] < 0.0f)
                    inside[insideCount++] = index;
                else
                    outside[outsideCount++] = index;
            }

            if (insideCount == 0 || insideCount == 4)
                continue;

            if (insideCount == 1 || insideCount == 3)
            {
                const int* singleSide = insideCount == 1 ? inside : outside;
                const int* otherSide = insideCount == 1 ? outside : inside;
                owl::vec3f outward(0.0f);
                for (int i = 0; i < 3; ++i)
                    outward += positionForSample(otherSide[i]);
                outward = (insideCount == 1)
                    ? outward / 3.0f - positionForSample(singleSide[0])
                    : positionForSample(singleSide[0]) - outward / 3.0f;
                addTriangle(
                    interpolate(singleSide[0], otherSide[0]),
                    interpolate(singleSide[0], otherSide[1]),
                    interpolate(singleSide[0], otherSide[2]),
                    outward);
            }
            else
            {
                const owl::vec3f outward =
                    0.5f * (positionForSample(outside[0]) + positionForSample(outside[1])
                          - positionForSample(inside[0]) - positionForSample(inside[1]));
                const owl::vec3f ac = interpolate(inside[0], outside[0]);
                const owl::vec3f ad = interpolate(inside[0], outside[1]);
                const owl::vec3f bc = interpolate(inside[1], outside[0]);
                const owl::vec3f bd = interpolate(inside[1], outside[1]);
                addTriangle(ac, ad, bd, outward);
                addTriangle(ac, bd, bc, outward);
            }
        }
    }

    if (vertices.empty())
        throw std::runtime_error("No implicit surface intersects the preview bounds.");
    if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        throw std::runtime_error("Implicit surface preview exceeds VTK Int32 connectivity capacity.");

    std::ofstream f(filename);
    if (!f.is_open())
        throw std::runtime_error("Could not open VTK file: " + filename);

    const std::size_t triangleCount = vertices.size() / 3;
    f << std::setprecision(std::numeric_limits<float>::max_digits10);
    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"PolyData\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    f << "  <PolyData>\n";
    f << "    <Piece NumberOfPoints=\"" << vertices.size()
      << "\" NumberOfVerts=\"0\" NumberOfLines=\"0\" NumberOfStrips=\"0\""
      << " NumberOfPolys=\"" << triangleCount << "\">\n";
    f << "      <Points>\n";
    f << "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (const owl::vec3f& point : vertices)
        f << "          " << point.x << ' ' << point.y << ' ' << point.z << '\n';
    f << "        </DataArray>\n";
    f << "      </Points>\n";
    f << "      <Polys>\n";
    f << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
    for (std::size_t i = 0; i < vertices.size(); ++i)
        f << "          " << i << '\n';
    f << "        </DataArray>\n";
    f << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    for (std::size_t i = 1; i <= triangleCount; ++i)
        f << "          " << 3 * i << '\n';
    f << "        </DataArray>\n";
    f << "      </Polys>\n";
    f << "    </Piece>\n";
    f << "  </PolyData>\n";
    f << "</VTKFile>\n";

    if (!f)
        throw std::runtime_error("Failed while writing VTK file: " + filename);
}