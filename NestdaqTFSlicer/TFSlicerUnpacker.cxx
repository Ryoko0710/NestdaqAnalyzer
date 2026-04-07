#include "TFSlicerUnpacker.h"
#include "HeartbeatFrameHeader.h"
#include <fairlogger/Logger.h>
#include <iostream>
#include <cstring>
#include <iomanip>

TFSlicerUnpacker::TFSlicerUnpacker() : m_data(nullptr), m_size(0), m_filter_header(nullptr), m_num_slices(0) {}
TFSlicerUnpacker::~TFSlicerUnpacker() {}

void TFSlicerUnpacker::set_data(uint64_t* data, size_t size_in_bytes)
{
    m_data = data;
    m_size = size_in_bytes;
}

void TFSlicerUnpacker::unpack()
{
    m_num_slices = 0;
    m_slices.clear();

    if (!m_data || m_size < sizeof(TimeFrame::Header)) {
        LOG(error) << "TFSlicerUnpacker: Invalid data pointer or size too small (" << m_size << ")";
        return;
    }

    uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(m_data);
    TimeFrame::Header* tf_header = reinterpret_cast<TimeFrame::Header*>(byte_ptr);

    if (tf_header->magic != TimeFrame::MAGIC) {
        LOG(error) << "TFSlicerUnpacker: TimeFrame magic mismatch. Exp: 0x" << std::hex << TimeFrame::MAGIC 
                   << " Got: 0x" << tf_header->magic;
        return;
    }

    // META TimeFrame contains Filter::Header and Filter::TrgTime data
    size_t offset = tf_header->hLength;
    if (offset + sizeof(Filter::Header) > m_size) {
        LOG(error) << "TFSlicerUnpacker: META block exceeds message size";
        return;
    }

    m_filter_header = reinterpret_cast<Filter::Header*>(byte_ptr + offset);
    if (m_filter_header->magic != Filter::v1::MAGIC) {
        LOG(error) << "TFSlicerUnpacker: Filter magic mismatch. Exp: 0x" << std::hex << Filter::v1::MAGIC 
                   << " Got: 0x" << m_filter_header->magic;
        return;
    }

    uint32_t num_triggers = m_filter_header->numTrigs;
    if (num_triggers == 0) {
        LOG(debug) << "TFSlicerUnpacker: No triggers found in this TimeFrame";
        return;
    }

    // LogicFilter container has Filter::Header then Filter::TrgTimeHeader then TrgTimes
    uint8_t* filter_block_start = byte_ptr + offset;
    // In v1 structure: Filter::Header (56) + Filter::TrgTimeHeader (16) + TrgTime data...
    auto trg_ptr = reinterpret_cast<Filter::TrgTime*>(filter_block_start + m_filter_header->hLength + sizeof(Filter::TrgTimeHeader));

    // Slices follow the META block
    size_t meta_block_size = tf_header->length;
    size_t next_slice_offset = meta_block_size; 

    for (uint32_t i = 0; i < num_triggers; ++i) { 
        if (next_slice_offset + sizeof(TimeFrame::Header) > m_size) {
            LOG(error) << "TFSlicerUnpacker: Slice " << i << " offset exceeds message size";
            break;
        }

        auto slice_tf_header = reinterpret_cast<TimeFrame::Header*>(byte_ptr + next_slice_offset);
        if (slice_tf_header->magic != TimeFrame::MAGIC) {
            LOG(error) << "TFSlicerUnpacker: Slice " << i << " magic mismatch. Exp: 0x" << std::hex << TimeFrame::MAGIC 
                       << " Got: 0x" << slice_tf_header->magic;
            break;
        }

        SliceData slice_info;
        slice_info.slice_index = i;
        slice_info.trigger_info = *(trg_ptr + i);
        
        size_t stf_cursor = next_slice_offset + slice_tf_header->hLength;
        for (size_t stf_idx = 0; stf_idx < slice_tf_header->numSource; ++stf_idx) {
            if (stf_cursor + sizeof(SubTimeFrame::Header) > m_size) break;

            auto stf_header = reinterpret_cast<SubTimeFrame::Header*>(byte_ptr + stf_cursor);
            if (stf_header->magic != SubTimeFrame::MAGIC) {
                LOG(error) << "TFSlicerUnpacker: SubTimeFrame magic mismatch inside slice " << i;
                break;
            }

            uint32_t fem_id = stf_header->femId;
            slice_info.fem_types[fem_id] = stf_header->femType;

            // HeartbeatFrame follows SubTimeFrame header
            size_t hbf_cursor = stf_cursor + stf_header->hLength; 
            if (hbf_cursor + sizeof(HeartbeatFrame::Header) > m_size) break;

            auto hbf_header = reinterpret_cast<HeartbeatFrame::Header*>(byte_ptr + hbf_cursor);
            
            // TDC Data follows HeartbeatFrame header
            size_t data_offset = hbf_cursor + hbf_header->hLength;
            size_t data_len_bytes = hbf_header->length - hbf_header->hLength;
            size_t n_words = data_len_bytes / sizeof(uint64_t);

            uint64_t* data_ptr = reinterpret_cast<uint64_t*>(byte_ptr + data_offset);
            for (size_t j = 0; j < n_words; ++j) {
                slice_info.data_by_fem[fem_id].push_back(data_ptr[j]);
            }

            stf_cursor += stf_header->length;
        }

        m_slices.push_back(slice_info);
        next_slice_offset += slice_tf_header->length;
    }
    m_num_slices = m_slices.size();
}

size_t TFSlicerUnpacker::get_num_slices() const
{
    return m_slices.size();
}

const TFSlicerUnpacker::SliceData& TFSlicerUnpacker::get_slice(size_t index) const
{
    static SliceData empty;
    if (index >= m_slices.size()) return empty;
    return m_slices[index];
}
