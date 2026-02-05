#ifndef MATRIX_UTILS_H
#define MATRIX_UTILS_H

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

class MatrixUtils {
public:
    // Compute reduced row echelon form (RREF) of a matrix
    // Input: rows of 3D vectors [i, j, k]
    // Output: RREF form with independent basis vectors
    static std::vector<std::array<int, 3>> rref(const std::vector<std::array<int, 3>>& matrix);
    
private:
    // Helper function to find pivot row
    static int find_pivot(const std::vector<std::array<double, 3>>& mat, int col, int start_row);
    
    // Helper function to swap rows
    static void swap_rows(std::vector<std::array<double, 3>>& mat, int row1, int row2);
};

#endif // MATRIX_UTILS_H
