#include "vtkExport.h"
#include <fstream>
#include <stdexcept>

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

    int totalCells = dims.x * dims.y * dims.z;
    uint32_t dataBytes = totalCells * sizeof(double);

    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"ImageData\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    f << "  <ImageData WholeExtent=\""
      << "0 " << dims.x << " "
      << "0 " << dims.y << " "
      << "0 " << dims.z << "\" "
      << "Origin=\""
      << origin.x << " " << origin.y << " " << origin.z << "\" "
      << "Spacing=\""
      << cellSize.x << " " << cellSize.y << " " << cellSize.z << "\">\n";
    f << "    <Piece Extent=\""
      << "0 " << dims.x << " "
      << "0 " << dims.y << " "
      << "0 " << dims.z << "\">\n";
    
    f << "      <CellData Scalars=\"" << fieldName << "\">\n";
    f << "        <DataArray type=\"Float64\" Name=\"" << fieldName << "\""
      << " format=\"appended\" offset=\"0\"/>\n";
    f << "      </CellData>\n";
    f << "    </Piece>\n";
    f << "  </ImageData>\n";
    f << "  <AppendedData encoding=\"raw\">\n_";

    // Byte count -> data
    f.write(reinterpret_cast<const char*>(&dataBytes), sizeof(float));
    f.write(reinterpret_cast<const char*>(grid), dataBytes);

    f << "  </AppendedData>\n";
    f << "</VTKFile>\n";
}