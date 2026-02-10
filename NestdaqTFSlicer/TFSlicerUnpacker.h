#ifndef TF_SLICER_UNPACKER_H
#define TF_SLICER_UNPACKER_H

#include <cstdint>
#include <vector>
#include <map>
#include <iostream>
#include <iomanip>
#include <memory>

// Include necessary headers from Nestdaq
// Paths will be handled by CMake include_directories
#include "FilterHeader.h"
#include "TimeFrameHeader.h"
#include "SubTimeFrameHeader.h"
#include "UnpackTdc.h"

class TFSlicerUnpacker {
public:
    struct SliceData {
        int slice_index;
        Filter::TrgTime trigger_info;
        // Map: FEM ID -> Vector of Raw Data
        std::map<uint32_t, std::vector<uint64_t>> data_by_fem; 
        // Map: FEM ID -> FEM Type
        std::map<uint32_t, int> fem_types;
    };

    TFSlicerUnpacker();
    ~TFSlicerUnpacker();

    void set_data(uint64_t* data, size_t size_in_bytes);
    void unpack();

    // Accessors
    const Filter::Header* get_filter_header() const { return m_filter_header; }
    size_t get_num_slices() const { return m_slices.size(); }
    const SliceData& get_slice(size_t index) const;

private:
    uint64_t* m_data = nullptr;
    size_t m_size = 0; // Size in bytes
    
    Filter::Header* m_filter_header = nullptr;
    std::vector<SliceData> m_slices;

    void parse_slices();
};

#endif // TF_SLICER_UNPACKER_H
