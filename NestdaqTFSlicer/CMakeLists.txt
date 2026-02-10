# ------------------------------------------------------------------------------
# NestdaqTFSlicer Library
# ------------------------------------------------------------------------------
add_library(NestdaqTFSlicer SHARED
    TFSlicerUnpacker.cxx
)

target_link_libraries(NestdaqTFSlicer
    FairMQ::FairMQ
    FairLogger::FairLogger
)

# KTimer is header-only (included in source), so we don't compile it here to avoid duplication.
# It is available to other targets via include_directories in top CMake.
