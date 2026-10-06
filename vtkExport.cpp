#include "vtkExport.h"
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <vector>

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