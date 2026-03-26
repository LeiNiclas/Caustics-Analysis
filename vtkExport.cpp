#include "vtkExport.h"
#include <fstream>
#include <stdexcept>

void exportVTI(
    const std::string& filename,
    const uint32_t* grid,
    owl::vec3i dims,
    owl::vec3f origin,
    owl::vec3f cellSize,
    const std::string& fieldName
)
{
    std::ofstream f(filename);

    if (!f.is_open())
        throw std::runtime_error("Could not open VTK file: " + filename);
    
    int totalCells = dims.x * dims.y * dims.z;

    // VTK ImageData XML Header
    // "Extent" from 0 to dims (exclusive) -> dims cells per axis
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
    
    // CellData: counts are in the cells
    f << "      <CellData Scalars=\"" << fieldName << "\">\n";
    f << "        <DataArray type=\"UInt32\" Name=\"" << fieldName << "\" format=\"ascii\">\n";

    // Write Values. traverse order: X -> Y -> Z
    for (int z = 0; z < dims.z; ++z)
    {
        for (int y = 0; y < dims.y; ++y)
        {
            for (int x = 0; x < dims.x; ++x)
            {
                int idx = x + dims.x * y + dims.x * dims.y * z;
                f << grid[idx];
                
                if (x < dims.x - 1) f << ' ';
            }

            f << '\n';
        }
    }


    f << "        </DataArray>\n";
    f << "      </CellData>\n";
    f << "    </Piece>\n";
    f << "  </ImageData>\n";
    f << "</VTKFile>\n";
}