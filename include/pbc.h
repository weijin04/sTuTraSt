#ifndef PBC_H
#define PBC_H

#include "types.h"
#include <array>

// Periodic boundary conditions utilities
class PBC {
public:
    // Get neighbor coordinates with periodic boundary conditions
    // Returns ip, im, jp, jm, kp, km and cross vectors
    static void apply(int i, int j, int k, int nx, int ny, int nz,
                     int& ip, int& im, int& jp, int& jm, int& kp, int& km,
                     CrossVector& cross_vec);
    
    // Get all 26 neighbor coordinates
    static std::vector<Coord3D> get_neighbors(int x, int y, int z, 
                                             int nx, int ny, int nz);
};

#endif // PBC_H
