cmake_minimum_required(VERSION 3.14)
if(NOT ZITI_SDK_C_BRANCH)
    #allow using a different branch of the CSDK easily
    set(ZITI_SDK_C_BRANCH "0.26.29")
endif()

# if TUNNEL_SDK_ONLY then don't descend into programs/ziti-edge-tunnel
option(TUNNEL_SDK_ONLY "build only ziti-tunnel-sdk (without ziti)" OFF)
message("tunnel only = ${TUNNEL_SDK_ONLY}")

find_package(Git)
add_custom_target(version
    if(GIT_FOUND)
        # Generate a git-describe version string from Git repository tags
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --dirty=-local --match "v*"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
            RESULT_VARIABLE GIT_DESCRIBE_ERROR_CODE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        if(NOT GIT_DESCRIBE_ERROR_CODE)
            set(GIT_VERSION ${GIT_DESCRIBE_VERSION})
        endif()
    endif()
    # Final fallback: Just use a bogus version string that is semantically older
    # than anything else and spit out a warning to the developer.
    if(DEFINED GIT_VERSION)
    else()
        set(GIT_VERSION v0.0.0-unknown)
        message(WARNING "Failed to determine GIT_VERSION from Git tags. Using default version \"${GIT_VERSION}\".")
    endif()
    string(REGEX MATCH "([0-9]+\\.[0-9]+\\.[0-9]+)" PROJECT_SEMVER "${GIT_VERSION}")
)
add_dependencies(package version)

project(ziti-tunnel-sdk-c
        VERSION "${PROJECT_SEMVER}"
        LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)

if (MSVC)
    add_compile_options(-Zi)
else()
    add_compile_options(-g)
endif()

add_custom_target(bundle
        COMMENT "creates publishable artifacts")

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bundle)

macro(BUNDLE_COMP comp)
    add_custom_target(${comp}-bundle
        BYPRODUCTS ${CMAKE_BINARY_DIR}/bundle/${comp}-${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}.zip
        DEPENDS ${comp}
        COMMAND 7z a ${CMAKE_BINARY_DIR}/bundle/${comp}-${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}.zip ${comp}${CMAKE_EXECUTABLE_SUFFIX} ${${comp}_BUNDLE_COMPS})
    add_dependencies(bundle ${comp}-bundle)
endmacro()

message("project version: ${PROJECT_VERSION} (git version ${GIT_VERSION})")
message("cross-compiling: ${CMAKE_CROSSCOMPILING}")

link_directories(${CMAKE_BINARY_DIR}/lib)

add_subdirectory(deps)

add_subdirectory(lib/ziti-tunnel)

if(NOT TUNNEL_SDK_ONLY)
add_subdirectory(lib/ziti-tunnel-cbs)
add_subdirectory(programs)
endif()

if (WIN32)
set(CMAKE_INSTALL_LIBDIR lib)
set(CMAKE_INSTALL_INCLUDEDIR include)
endif()

# lwip macro defaults. override on command line or in parent cmakelists.
set(LWIP_MEM_SIZE 1048576 CACHE STRING "LWIP MEM_SIZE option")
set(LWIP_PBUF_POOL_SIZE 1024 CACHE STRING "LWIP PBUF_POOL_SIZE option")
set(UDP_MAX_CONNECTIONS 512 CACHE STRING "LWIP MEMP_NUM_UDP_PCB option")
set(TCP_MAX_QUEUED_SEGMENTS 2048 CACHE STRING "LWIP MEMP_NUM_TCP_SEG option")
set(TCP_MAX_CONNECTIONS 512 CACHE STRING "LWIP MEMP_NUM_TCP_PCB option")

target_compile_definitions(lwipcore
    PUBLIC MEM_SIZE=${LWIP_MEM_SIZE}
    PUBLIC PBUF_POOL_SIZE=${LWIP_PBUF_POOL_SIZE}
    PUBLIC MEMP_NUM_TCP_PCB=${TCP_MAX_CONNECTIONS}
    PUBLIC MEMP_NUM_TCP_SEG=${TCP_MAX_QUEUED_SEGMENTS}
    PUBLIC MEMP_NUM_UDP_PCB=${UDP_MAX_CONNECTIONS}
)

if (WIN32)
install(TARGETS lwipwin32arch
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
endif()
