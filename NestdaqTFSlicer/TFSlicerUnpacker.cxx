#include "TFSlicerUnpacker.h"
#include "HeartbeatFrameHeader.h"

#include <cstring>
#include <stdexcept>

// Debug macro
//#define DEBUG_UNPACKER

#ifdef DEBUG_UNPACKER
#define LOG_DEBUG(x) std::cout << "[TFSlicerUnpacker] " << x << std::endl
#else
#define LOG_DEBUG(x)
#endif

// Rebuild forced
TFSlicerUnpacker::TFSlicerUnpacker()
{
}

TFSlicerUnpacker::~TFSlicerUnpacker()
{
}

void TFSlicerUnpacker::set_data(uint64_t* data, size_t size_in_bytes)
{
    m_data = data;
    m_size = size_in_bytes;
    m_filter_header = nullptr;
    m_slices.clear();
}

void TFSlicerUnpacker::unpack()
{
    if (!m_data || m_size == 0) {
        return;
    }

    uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(m_data);
    size_t offset = 0;

    // 1. TF Header (META)
    if (offset + sizeof(TimeFrame::Header) > m_size) return;
    auto tf_header = reinterpret_cast<TimeFrame::Header*>(byte_ptr + offset);
    if (tf_header->magic != TimeFrame::MAGIC) {
        LOG_DEBUG("Invalid TimeFrame Magic: " << std::hex << tf_header->magic);
        return;
    }
    offset += sizeof(TimeFrame::Header);

    // 2. Filter Header
    if (offset + sizeof(Filter::Header) > m_size) return;
    m_filter_header = reinterpret_cast<Filter::Header*>(byte_ptr + offset);
    
    // Validating Filter Magic (v1)
    if (m_filter_header->magic != Filter::v1::MAGIC) {
        LOG_DEBUG("Invalid Filter Magic: " << std::hex << m_filter_header->magic);
    }

    uint32_t num_triggers = m_filter_header->numTrigs;
    
    // Point to triggers. 
    // Filter Header structure in v1: Header (incl. hLength) + Triggers.
    // m_filter_header->length is total size of Filter block (Header + Triggers).
    // m_filter_header->hLength is size of Header struct (56 bytes).
    
    uint8_t* filter_block_start = reinterpret_cast<uint8_t*>(m_filter_header);
    auto trg_ptr = reinterpret_cast<Filter::TrgTime*>(filter_block_start + m_filter_header->hLength);
    
    // Skip the META block to find the first SLICE.
    // META block size is defined in the initial TF Header.
    size_t meta_block_size = tf_header->length;
    size_t next_slice_offset = meta_block_size; // Relative to start of data (0)

    size_t current_offset = next_slice_offset;
    
    for (uint32_t i = 0; i < num_triggers; ++i) { 
        if (current_offset >= m_size) break;

        // SLICE Block
        auto slice_tf_header = reinterpret_cast<TimeFrame::Header*>(byte_ptr + current_offset);
        if (slice_tf_header->magic != TimeFrame::MAGIC || slice_tf_header->type != TimeFrame::SLICE) {
            LOG_DEBUG("Invalid Slice Header at index " << i);
            break;
        }

        SliceData slice;
        slice.slice_index = i;
        slice.trigger_info = trg_ptr[i]; // Store trigger info
        
        // Parse STFs inside this Slice
        size_t slice_payload_offset = current_offset + sizeof(TimeFrame::Header);
        size_t slice_end = current_offset + slice_tf_header->length;

        size_t stf_cursor = slice_payload_offset;
        while (stf_cursor < slice_end) {
            auto stf_header = reinterpret_cast<SubTimeFrame::Header*>(byte_ptr + stf_cursor);
            if (stf_header->magic != SubTimeFrame::MAGIC) {
                break;
            }
            
            uint32_t fem_id = stf_header->femId;
            uint32_t fem_type = stf_header->femType;
            
            slice.fem_types[fem_id] = fem_type;
            
            // Inside STF: HBFH -> Data
            size_t hbf_cursor = stf_cursor + stf_header->hLength; 
            
            // HBF Header
            auto hbf_header = reinterpret_cast<HeartbeatFrame::Header*>(byte_ptr + hbf_cursor);
            if (hbf_header->magic != HeartbeatFrame::MAGIC) {
                break;
            }
            
            // Data follows HBF Header
            size_t data_len = hbf_header->length - sizeof(HeartbeatFrame::Header);
            uint64_t* data_ptr = reinterpret_cast<uint64_t*>(byte_ptr + hbf_cursor + sizeof(HeartbeatFrame::Header));
            size_t num_words = data_len / sizeof(uint64_t);

            for (size_t w = 0; w < num_words; ++w) {
                slice.data_by_fem[fem_id].push_back(data_ptr[w]);
            }

            stf_cursor += stf_header->length;
        }

        m_slices.push_back(slice);
        current_offset += slice_tf_header->length;
    }
}

const TFSlicerUnpacker::SliceData& TFSlicerUnpacker::get_slice(size_t index) const
{
    if (index >= m_slices.size()) {
        throw std::out_of_range("Slice index out of range");
    }
    return m_slices[index];
}
