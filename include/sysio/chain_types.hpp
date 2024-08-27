#pragma once
#include "ship_protocol.hpp"

namespace chain_types {
using namespace sysio::ship_protocol;

struct block_info {
   uint32_t               block_num = {};
   sysio::checksum256     block_id  = {};
   sysio::block_timestamp timestamp;
};

SYSIO_REFLECT(block_info, block_num, block_id, timestamp);
}; // namespace chain_types