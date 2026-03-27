cmake_minimum_required(VERSION 3.10)
project(TuTraSt VERSION 1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(TUTRAST_ARCH_FLAGS "-march=znver4 -mtune=znver4" CACHE STRING "Architecture tuning flags")
separate_arguments(TUTRAST_ARCH_FLAG_LIST NATIVE_COMMAND "${TUTRAST_ARCH_FLAGS}")
set(TUTRAST_EFFECTIVE_ARCH_FLAG_LIST "")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    include(CheckCXXCompilerFlag)
    add_compile_options(-Wall -Wextra -pipe)
    foreach(release_flag -O3 -DNDEBUG)
        add_compile_options($<$<CONFIG:Release>:${release_flag}>)
    endforeach()

    foreach(arch_flag IN LISTS TUTRAST_ARCH_FLAG_LIST)
        string(MAKE_C_IDENTIFIER "${arch_flag}" arch_flag_id)
        check_cxx_compiler_flag("${arch_flag}" "TUTRAST_SUPPORTS_${arch_flag_id}")
        if(TUTRAST_SUPPORTS_${arch_flag_id})
            list(APPEND TUTRAST_EFFECTIVE_ARCH_FLAG_LIST "${arch_flag}")
        endif()
    endforeach()

    if(NOT TUTRAST_EFFECTIVE_ARCH_FLAG_LIST AND TUTRAST_ARCH_FLAGS STREQUAL "-march=znver4 -mtune=znver4")
        foreach(arch_flag -march=native -mtune=native)
            string(MAKE_C_IDENTIFIER "${arch_flag}" arch_flag_id)
            check_cxx_compiler_flag("${arch_flag}" "TUTRAST_SUPPORTS_${arch_flag_id}")
            if(TUTRAST_SUPPORTS_${arch_flag_id})
                list(APPEND TUTRAST_EFFECTIVE_ARCH_FLAG_LIST "${arch_flag}")
            endif()
        endforeach()
    endif()

    foreach(arch_flag IN LISTS TUTRAST_EFFECTIVE_ARCH_FLAG_LIST)
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
    src/cli_options.cpp
    src/input_parser.cpp
    src/grid.cpp
    src/cluster.cpp
    src/transition_state.cpp
    src/tunnel.cpp
    src/pbc.cpp
    src/output_writer.cpp
    src/kmc.cpp
    src/kmc_state.cpp
    src/matrix_utils.cpp
)

set(HEADERS
    include/cube_parser.h
    include/cli_options.h
    include/input_parser.h
    include/grid.h
    include/cluster.h
    include/transition_state.h
    include/tunnel.h
    include/pbc.h
    include/output_writer.h
    include/kmc.h
    include/kmc_state.h
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

if(BUILD_TESTING)
    add_executable(kmc_checkpoint_tests
        tests/kmc_checkpoint_tests.cpp
        src/kmc.cpp
        src/kmc_state.cpp
    )
    target_include_directories(kmc_checkpoint_tests PRIVATE include)
    target_link_libraries(kmc_checkpoint_tests PRIVATE m)

    add_test(
        NAME kmc_checkpoint_tests
        COMMAND kmc_checkpoint_tests
    )

    add_test(
        NAME tutrast_cli_checkpoint_resume
        COMMAND /bin/bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_checkpoint_resume_test.sh
                $<TARGET_FILE:tutrast>
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/small_cli
    )
endif()
