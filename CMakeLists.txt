cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are rejected. From the repository root run: cmake --preset mingw64-dev")
endif()

project(ERNCStudio
    VERSION 0.1.0
    DESCRIPTION "ERNC Studio industrial engineering workstation"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(ToolchainGate)
ernc_require_mingw64_gcc()

option(ERNC_BUILD_UI "Build the Qt Widgets desktop entry" ON)
option(ERNC_BUILD_TESTS "Enable CTest and add tests/ when that CMakeLists exists" ON)
option(ERNC_BUILD_COMM_LAB "Add tools/ernc-comm-lab when that CMakeLists exists" OFF)

add_subdirectory(src/domain)
add_subdirectory(src/infrastructure/protocol)
add_subdirectory(src/infrastructure/serial)
add_subdirectory(src/infrastructure/filesystem)
add_subdirectory(src/infrastructure/persistence)
add_subdirectory(src/application/device)
add_subdirectory(src/application/parameter)
add_subdirectory(src/application/project)
add_subdirectory(src/application/experiment)
add_subdirectory(simulator)

if(ERNC_BUILD_UI)
    add_subdirectory(app)
endif()

if(ERNC_BUILD_COMM_LAB)
    if(EXISTS "${CMAKE_SOURCE_DIR}/tools/ernc-comm-lab/CMakeLists.txt")
        add_subdirectory(tools/ernc-comm-lab)
    else()
        message(STATUS
            "ERNC: ERNC_BUILD_COMM_LAB is ON and tools/ernc-comm-lab is not in this phase.")
    endif()
endif()

if(ERNC_BUILD_TESTS)
    enable_testing()
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    else()
        message(STATUS
            "ERNC: CTest is enabled and tests/CMakeLists.txt is not present yet.")
    endif()
endif()
