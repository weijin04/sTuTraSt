#include "pbc.h"

void PBC::apply(int i, int j, int k, int nx, int ny, int nz,
               int& ip, int& im, int& jp, int& jm, int& kp, int& km,
               CrossVector& cross_vec) {
    // Initialize cross vector
    cross_vec.i = 0;
    cross_vec.j = 0;
    cross_vec.k = 0;
    
    // X direction
    ip = i + 1;
    im = i - 1;
    if (ip >= nx) {
        ip = 0;
        cross_vec.i = 1;
    }
    if (im < 0) {
        im = nx - 1;
        cross_vec.i = -1;
    }
    
    // Y direction
    jp = j + 1;
    jm = j - 1;
    if (jp >= ny) {
        jp = 0;
        cross_vec.j = 1;
    }
    if (jm < 0) {
        jm = ny - 1;
        cross_vec.j = -1;
    }
    
    // Z direction
    kp = k + 1;
    km = k - 1;
    if (kp >= nz) {
        kp = 0;
        cross_vec.k = 1;
    }
    if (km < 0) {
        km = nz - 1;
        cross_vec.k = -1;
    }
}

std::vector<Coord3D> PBC::get_neighbors(int x, int y, int z, 
                                        int nx, int ny, int nz) {
    std::vector<Coord3D> neighbors;
    
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dz = -1; dz <= 1; dz++) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                
                int neighbor_x = x + dx;
                int neighbor_y = y + dy;
                int neighbor_z = z + dz;
                
                // Apply periodic boundaries
                if (neighbor_x < 0) neighbor_x += nx;
                if (neighbor_x >= nx) neighbor_x -= nx;
                if (neighbor_y < 0) neighbor_y += ny;
                if (neighbor_y >= ny) neighbor_y -= ny;
                if (neighbor_z < 0) neighbor_z += nz;
                if (neighbor_z >= nz) neighbor_z -= nz;
                
                neighbors.push_back(Coord3D(neighbor_x, neighbor_y, neighbor_z));
            }
        }
    }
    
    return neighbors;
}
