#pragma once

#include "from_json.hpp"
#include "to_json.hpp"
#include "operators.hpp"
#include <vector>

namespace sysio {

struct bytes {
   std::vector<char> data;
};

SYSIO_REFLECT(bytes, data);
SYSIO_COMPARE(bytes);

template <typename S>
void from_json(bytes& obj, S& stream) {
   return sysio::from_json_hex(obj.data, stream);
}

template <typename S>
void to_json(const bytes& obj, S& stream) {
   return sysio::to_json_hex(obj.data.data(), obj.data.size(), stream);
}

} // namespace sysio
