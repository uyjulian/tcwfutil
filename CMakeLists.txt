cmake_minimum_required(VERSION 3.0...3.5)
project(tcwfutil)

if(NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

set(ENABLE_EXTERNAL_LIBS OFF CACHE INTERNAL "")
set(ENABLE_EXPERIMENTAL OFF CACHE INTERNAL "")
set(BUILD_TESTING OFF CACHE INTERNAL "")
set(BUILD_REGTEST OFF CACHE INTERNAL "")
set(BUILD_PROGRAMS OFF CACHE INTERNAL "")
set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
FetchContent_Declare(
	libsndfile
	GIT_REPOSITORY https://github.com/erikd/libsndfile.git
	GIT_TAG        v1.0.29
	CMAKE_ARGS        "-Wno-dev"
)

FetchContent_MakeAvailable(libsndfile)

if(NOT libsndfile_POPULATED)
	FetchContent_Populate(libsndfile)
endif()

include_directories(${libsndfile_BINARY_DIR}/src)

if(WIN32)
	add_definitions(-static -static-libgcc)
endif()

add_executable(tcwfutil tcwfutil.c)

target_link_libraries(tcwfutil PRIVATE sndfile)
