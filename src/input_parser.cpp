#include "input_parser.h"
#include <fstream>
#include <iostream>
#include <sstream>

InputParams InputParser::parse(const std::string& filename) {
    InputParams params;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open " << filename << std::endl;
        return params;
    }
    
    std::string line;
    std::vector<double> values;
    
    // Read all numeric values
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Extract first number from line (ignore comments after %)
        std::istringstream iss(line);
        double value;
        if (iss >> value) {
            values.push_back(value);
        }
    }
    
    file.close();
    
    if (values.size() < 12) {
        std::cerr << "Error: input.param has insufficient parameters" << std::endl;
        return params;
    }
    
    int idx = 0;
    params.energy_unit = static_cast<int>(values[idx++]);
    params.n_temps = static_cast<int>(values[idx++]);
    
    for (int i = 0; i < params.n_temps; i++) {
        params.temperatures.push_back(values[idx++]);
    }
    
    params.run_kmc = (static_cast<int>(values[idx++]) == 1);
    params.plot_msd = (static_cast<int>(values[idx++]) == 1);
    params.n_steps = static_cast<int>(values[idx++]);
    params.print_every = static_cast<int>(values[idx++]);
    // MATLAB input.param order:
    // ... nsteps, print_every, nruns, n_particle, per_tunnel, ...
    params.n_runs = static_cast<int>(values[idx++]);
    params.n_particles = static_cast<int>(values[idx++]);
    params.per_tunnel = static_cast<int>(values[idx++]);
    params.particle_mass = values[idx++];
    params.energy_step = values[idx++];
    params.energy_cutoff = values[idx++];
    
    return params;
}
