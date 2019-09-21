# Convery (tcn@ayria.se)
cmake_minimum_required(VERSION 3.1)
project(Appcore)

# Use the latest standard at this time.
set(CMAKE_CXX_STANDARD 20)
enable_language(ASM_MASM)

# Export to the a ignored directory.
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/Bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/Bin)

# Separate release and debug-builds.
mark_as_advanced(CMAKE_IMPORT_LIBRARY_SUFFIX CMAKE_DEBUG_POSTFIX)
set(MODULENAME ${PROJECT_NAME})
set(CMAKE_DEBUG_POSTFIX "d")

# Differentiate between builds.
if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    string(APPEND MODULENAME "64")
else()
    string(APPEND MODULENAME "32")
endif()

# Per compiler instructions.
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")

    # Windows compilation.
    if(CMAKE_BUILD_TYPE MATCHES Release)
        set(EXTRA_CMPFLAGS ${EXTRA_CMPFLAGS} "/permissive- /Gy /Gw /GS- /GL /Os /Ox /Brepro")
        set(EXTRA_LNKFLAGS ${EXTRA_LNKFLAGS} "/subsystem:windows /entry:mainCRTStartup /LTCG /safeseh:no /filealign:512 /opt:ref,icf,lbr /incremental:no /Brepro /cgthreads:8")
    else()
        set(EXTRA_CMPFLAGS ${EXTRA_CMPFLAGS} "/permissive- /Gy") # /d2cgsummary
        set(EXTRA_LNKFLAGS ${EXTRA_LNKFLAGS} "/time /debug:fastlink /cgthreads:8")
    endif()

    # Force static runtime.
    foreach(flag_var
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE)
        if(${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()

    # Force warnings to level 4.
    if (CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
        string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    else()
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    endif()

    # Platform dependencies.
    set(PLATFORM_LIBS gdiplus)
else()
    # Assume GNU-GCC/CLANG.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG} -g")

    # For some reason my CMAKE fails to differentiate between x86 and x64.
    if(${CMAKE_SIZEOF_VOID_P} EQUAL 4)
        set(EXTRA_CMPFLAGS ${EXTRA_CMPFLAGS} "-m32")
        set(EXTRA_LNKFLAGS ${EXTRA_LNKFLAGS} "-m32 -static-libgcc -static-libstdc++")
    else()
        set(EXTRA_CMPFLAGS ${EXTRA_CMPFLAGS} "-m64")
        set(EXTRA_LNKFLAGS ${EXTRA_LNKFLAGS} "-m64 -static-libgcc -static-libstdc++")
    endif()

    # Platform dependencies.
    set(PLATFORM_LIBS dl pthread)
endif()

# Third-party packages.
find_package(pugixml CONFIG REQUIRED)
set(MODULE_LIBS ${MODULE_LIBS} pugixml)

# Just pull all the files from /Source
file(GLOB_RECURSE SOURCES "Source/*.cpp")
file(GLOB_RECURSE ASSEMBLY "Source/*.asm")
add_definitions(-DMODULENAME="${MODULENAME}")
include_directories("${PROJECT_SOURCE_DIR}/Source")
add_executable(${MODULENAME} ${SOURCES} ${ASSEMBLY})
set_target_properties(${MODULENAME} PROPERTIES PREFIX "")
target_link_libraries(${MODULENAME} ${PLATFORM_LIBS} ${MODULE_LIBS})
set_target_properties(${MODULENAME} PROPERTIES COMPILE_FLAGS "${EXTRA_CMPFLAGS}" LINK_FLAGS "${EXTRA_LNKFLAGS}")
