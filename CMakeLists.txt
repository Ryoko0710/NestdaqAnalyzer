cmake_minimum_required(VERSION 3.16)
project(NestdaqAnalyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Find Packages
# ------------------------------------------------------------------------------
find_package(FairMQ REQUIRED)
find_package(ROOT REQUIRED COMPONENTS RIO RHTTP Hist)
find_package(Boost REQUIRED COMPONENTS iostreams) # thread component is usually needed too

# Dependencies from FairMQ
if(FairMQ_FOUND)
  foreach(dep IN LISTS FairMQ_PACKAGE_DEPENDENCIES)
    if(FairMQ_${dep}_COMPONENTS)
      if(${dep} MATCHES "Boost")
        find_package(${dep} ${FairMQ_${dep}_VERSION} COMPONENTS ${FairMQ_${dep}_COMPONENTS} iostreams thread)
      else()
        find_package(${dep} ${FairMQ_${dep}_VERSION} COMPONENTS ${FairMQ_${dep}_COMPONENTS})
      endif()
    else()
      find_package(${dep} ${FairMQ_${dep}_VERSION})
    endif()
  endforeach()
endif()

if(NOT TARGET FairLogger::FairLogger)
    find_package(FairLogger)
endif()

# ------------------------------------------------------------------------------
# Include Directories
# ------------------------------------------------------------------------------
set(NESTDAQ_USER_IMPL_DIR "/home/unidaq/nestdaq/src/nestdaq-user-impl")
set(NESTDAQ_UNPACKER_SRC_DIR "/home/unidaq/repos/NestdaqUnpacker/src")

include_directories(
    ${NESTDAQ_USER_IMPL_DIR}
    ${NESTDAQ_UNPACKER_SRC_DIR}
    ${CMAKE_SOURCE_DIR}/NestdaqTFSlicer
    ${FairMQ_INCLUDE_DIRS}
    ${ROOT_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

# ------------------------------------------------------------------------------
# Link Directories
# ------------------------------------------------------------------------------
link_directories(
    ${FairMQ_LIBDIR}
    ${FairLogger_LIBDIR}
    ${Boost_LIBRARY_DIRS}
    ${ROOT_LIBRARY_DIRS}
)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------
add_subdirectory(NestdaqTFSlicer)
add_subdirectory(analyzer)
