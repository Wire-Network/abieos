// copyright defined in abieos/LICENSE.md

#include "abieos.h"
#include "abieos.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

using namespace abieos;

struct abieos_context_s {
    const char* last_error = "";
    std::string last_error_buffer{};
    std::string result_str{};
    std::vector<char> result_bin{};

    std::map<name, abi> contracts{};
};

void fix_null_str(const char*& s) {
    if (!s)
        s = "";
}

bool set_error(abieos_context* context, std::string error) noexcept {
    context->last_error_buffer = std::move(error);
    context->last_error = context->last_error_buffer.c_str();
    return false;
}

template <typename T, typename F>
auto handle_exceptions(abieos_context* context, T errval, F f) noexcept -> decltype(f()) {
    if (!context)
        return errval;
    try {
        return f();
    } catch (std::exception& e) {
        set_error(context, e.what());
        return errval;
    } catch (...) {
        set_error(context, "unknown exception");
        return errval;
    }
}

extern "C" abieos_context* abieos_create() {
    try {
        return new abieos_context{};
    } catch (...) {
        return nullptr;
    }
}

extern "C" void abieos_destroy(abieos_context* context) { delete context; }

extern "C" const char* abieos_get_error(abieos_context* context) {
    if (!context)
        return "context is null";
    return context->last_error;
}

extern "C" int abieos_get_bin_size(abieos_context* context) {
    if (!context)
        return 0;
    return context->result_bin.size();
}

extern "C" const char* abieos_get_bin_data(abieos_context* context) {
    if (!context)
        return nullptr;
    return context->result_bin.data();
}

extern "C" const char* abieos_get_bin_hex(abieos_context* context) {
    return handle_exceptions(context, nullptr, [&] {
        context->result_str.clear();
        hex(context->result_bin.begin(), context->result_bin.end(), std::back_inserter(context->result_str));
        return context->result_str.c_str();
    });
}

extern "C" uint64_t abieos_string_to_name(abieos_context* context, const char* str) {
    fix_null_str(str);
    return sysio::string_to_name(str);
}

extern "C" const char* abieos_name_to_string(abieos_context* context, uint64_t name) {
    return handle_exceptions(context, nullptr, [&] {
        context->result_str = sysio::name_to_string(name);
        return context->result_str.c_str();
    });
}

extern "C" abieos_bool abieos_set_abi(abieos_context* context, uint64_t contract, const char* abi) {
    fix_null_str(abi);
    return handle_exceptions(context, false, [&]() {
        context->last_error = "abi parse error";
        abi_def def{};
        std::string error;
        std::string abi_copy{abi};
        sysio::json_token_stream stream(abi_copy.data());
        from_json(def, stream);
        if (!check_abi_version(def.version, error))
            return set_error(context, std::move(error));
        abieos::abi c;
        convert(def, c);
        context->contracts.insert({name{contract}, std::move(c)});
        return true;
    });
}

extern "C" abieos_bool abieos_set_abi_bin(abieos_context* context, uint64_t contract, const char* data, size_t size) {
    return handle_exceptions(context, false, [&] {
        context->last_error = "abi parse error";
        if (!data || !size)
            return set_error(context, "no data");
        std::string error;
        sysio::input_stream stream{data, size};
        std::string version;
        from_bin(version, stream);
        if (!check_abi_version(version, error))
            return set_error(context, std::move(error));
        abi_def def{};
        stream = {data, size};
        from_bin(def, stream);
        abieos::abi c;
        convert(def, c);
        context->contracts.insert({name{contract}, std::move(c)});
        return true;
    });
}

extern "C" abieos_bool abieos_set_abi_hex(abieos_context* context, uint64_t contract, const char* hex) {
    fix_null_str(hex);
    return handle_exceptions(context, false, [&]() -> abieos_bool {
        std::vector<char> data;
        std::string error;
        if (!unhex(error, hex, hex + strlen(hex), std::back_inserter(data))) {
            if (!error.empty())
                set_error(context, std::move(error));
            return false;
        }
        return abieos_set_abi_bin(context, contract, data.data(), data.size());
    });
}

extern "C" const char* abieos_get_type_for_action(abieos_context* context, uint64_t contract, uint64_t action) {
    return handle_exceptions(context, nullptr, [&] {
        auto contract_it = context->contracts.find(::abieos::name{contract});
        if (contract_it == context->contracts.end())
            throw std::runtime_error("contract \"" + sysio::name_to_string(contract) + "\" is not loaded");
        auto& c = contract_it->second;

        auto action_it = c.action_types.find(name{action});
        if (action_it == c.action_types.end())
            throw std::runtime_error("contract \"" + sysio::name_to_string(contract) + "\" does not have action \"" +
                                     sysio::name_to_string(action) + "\"");
        return action_it->second.c_str();
    });
}

extern "C" const char* abieos_get_type_for_table(abieos_context* context, uint64_t contract, uint64_t table) {
    return handle_exceptions(context, nullptr, [&] {
        auto contract_it = context->contracts.find(::abieos::name{contract});
        if (contract_it == context->contracts.end())
            throw std::runtime_error("contract \"" + sysio::name_to_string(contract) + "\" is not loaded");
        auto& c = contract_it->second;

        auto table_it = c.table_types.find(name{table});
        if (table_it == c.table_types.end())
            throw std::runtime_error("contract \"" + sysio::name_to_string(contract) + "\" does not have table \"" +
                                     sysio::name_to_string(table) + "\"");
        return table_it->second.c_str();
    });
}

extern "C" const char* abieos_get_type_for_action_result(abieos_context* context, uint64_t contract,
                                                         uint64_t action_result) {
    return handle_exceptions(context, nullptr, [&] {
        auto contract_it = context->contracts.find(::abieos::name{contract});
        if (contract_it == context->contracts.end())
            throw std::runtime_error("contract \"" + sysio::name_to_string(contract) + "\" is not loaded");
        auto& c = contract_it->second;

        auto action_result_it = c.action_result_types.find(name{action_result});
        if (action_result_it == c.action_result_types.end())
            throw std::runtime_error("contract \"" + sysio::name_to_string(contract) +
                                     "\" does not have action_result \"" + sysio::name_to_string(action_result) + "\"");
        return action_result_it->second.c_str();
    });
}

extern "C" abieos_bool abieos_json_to_bin(abieos_context* context, uint64_t contract, const char* type,
                                          const char* json) {
    fix_null_str(type);
    fix_null_str(json);
    return handle_exceptions(context, false, [&] {
        context->last_error = "json parse error";
        auto contract_it = context->contracts.find(::abieos::name{contract});
        if (contract_it == context->contracts.end())
            return set_error(context, "contract \"" + sysio::name_to_string(contract) + "\" is not loaded");
        std::string error;
        auto t = contract_it->second.get_type(type);
        context->result_bin.clear();
        context->result_bin = t->json_to_bin(json);
        return true;
    });
}

extern "C" abieos_bool abieos_json_to_bin_reorderable(abieos_context* context, uint64_t contract, const char* type,
                                                      const char* json) {
    fix_null_str(type);
    fix_null_str(json);
    return handle_exceptions(context, false, [&] {
        context->last_error = "json parse error";
        auto contract_it = context->contracts.find(::abieos::name{contract});
        if (contract_it == context->contracts.end())
            return set_error(context, "contract \"" + sysio::name_to_string(contract) + "\" is not loaded");
        std::string error;
        auto t = contract_it->second.get_type(type);
        context->result_bin.clear();
        context->result_bin = t->json_to_bin_reorderable(json);
        return true;
    });
}

extern "C" const char* abieos_bin_to_json(abieos_context* context, uint64_t contract, const char* type,
                                          const char* data, size_t size) {
    fix_null_str(type);
    return handle_exceptions(context, nullptr, [&]() -> const char* {
        if (!data)
            size = 0;
        context->last_error = "binary decode error";
        auto contract_it = context->contracts.find(::abieos::name{contract});
        std::string error;
        if (contract_it == context->contracts.end()) {
            (void)set_error(error, "contract \"" + sysio::name_to_string(contract) + "\" is not loaded");
            return nullptr;
        }
        auto t = contract_it->second.get_type(type);
        sysio::input_stream bin{data, size};
        context->result_str = t->bin_to_json(bin);
        return context->result_str.c_str();
    });
}

extern "C" const char* abieos_hex_to_json(abieos_context* context, uint64_t contract, const char* type,
                                          const char* hex) {
    fix_null_str(hex);
    return handle_exceptions(context, nullptr, [&]() -> const char* {
        std::vector<char> data;
        std::string error;
        if (!unhex(error, hex, hex + strlen(hex), std::back_inserter(data))) {
            if (!error.empty())
                set_error(context, std::move(error));
            return nullptr;
        }
        return abieos_bin_to_json(context, contract, type, data.data(), data.size());
    });
}

extern "C" abieos_bool abieos_abi_json_to_bin(abieos_context* context, const char* abi_json) {
    fix_null_str(abi_json);
    return handle_exceptions(context, false, [&] {
        std::string abi_copy{abi_json};
        sysio::json_token_stream json_stream(abi_copy.data());
        abi_def def{};
        std::string error;
        from_json(def, json_stream);
        if (!check_abi_version(def.version, error)) {
            return set_error(context, std::move(error));
        }
        context->result_bin = convert_to_bin(def);
        return true;
    });
}

extern "C" const char* abieos_abi_bin_to_json(abieos_context* context, const char* abi_bin_data,
                                              const size_t abi_bin_data_size) {
    return handle_exceptions(context, nullptr, [&]() -> const char* {
        if (!abi_bin_data || abi_bin_data_size == 0) {
            set_error(context, "no data");
            return nullptr;
        }
        sysio::input_stream bin_stream{abi_bin_data, abi_bin_data_size};
        abi_def def{};
        from_bin(def, bin_stream);
        std::string error;
        if (!check_abi_version(def.version, error)) {
            set_error(context, std::move(error));
            return nullptr;
        }
        std::vector<char> bytes;
        sysio::vector_stream byte_stream(bytes);
        to_json(def, byte_stream);

        context->result_str.assign(bytes.begin(), bytes.end());
        return context->result_str.c_str();
    });
}

// --- Big-endian KV key decoder ---
//
// Decodes keys produced by CDT's `sysio::kv::be_key_stream` (see
// libraries/sysiolib/contracts/sysio/kv_utils.hpp in wire-cdt). The encoding
// is designed so a lexicographic memcmp on the encoded bytes matches the
// natural ordering of the underlying values, which means the encoder applies
// sign-flips to signed integers and to floats:
//
//   uint8/16/32/64    big-endian raw
//   int8/16/32/64     big-endian XOR 1<<(N-1)  (flip sign bit so negatives sort first)
//   uint128/int128    big-endian (high uint64 then low uint64); int128 XOR 1<<127
//   name              big-endian raw uint64
//   bool              1 byte
//   float (32-bit)    big-endian, positive values XOR 1<<31, negative values bitwise NOT
//   double (64-bit)   big-endian, positive values XOR 1<<63, negative values bitwise NOT
//   string / bytes    NUL-escaped: each 0x00 in the payload is rewritten as 0x00 0x01,
//                     terminated by a 0x00 0x00 sentinel
//   checksum160       20 raw bytes (no transform)
//   checksum256       32 raw bytes
//   checksum512       64 raw bytes
//
// `checksum*` types reach be_key_stream via a generic
// `template<DataStream, size_t> operator<<(DataStream&, const fixed_bytes<Size>&)`
// in core/sysio/fixed_bytes.hpp that just writes the raw bytes via
// be_key_stream::write(const char*, size_t) — no sign flip, no length prefix.

namespace {

[[noreturn]] void be_key_overrun(const char* type) {
    throw std::runtime_error(std::string("be_key: unexpected end of data reading ") + type);
}

uint8_t read_be8(const char*& pos, const char* end) {
    if (pos + 1 > end) be_key_overrun("uint8");
    return static_cast<uint8_t>(*pos++);
}

uint16_t read_be16(const char*& pos, const char* end) {
    if (pos + 2 > end) be_key_overrun("uint16");
    uint16_t v = 0;
    for (int i = 0; i < 2; ++i) v = static_cast<uint16_t>((v << 8) | static_cast<uint8_t>(*pos++));
    return v;
}

uint32_t read_be32(const char*& pos, const char* end) {
    if (pos + 4 > end) be_key_overrun("uint32");
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<uint8_t>(*pos++);
    return v;
}

uint64_t read_be64(const char*& pos, const char* end) {
    if (pos + 8 > end) be_key_overrun("uint64");
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<uint8_t>(*pos++);
    return v;
}

// Read N raw bytes and emit as a lowercase hex string (no 0x prefix), e.g.
// for checksum160/256/512 fields. The CDT encoder writes these via
// be_key_stream::write() with no transform, so the decoder mirrors that.
std::string read_raw_hex(const char*& pos, const char* end, size_t n, const char* type) {
    if (pos + n > end) be_key_overrun(type);
    static const char hex_chars[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = static_cast<uint8_t>(*pos++);
        out.push_back(hex_chars[b >> 4]);
        out.push_back(hex_chars[b & 0x0F]);
    }
    return out;
}

// Decode a NUL-escaped string/bytes payload terminated by 0x00 0x00.
// Each 0x00 0x01 in the stream represents an embedded NUL byte; a bare
// 0x00 followed by 0x00 ends the field. Mirrors `write_escaped` in CDT's
// be_key_stream.
std::string read_nul_escaped(const char*& pos, const char* end) {
    std::string out;
    while (pos < end) {
        char c = *pos++;
        if (c != '\0') {
            out.push_back(c);
            continue;
        }
        if (pos >= end) be_key_overrun("string (truncated escape)");
        char next = *pos++;
        if (next == '\0') return out;            // 0x00 0x00 → end of field
        if (next == '\x01') { out.push_back('\0'); continue; } // 0x00 0x01 → embedded NUL
        be_key_overrun("string (bad escape)");
    }
    be_key_overrun("string (no terminator)");
}

// Append a JSON-escaped string literal (with surrounding quotes) to `out`.
// Handles the escapes that string fields can legitimately contain after
// NUL-unescaping (control chars, quotes, backslashes).
void append_json_string(std::string& out, std::string_view s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (static_cast<uint8_t>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<uint8_t>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void decode_be_field(const std::string& type, const char*& pos, const char* end, std::string& out) {
    if (type == "uint8") {
        out += std::to_string(read_be8(pos, end));
    } else if (type == "int8") {
        // be_key_stream: int8 → uint8 ^ 0x80
        uint8_t raw = read_be8(pos, end);
        out += std::to_string(static_cast<int8_t>(raw ^ 0x80));
    } else if (type == "uint16") {
        out += std::to_string(read_be16(pos, end));
    } else if (type == "int16") {
        uint16_t raw = read_be16(pos, end);
        out += std::to_string(static_cast<int16_t>(raw ^ 0x8000));
    } else if (type == "uint32") {
        out += std::to_string(read_be32(pos, end));
    } else if (type == "int32") {
        uint32_t raw = read_be32(pos, end);
        out += std::to_string(static_cast<int32_t>(raw ^ 0x80000000u));
    } else if (type == "uint64") {
        // JSON numbers >2^53 are unsafe in JS — emit as a string for parity
        // with the rest of abieos which serializes uint64 as a JSON string.
        out += "\"" + std::to_string(read_be64(pos, end)) + "\"";
    } else if (type == "int64") {
        uint64_t raw = read_be64(pos, end);
        int64_t signed_val = static_cast<int64_t>(raw ^ (uint64_t(1) << 63));
        out += "\"" + std::to_string(signed_val) + "\"";
    } else if (type == "uint128") {
        uint64_t hi = read_be64(pos, end);
        uint64_t lo = read_be64(pos, end);
        char buf[40];
        std::snprintf(buf, sizeof(buf), "\"0x%016" PRIx64 "%016" PRIx64 "\"", hi, lo);
        out += buf;
    } else if (type == "int128") {
        // be_key_stream: int128 → (uint128) ^ (1 << 127)
        uint64_t hi = read_be64(pos, end);
        uint64_t lo = read_be64(pos, end);
        hi ^= (uint64_t(1) << 63);
        char buf[40];
        std::snprintf(buf, sizeof(buf), "\"0x%016" PRIx64 "%016" PRIx64 "\"", hi, lo);
        out += buf;
    } else if (type == "name") {
        out += "\"" + sysio::name_to_string(read_be64(pos, end)) + "\"";
    } else if (type == "float32" || type == "float") {
        uint32_t bits = read_be32(pos, end);
        // Reverse encoder: positive had top bit flipped, negative had all bits flipped
        if (bits >> 31) bits ^= (uint32_t(1) << 31);
        else            bits = ~bits;
        float v;
        std::memcpy(&v, &bits, 4);
        out += std::to_string(v);
    } else if (type == "float64" || type == "double") {
        uint64_t bits = read_be64(pos, end);
        if (bits >> 63) bits ^= (uint64_t(1) << 63);
        else            bits = ~bits;
        double v;
        std::memcpy(&v, &bits, 8);
        out += std::to_string(v);
    } else if (type == "bool") {
        out += (read_be8(pos, end) ? "true" : "false");
    } else if (type == "string") {
        append_json_string(out, read_nul_escaped(pos, end));
    } else if (type == "bytes") {
        // bytes are encoded the same way as string (NUL-escaped). Emit as a
        // hex string for parity with the rest of abieos which represents
        // bytes fields as hex in JSON.
        std::string raw = read_nul_escaped(pos, end);
        static const char hex_chars[] = "0123456789abcdef";
        out += "\"";
        for (char c : raw) {
            uint8_t b = static_cast<uint8_t>(c);
            out.push_back(hex_chars[b >> 4]);
            out.push_back(hex_chars[b & 0x0F]);
        }
        out += "\"";
    } else if (type == "checksum160") {
        out += "\"" + read_raw_hex(pos, end, 20, "checksum160") + "\"";
    } else if (type == "checksum256") {
        out += "\"" + read_raw_hex(pos, end, 32, "checksum256") + "\"";
    } else if (type == "checksum512") {
        out += "\"" + read_raw_hex(pos, end, 64, "checksum512") + "\"";
    } else {
        throw std::runtime_error("be_key: unsupported type \"" + type + "\"");
    }
}

// Parse a JSON string array using sysio's existing JSON parser instead of
// hand-rolling. Throws on malformed input. Caller passes a mutable buffer.
std::vector<std::string> parse_json_string_array(const char* json) {
    if (!json) throw std::runtime_error("be_key: null JSON array");
    std::string copy{json};
    sysio::json_token_stream stream(copy.data());
    std::vector<std::string> result;
    sysio::from_json(result, stream);
    return result;
}

} // anonymous namespace

extern "C" const char* abieos_be_key_hex_to_json(abieos_context* context, const char* key_names_json,
                                                   const char* key_types_json, const char* hex) {
    fix_null_str(key_names_json);
    fix_null_str(key_types_json);
    fix_null_str(hex);
    return handle_exceptions(context, nullptr, [&]() -> const char* {
        auto names = parse_json_string_array(key_names_json);
        auto types = parse_json_string_array(key_types_json);
        if (names.size() != types.size())
            throw std::runtime_error("be_key: key_names and key_types must have the same length");
        if (names.empty())
            throw std::runtime_error("be_key: key_names/key_types are empty");

        std::vector<char> data;
        std::string error;
        if (!unhex(error, hex, hex + strlen(hex), std::back_inserter(data))) {
            set_error(context, error.empty() ? "invalid hex" : std::move(error));
            return nullptr;
        }

        const char* pos = data.data();
        const char* end = pos + data.size();

        std::string json = "{";
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) json += ",";
            append_json_string(json, names[i]);
            json += ":";
            decode_be_field(types[i], pos, end, json);
        }
        json += "}";

        if (pos != end) {
            // Trailing bytes after all declared fields are decoded means the
            // caller's key_types didn't match the encoded key. Surface that
            // as an explicit error rather than silently dropping bytes.
            throw std::runtime_error("be_key: " + std::to_string(end - pos) +
                                     " trailing byte(s) after decoding declared fields");
        }

        context->result_str = std::move(json);
        return context->result_str.c_str();
    });
}

extern "C" abieos_bool abieos_delete_contract(abieos_context* context, uint64_t contract) {
    auto itr = context->contracts.find(::abieos::name{contract});
    if(itr == context->contracts.end()) {
        return false;
    } else {
        context->contracts.erase(itr);
        return true;
    }
}
