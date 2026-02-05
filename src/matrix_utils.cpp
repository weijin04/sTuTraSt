#include "matrix_utils.h"
#include <iostream>
#include <limits>

std::vector<std::array<int, 3>> MatrixUtils::rref(const std::vector<std::array<int, 3>>& matrix) {
    if (matrix.empty()) {
        return {};
    }
    
    // Convert to double for RREF computation
    // Note: We take absolute values here to match Octave's rref(abs(tunnel_list))
    // This focuses on tunnel connectivity regardless of direction
    std::vector<std::array<double, 3>> mat;
    for (const auto& row : matrix) {
        mat.push_back({static_cast<double>(std::abs(row[0])), 
                       static_cast<double>(std::abs(row[1])), 
                       static_cast<double>(std::abs(row[2]))});
    }
    
    const double eps = 1e-10;
    int rows = mat.size();
    int cols = 3;
    int lead = 0;
    
    // Forward elimination to row echelon form
    for (int r = 0; r < rows && lead < cols; r++) {
        // Find pivot
        int i = find_pivot(mat, lead, r);
        if (i == -1) {
            lead++;
            r--;  // Retry with next column
            continue;
        }
        
        // Swap rows if needed
        if (i != r) {
            swap_rows(mat, i, r);
        }
        
        // Scale pivot row to make leading coefficient 1
        double pivot = mat[r][lead];
        if (std::abs(pivot) > eps) {
            for (int j = 0; j < cols; j++) {
                mat[r][j] /= pivot;
            }
        }
        
        // Eliminate all other entries in this column
        for (int i = 0; i < rows; i++) {
            if (i != r) {
                double factor = mat[i][lead];
                for (int j = 0; j < cols; j++) {
                    mat[i][j] -= factor * mat[r][j];
                }
            }
        }
        
        lead++;
    }
    
    // Convert back to integers and remove zero rows
    std::vector<std::array<int, 3>> result;
    for (const auto& row : mat) {
        // Check if row is non-zero
        bool is_zero = true;
        for (int j = 0; j < cols; j++) {
            if (std::abs(row[j]) > eps) {
                is_zero = false;
                break;
            }
        }
        
        if (!is_zero) {
            // Round to nearest integer
            std::array<int, 3> int_row;
            for (int j = 0; j < cols; j++) {
                int_row[j] = static_cast<int>(std::round(row[j]));
            }
            result.push_back(int_row);
        }
    }
    
    return result;
}

int MatrixUtils::find_pivot(const std::vector<std::array<double, 3>>& mat, int col, int start_row) {
    const double eps = 1e-10;
    int rows = mat.size();
    
    for (int i = start_row; i < rows; i++) {
        if (std::abs(mat[i][col]) > eps) {
            return i;
        }
    }
    
    return -1;
}

void MatrixUtils::swap_rows(std::vector<std::array<double, 3>>& mat, int row1, int row2) {
    std::array<double, 3> temp = mat[row1];
    mat[row1] = mat[row2];
    mat[row2] = temp;
}
