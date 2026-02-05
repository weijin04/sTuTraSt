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
                
                int nx_coord = x + dx;
                int ny_coord = y + dy;
                int nz_coord = z + dz;
                
                // Apply periodic boundaries
                if (nx_coord < 0) nx_coord += nx;
                if (nx_coord >= nx) nx_coord -= nx;
                if (ny_coord < 0) ny_coord += ny;
                if (ny_coord >= ny) ny_coord -= ny;
                if (nz_coord < 0) nz_coord += nz;
                if (nz_coord >= nz) nz_coord -= nz;
                
                neighbors.push_back(Coord3D(nx_coord, ny_coord, nz_coord));
            }
        }
    }
    
    return neighbors;
}
