#ifndef CUBE_PARSER_H
#define CUBE_PARSER_H

#include "types.h"
#include <string>
#include <vector>
#include <array>

class CubeParser {
public:
    // Parse cube file and return grid dimensions, grid size, and potential data
    static bool parse(const std::string& filename, int energy_unit,
                     std::array<int, 3>& ngrid,
                     std::array<double, 3>& grid_size,
                     std::vector<std::vector<double>>& pot_data);
    
private:
    // Convert energy units
    static double convert_energy(double energy, int from_unit);
};

#endif // CUBE_PARSER_H
