cmake_minimum_required(VERSION 3.10)
project(TuTraSt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(TUTRAST_ARCH_FLAGS "-march=znver4 -mtune=znver4" CACHE STRING "Architecture tuning flags")
separate_arguments(TUTRAST_ARCH_FLAG_LIST NATIVE_COMMAND "${TUTRAST_ARCH_FLAGS}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pipe)
    foreach(release_flag -O3 -DNDEBUG)
        add_compile_options($<$<CONFIG:Release>:${release_flag}>)
    endforeach()
    foreach(arch_flag IN LISTS TUTRAST_ARCH_FLAG_LIST)
        add_compile_options($<$<CONFIG:Release>:${arch_flag}>)
    endforeach()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
endif()

find_package(OpenMP)

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

add_executable(tutrast ${SOURCES} ${HEADERS})
target_include_directories(tutrast PRIVATE include)
target_link_libraries(tutrast PRIVATE m)

add_executable(generate_pes_grid_v10_cpp src/pes_grid_v10_cpp.cpp)
target_include_directories(generate_pes_grid_v10_cpp PRIVATE include)
target_link_libraries(generate_pes_grid_v10_cpp PRIVATE m)
if(OpenMP_CXX_FOUND)
    target_link_libraries(generate_pes_grid_v10_cpp PRIVATE OpenMP::OpenMP_CXX)
endif()

install(TARGETS tutrast DESTINATION bin)
