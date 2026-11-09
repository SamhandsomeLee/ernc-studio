# Reject every compiler that is not the locked MSYS2 MINGW64 GCC.
# Presets set ERNC_MINGW64_ROOT and the compiler paths before project().

function(ernc_require_mingw64_gcc)
    if(NOT DEFINED ERNC_MINGW64_ROOT OR ERNC_MINGW64_ROOT STREQUAL "")
        message(FATAL_ERROR
            "ERNC_MINGW64_ROOT is empty. Configure with cmake --preset mingw64-dev. "
            "A bare CMake run must not fall back to MSVC.")
    endif()

    file(TO_CMAKE_PATH "${ERNC_MINGW64_ROOT}" ernc_root)
    set(ernc_gxx "${ernc_root}/bin/g++.exe")
    set(ernc_ninja "${ernc_root}/bin/ninja.exe")

    if(NOT EXISTS "${ernc_gxx}")
        message(FATAL_ERROR "Locked MINGW64 g++ does not exist: ${ernc_gxx}")
    endif()
    if(NOT EXISTS "${ernc_ninja}")
        message(FATAL_ERROR "Locked MINGW64 ninja does not exist: ${ernc_ninja}")
    endif()

    if(CMAKE_GENERATOR MATCHES "Visual Studio" OR CMAKE_GENERATOR MATCHES "MSBuild")
        message(FATAL_ERROR
            "Generator '${CMAKE_GENERATOR}' is rejected. Use the Ninja presets.")
    endif()
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
        message(FATAL_ERROR
            "Generator '${CMAKE_GENERATOR}' is rejected. ERNC Studio uses Ninja.")
    endif()

    if(MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR
            "MSVC is rejected. ERNC Studio links only the MSYS2 MINGW64 ABI.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR
            "C++ compiler id '${CMAKE_CXX_COMPILER_ID}' is rejected. Require GCC.")
    endif()
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "ERNC Studio requires a 64-bit compiler.")
    endif()

    if(NOT DEFINED CMAKE_C_COMPILER OR CMAKE_C_COMPILER STREQUAL "")
        message(FATAL_ERROR
            "CMAKE_C_COMPILER is empty. The mingw64 presets must lock gcc.exe.")
    endif()
    set(ernc_gcc "${ernc_root}/bin/gcc.exe")

    get_filename_component(ernc_cc_real "${CMAKE_C_COMPILER}" REALPATH)
    get_filename_component(ernc_cxx_real "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(ernc_make_real "${CMAKE_MAKE_PROGRAM}" REALPATH)
    file(TO_CMAKE_PATH "${ernc_cc_real}" ernc_cc_real)
    file(TO_CMAKE_PATH "${ernc_cxx_real}" ernc_cxx_real)
    file(TO_CMAKE_PATH "${ernc_make_real}" ernc_make_real)
    string(TOLOWER "${ernc_cc_real}" ernc_cc_lower)
    string(TOLOWER "${ernc_cxx_real}" ernc_cxx_lower)
    string(TOLOWER "${ernc_make_real}" ernc_make_lower)
    string(TOLOWER "${ernc_gcc}" ernc_gcc_lower)
    string(TOLOWER "${ernc_gxx}" ernc_gxx_lower)
    string(TOLOWER "${ernc_ninja}" ernc_ninja_lower)

    if(ernc_cxx_lower MATCHES "/ucrt64/|/clang64/|/msys64/usr/"
            OR ernc_cc_lower MATCHES "/ucrt64/|/clang64/|/msys64/usr/")
        message(FATAL_ERROR
            "Compiler path is a different MSYS2 environment: C='${ernc_cc_real}' C++='${ernc_cxx_real}'")
    endif()
    if(NOT ernc_cc_lower STREQUAL ernc_gcc_lower)
        message(FATAL_ERROR
            "C compiler '${ernc_cc_real}' is not the locked compiler '${ernc_gcc}'.")
    endif()
    if(NOT ernc_cxx_lower STREQUAL ernc_gxx_lower)
        message(FATAL_ERROR
            "C++ compiler '${ernc_cxx_real}' is not the locked compiler '${ernc_gxx}'.")
    endif()
    if(NOT ernc_make_lower STREQUAL ernc_ninja_lower)
        message(FATAL_ERROR
            "CMAKE_MAKE_PROGRAM '${ernc_make_real}' is not the locked ninja '${ernc_ninja}'.")
    endif()

    execute_process(
        COMMAND "${ernc_gxx}" -dumpmachine
        OUTPUT_VARIABLE ernc_machine
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE ernc_dump_rc
        ERROR_VARIABLE ernc_dump_err
    )
    if(NOT ernc_dump_rc EQUAL 0)
        message(FATAL_ERROR "g++ -dumpmachine failed: ${ernc_dump_err}")
    endif()
    if(NOT ernc_machine STREQUAL "x86_64-w64-mingw32")
        message(FATAL_ERROR
            "Compiler target '${ernc_machine}' is rejected. Require x86_64-w64-mingw32. "
            "ARM or UCRT toolchains cannot build this desktop program.")
    endif()

    message(STATUS "ERNC toolchain: ${ernc_cxx_real} (${ernc_machine})")
endfunction()
