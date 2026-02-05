cmake_minimum_required(VERSION 3.22)

project(orbit_mapper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(ORBIT_MAPPER_ENABLE_SGP4 "Enable SGP4 propagation support (FetchContent)" ON)

set(ORBIT_MAPPER_QT_VERSION "6" CACHE STRING "Qt major version to use (6 or 5)")
set_property(CACHE ORBIT_MAPPER_QT_VERSION PROPERTY STRINGS 6 5)

if (ORBIT_MAPPER_QT_VERSION STREQUAL "6")
  find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
  qt_standard_project_setup()
elseif(ORBIT_MAPPER_QT_VERSION STREQUAL "5")
  find_package(Qt5 REQUIRED COMPONENTS Widgets OpenGL)
else()
  message(FATAL_ERROR "ORBIT_MAPPER_QT_VERSION must be '6' or '5'")
endif()

add_executable(orbit_mapper
  src/main.cpp
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/gl/OrbitGlWidget.cpp
  src/gl/OrbitGlWidget.h
  src/orbit/OrbitalElements.h
  src/orbit/Kepler.cpp
  src/orbit/Kepler.h
  src/orbit/OrbitSampler.cpp
  src/orbit/OrbitSampler.h
  src/orbit/Propagator.h
  src/orbit/Sgp4Propagator.cpp
  src/orbit/Sgp4Propagator.h
)

target_include_directories(orbit_mapper PRIVATE src)

target_link_libraries(orbit_mapper PRIVATE
  $<$<STREQUAL:${ORBIT_MAPPER_QT_VERSION},6>:Qt6::Widgets>
  $<$<STREQUAL:${ORBIT_MAPPER_QT_VERSION},6>:Qt6::OpenGL>
  $<$<STREQUAL:${ORBIT_MAPPER_QT_VERSION},6>:Qt6::OpenGLWidgets>
  $<$<STREQUAL:${ORBIT_MAPPER_QT_VERSION},5>:Qt5::Widgets>
  $<$<STREQUAL:${ORBIT_MAPPER_QT_VERSION},5>:Qt5::OpenGL>
)

if (ORBIT_MAPPER_ENABLE_SGP4)
  include(FetchContent)

  # NOTE: This is a placeholder dependency hook.
  # Pick a specific SGP4 implementation and pin it to a commit/tag.
  # Example candidates (you choose):
  # - https://github.com/dnwrnr/sgp4 (C++)
  # - https://github.com/aholinch/sgp4 (C)
  #
  # Once selected, update:
  # - ORBIT_MAPPER_SGP4_REPO
  # - ORBIT_MAPPER_SGP4_TAG
  # - link target name
  set(ORBIT_MAPPER_SGP4_REPO "" CACHE STRING "SGP4 repo URL")
  set(ORBIT_MAPPER_SGP4_TAG "" CACHE STRING "SGP4 tag/commit")

  if (ORBIT_MAPPER_SGP4_REPO STREQUAL "")
    message(STATUS "SGP4 enabled, but ORBIT_MAPPER_SGP4_REPO is empty; building with stub propagator")
    target_compile_definitions(orbit_mapper PRIVATE ORBIT_MAPPER_SGP4_STUB=1)
  else()
    FetchContent_Declare(sgp4
      GIT_REPOSITORY ${ORBIT_MAPPER_SGP4_REPO}
      GIT_TAG ${ORBIT_MAPPER_SGP4_TAG}
    )
    FetchContent_MakeAvailable(sgp4)

    # TODO: Update link library name for the chosen implementation.
    # target_link_libraries(orbit_mapper PRIVATE sgp4)
    target_compile_definitions(orbit_mapper PRIVATE ORBIT_MAPPER_SGP4_STUB=1)
  endif()
else()
  target_compile_definitions(orbit_mapper PRIVATE ORBIT_MAPPER_SGP4_STUB=1)
endif()
