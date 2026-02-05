#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <array>
#include <string>
#include <cmath>

// 3D coordinate structure
struct Coord3D {
    int x, y, z;
    
    Coord3D() : x(0), y(0), z(0) {}
    Coord3D(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}
    
    bool operator==(const Coord3D& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

// 3D cross vector for periodic boundaries
// Stores boundary crossing information for neighbors
// Indices: 0=x, 1=ip, 2=im, 3=y, 4=jp, 5=jm, 6=z, 7=kp, 8=km
struct CrossVector {
    int i, j, k;  // For simple 3D offset
    int vals[9];  // For detailed neighbor offsets (MATLAB compatible)
    
    CrossVector() : i(0), j(0), k(0) {
        for (int idx = 0; idx < 9; idx++) vals[idx] = 0;
    }
    CrossVector(int i_, int j_, int k_) : i(i_), j(j_), k(k_) {
        for (int idx = 0; idx < 9; idx++) vals[idx] = 0;
    }
};

// Cluster point information (equivalent to MATLAB list.C(iC).info)
struct ClusterPoint {
    int x, y, z;          // Grid coordinates
    int level;            // Energy level
    int boundary;         // 1 if boundary point, 0 otherwise
    int cross_i, cross_j, cross_k;  // Periodic image offsets
    
    ClusterPoint() : x(0), y(0), z(0), level(0), boundary(0), 
                     cross_i(0), cross_j(0), cross_k(0) {}
};

// Cluster structure
struct Cluster {
    int id;
    std::vector<ClusterPoint> points;
    double min_energy;
    int tunnel_id;
    
    Cluster() : id(0), min_energy(0.0), tunnel_id(-1) {}
};

// Transition state point
struct TSPoint {
    int x, y, z;
    int level;
    double energy;
    int cluster1_id;
    int cluster2_id;
    int cross_i, cross_j, cross_k;
    
    TSPoint() : x(0), y(0), z(0), level(0), energy(0.0),
                cluster1_id(0), cluster2_id(0),
                cross_i(0), cross_j(0), cross_k(0) {}
};

// TS Group (surface between two clusters)
struct TSGroup {
    std::vector<TSPoint> points;
    int cluster1_id;
    int cluster2_id;
    double min_energy;
    Coord3D min_coord;
    
    TSGroup() : cluster1_id(0), cluster2_id(0), min_energy(0.0) {}
};

// Tunnel structure
struct Tunnel {
    int id;
    std::array<int, 3> dimensions;  // Which dimensions tunnel exists in
    std::vector<int> cluster_ids;
    std::vector<int> tsgroup_ids;
    double min_energy;
    
    Tunnel() : id(0), min_energy(0.0) {
        dimensions = {0, 0, 0};
    }
};

// Process structure (basis transition)
struct Process {
    int from_basis;
    int to_basis;
    double rate;
    CrossVector cross;
    CrossVector ts_cross;
    int tunnel_id;
    int tsgroup_id;
    
    Process() : from_basis(0), to_basis(0), rate(0.0), tunnel_id(0), tsgroup_id(0) {}
};

// Input parameters
struct InputParams {
    int energy_unit;        // 1=kJ/mol, 2=kcal/mol, 3=Ry, 4=eV, 5=Hartree
    int n_temps;
    std::vector<double> temperatures;
    bool run_kmc;
    bool plot_msd;
    int n_steps;
    int print_every;
    int n_runs;
    int n_particles;
    int per_tunnel;
    double particle_mass;
    double energy_step;
    double energy_cutoff;
    
    InputParams() : energy_unit(1), n_temps(1), run_kmc(false), plot_msd(false),
                    n_steps(0), print_every(0), n_runs(0), n_particles(1),
                    per_tunnel(1), particle_mass(0.0), energy_step(1.0), 
                    energy_cutoff(100.0) {}
};

// Constants
const double R_GAS = 8.3144621;  // Gas constant in J/(mol·K) - note: divide by 1000 for kJ/(mol·K)

#endif // TYPES_H
