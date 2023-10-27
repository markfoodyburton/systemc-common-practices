cmake_minimum_required(VERSION 3.14 FATAL_ERROR)
project(scp VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 14 CACHE STRING "C++ standard to build all targets.")

set(GITHUB "https://github.com/" CACHE STRING "github base url")

include(FetchContent)
include(CTest)

FetchContent_Declare(
  cpm-cmake
  GIT_REPOSITORY ${GITHUB}cpm-cmake/CPM.cmake.git
  GIT_SHALLOW True
  GIT_TAG v0.31.1
)

FetchContent_MakeAvailable(cpm-cmake)
include(${cpm-cmake_SOURCE_DIR}/cmake/CPM.cmake)

cpmaddpackage("${GITHUB}TheLartians/PackageProject.cmake.git@1.4.1")
# check if caller has already setup SystemC based on e.g. SYSTEMC_HOME

if(DEFINED ENV{SYSTEMC_HOME})
  cpmaddpackage(
    NAME SystemCLanguage
    SOURCE_DIR ENV{SYSTEMC_HOME}
  )
else ()
  cpmaddpackage(
    NAME SystemCLanguage
    GIT_REPOSITORY  ${GITHUB}accellera-official/systemc.git
    GIT_SHALLOW True
    GIT_TAG main
  )
endif()

cpmaddpackage(
  NAME
  RapidJSON
  GIT_REPOSITORY
  https://github.com/Tencent/rapidjson
  GIT_TAG
  e0f68a435610e70ab5af44fc6a90523d69b210b3
  GIT_SHALLOW
  FALSE
  OPTIONS
  "RAPIDJSON_BUILD_TESTS OFF"
  "RAPIDJSON_BUILD_DOC OFF"
  "RAPIDJSON_BUILD_EXAMPLES OFF")

set(RapidJSON_DIR "${RapidJSON_BINARY_DIR}")

cpmaddpackage(
  NAME SystemCCCI
  GIT_REPOSITORY  ${GITHUB}accellera-official/cci.git
  GIT_SHALLOW True
  GIT_TAG master
)

cpmaddpackage(
  NAME initiator_id
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/tlm_extensions/initiator_id
)
cpmaddpackage(
  NAME path_trace
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/tlm_extensions/path_trace
)
cpmaddpackage(
  NAME report
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/report
)

if("${PROJECT_NAME}" STREQUAL "${CMAKE_PROJECT_NAME}")
  add_subdirectory(examples)
  if(BUILD_TESTING)
    enable_testing()
  endif()
endif()
