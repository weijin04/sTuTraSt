#include "pbc.h"

void PBC::apply(int i, int j, int k, int nx, int ny, int nz,
               int& ip, int& im, int& jp, int& jm, int& kp, int& km,
               CrossVector& cross_vec) {
    // Initialize cross vector (MATLAB indices: 0=x, 1=ip, 2=im, 3=y, 4=jp, 5=jm, 6=z, 7=kp, 8=km)
    for (int idx = 0; idx < 9; idx++) {
        cross_vec.vals[idx] = 0;
    }
    cross_vec.i = 0;
    cross_vec.j = 0;
    cross_vec.k = 0;
    
    // X direction (MATLAB 1-indexed: i==x means i==nx in C++)
    if (i == nx - 1) {  // At boundary x=nx-1, wrapping to 0
        im = i - 1;
        ip = 0;
        cross_vec.vals[1] = 1;  // ip crosses boundary
    } else if (i == 0) {  // At boundary x=0, wrapping to nx-1
        im = nx - 1;
        ip = i + 1;
        cross_vec.vals[2] = -1;  // im crosses boundary
    } else {
        ip = i + 1;
        im = i - 1;
    }
    
    // Y direction
    if (j == ny - 1) {
        jm = j - 1;
        jp = 0;
        cross_vec.vals[4] = 1;  // jp crosses boundary
    } else if (j == 0) {
        jm = ny - 1;
        jp = j + 1;
        cross_vec.vals[5] = -1;  // jm crosses boundary
    } else {
        jp = j + 1;
        jm = j - 1;
    }
    
    // Z direction
    if (k == nz - 1) {
        km = k - 1;
        kp = 0;
        cross_vec.vals[7] = 1;  // kp crosses boundary
    } else if (k == 0) {
        km = nz - 1;
        kp = k + 1;
        cross_vec.vals[8] = -1;  // km crosses boundary
    } else {
        kp = k + 1;
        km = k - 1;
    }
    
    // Set simple i,j,k for backwards compatibility
    if (cross_vec.vals[1] != 0 || cross_vec.vals[2] != 0) cross_vec.i = cross_vec.vals[1] + cross_vec.vals[2];
    if (cross_vec.vals[4] != 0 || cross_vec.vals[5] != 0) cross_vec.j = cross_vec.vals[4] + cross_vec.vals[5];
    if (cross_vec.vals[7] != 0 || cross_vec.vals[8] != 0) cross_vec.k = cross_vec.vals[7] + cross_vec.vals[8];
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
