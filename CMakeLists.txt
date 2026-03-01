cmake_minimum_required(VERSION 3.10)
project(TuTraSt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -Wextra")

# Source files
set(SOURCES
    src/main.cpp
    src/cube_parser.cpp
    src/input_parser.cpp
    src/grid.cpp
    src/cluster.cpp
    src/transition_state.cpp
    src/tunnel.cpp
    src/pbc.cpp
    src/output_writer.cpp
    src/kmc.cpp
    src/matrix_utils.cpp
)

# Header files
set(HEADERS
    include/cube_parser.h
    include/input_parser.h
    include/grid.h
    include/cluster.h
    include/transition_state.h
    include/tunnel.h
    include/pbc.h
    include/output_writer.h
    include/kmc.h
    include/types.h
    include/matrix_utils.h
)

# Create executable
add_executable(tutrast ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(tutrast PRIVATE include)

# Link math library
target_link_libraries(tutrast m)

# Optional OpenMP for accelerated grid generation
find_package(OpenMP)

# v10-aligned PES grid generator (C++)
add_executable(generate_pes_grid_v10_cpp src/pes_grid_v10_cpp.cpp)
target_include_directories(generate_pes_grid_v10_cpp PRIVATE include)
target_link_libraries(generate_pes_grid_v10_cpp m)
if(OpenMP_CXX_FOUND)
    target_link_libraries(generate_pes_grid_v10_cpp OpenMP::OpenMP_CXX)
endif()

# Installation
install(TARGETS tutrast DESTINATION bin)
