// copyright defined in abieos/LICENSE.txt

#pragma once

#include <eosio/chain_conversions.hpp>
#include <eosio/from_bin.hpp>
#include <eosio/from_json.hpp>
#include <eosio/reflection.hpp>
#include <eosio/to_bin.hpp>

#ifdef EOSIO_CDT_COMPILATION
#include <cwchar>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#endif

#include <ctime>
#include <map>
#include <optional>
#include <variant>
#include <vector>

#ifdef EOSIO_CDT_COMPILATION
#pragma clang diagnostic pop
#endif

#include "abieos_numeric.hpp"

#include "rapidjson/reader.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace abieos {

using eosio::from_bin;
using eosio::to_bin;

inline constexpr bool trace_json_to_jvalue_event = false;
inline constexpr bool trace_json_to_jvalue = false;
inline constexpr bool trace_jvalue_to_bin = false;
inline constexpr bool trace_json_to_bin = false;
inline constexpr bool trace_json_to_bin_event = false;
inline constexpr bool trace_bin_to_json = false;

inline constexpr size_t max_stack_size = 128;

template <typename T>
inline constexpr bool is_vector_v = false;

template <typename T>
inline constexpr bool is_vector_v<std::vector<T>> = true;

template <typename T>
inline constexpr bool is_pair_v = false;

template <typename First, typename Second>
inline constexpr bool is_pair_v<std::pair<First, Second>> = true;

template <typename T>
inline constexpr bool is_optional_v = false;

template <typename T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

template <typename T>
inline constexpr bool is_variant_v = false;

template <typename... Ts>
inline constexpr bool is_variant_v<std::variant<Ts...>> = true;

template <typename T>
inline constexpr bool is_string_v = false;

template <>
inline constexpr bool is_string_v<std::string> = true;

template <typename State, typename T>
ABIEOS_NODISCARD bool set_error(State& state, const eosio::result<T>& res) {
    state.error = res.error().message();
    return false;
}

// Pseudo objects never exist, except in serialized form
struct pseudo_optional;
struct pseudo_extension;
struct pseudo_object;
struct pseudo_array;
struct pseudo_variant;

template <typename T>
struct might_not_exist {
    T value{};
};

template <typename T, typename S>
eosio::result<void> from_bin(might_not_exist<T>& obj, S& stream) {
    if (stream.remaining())
        return from_bin(obj.value, stream);
    return eosio::outcome::success();
}

template <typename T, typename S>
eosio::result<void> from_json(might_not_exist<T>& obj, S& stream) {
    return from_json(obj.value, stream);
}

// !!!
template <typename SrcIt, typename DestIt>
void hex(SrcIt begin, SrcIt end, DestIt dest) {
    auto nibble = [&dest](uint8_t i) {
        if (i <= 9)
            *dest++ = '0' + i;
        else
            *dest++ = 'A' + i - 10;
    };
    while (begin != end) {
        nibble(((uint8_t)*begin) >> 4);
        nibble(((uint8_t)*begin) & 0xf);
        ++begin;
    }
}

// !!!
template <typename SrcIt>
std::string hex(SrcIt begin, SrcIt end) {
    std::string s;
    hex(begin, end, std::back_inserter(s));
    return s;
}

// !!!
template <typename SrcIt, typename DestIt>
ABIEOS_NODISCARD bool unhex(std::string& error, SrcIt begin, SrcIt end, DestIt dest) {
    auto get_digit = [&](uint8_t& nibble) {
        if (*begin >= '0' && *begin <= '9')
            nibble = *begin++ - '0';
        else if (*begin >= 'a' && *begin <= 'f')
            nibble = *begin++ - 'a' + 10;
        else if (*begin >= 'A' && *begin <= 'F')
            nibble = *begin++ - 'A' + 10;
        else
            return set_error(error, "expected hex string");
        return true;
    };
    while (begin != end) {
        uint8_t h, l;
        if (!get_digit(h) || !get_digit(l))
            return false;
        *dest++ = (h << 4) | l;
    }
    return true;
}

template <typename T>
void push_raw(std::vector<char>& bin, const T& obj) {
    static_assert(std::is_trivially_copyable_v<T>);
    bin.insert(bin.end(), reinterpret_cast<const char*>(&obj), reinterpret_cast<const char*>(&obj + 1));
}

// !!!
struct input_buffer {
    const char* pos = nullptr;
    const char* end = nullptr;
};

///////////////////////////////////////////////////////////////////////////////
// stream events
///////////////////////////////////////////////////////////////////////////////

enum class event_type {
    received_null,         // 0
    received_bool,         // 1
    received_string,       // 2
    received_start_object, // 3
    received_key,          // 4
    received_end_object,   // 5
    received_start_array,  // 6
    received_end_array,    // 7
};

struct event_data {
    bool value_bool = 0;
    std::string value_string{};
    std::string key{};
};

ABIEOS_NODISCARD bool receive_event(struct json_to_bin_state&, event_type, bool start);

template <typename Derived>
struct json_reader_handler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, Derived> {
    event_data received_data{};
    bool started = false;

    Derived& get_derived() { return *static_cast<Derived*>(this); }

    bool get_start() {
        if (started)
            return false;
        started = true;
        return true;
    }

    bool get_bool() const { return received_data.value_bool; }

    const std::string& get_string() const { return received_data.value_string; }

    bool Null() { return receive_event(get_derived(), event_type::received_null, get_start()); }
    bool Bool(bool v) {
        received_data.value_bool = v;
        return receive_event(get_derived(), event_type::received_bool, get_start());
    }
    bool RawNumber(const char* v, rapidjson::SizeType length, bool copy) { return String(v, length, copy); }
    bool Int(int v) { return false; }
    bool Uint(unsigned v) { return false; }
    bool Int64(int64_t v) { return false; }
    bool Uint64(uint64_t v) { return false; }
    bool Double(double v) { return false; }
    bool String(const char* v, rapidjson::SizeType length, bool) {
        received_data.value_string = {v, length};
        return receive_event(get_derived(), event_type::received_string, get_start());
    }
    bool StartObject() { return receive_event(get_derived(), event_type::received_start_object, get_start()); }
    bool Key(const char* v, rapidjson::SizeType length, bool) {
        received_data.key = {v, length};
        return receive_event(get_derived(), event_type::received_key, get_start());
    }
    bool EndObject(rapidjson::SizeType) {
        return receive_event(get_derived(), event_type::received_end_object, get_start());
    }
    bool StartArray() { return receive_event(get_derived(), event_type::received_start_array, get_start()); }
    bool EndArray(rapidjson::SizeType) {
        return receive_event(get_derived(), event_type::received_end_array, get_start());
    }
};

///////////////////////////////////////////////////////////////////////////////
// json model
///////////////////////////////////////////////////////////////////////////////

struct jvalue;
using jarray = std::vector<jvalue>;
using jobject = std::map<std::string, jvalue>;

struct jvalue {
    std::variant<std::nullptr_t, bool, std::string, jobject, jarray> value;
};

inline event_type get_event_type(std::nullptr_t) { return event_type::received_null; }
inline event_type get_event_type(bool b) { return event_type::received_bool; }
inline event_type get_event_type(const std::string& s) { return event_type::received_string; }
inline event_type get_event_type(const jobject&) { return event_type::received_start_object; }
inline event_type get_event_type(const jarray&) { return event_type::received_start_array; }

inline event_type get_event_type(const jvalue& value) {
    return std::visit([](const auto& x) { return get_event_type(x); }, value.value);
}

///////////////////////////////////////////////////////////////////////////////
// state and serializers
///////////////////////////////////////////////////////////////////////////////

struct size_insertion {
    size_t position = 0;
    uint32_t size = 0;
};

struct json_to_jvalue_stack_entry {
    jvalue* value = nullptr;
    std::string key = "";
};

struct jvalue_to_bin_stack_entry {
    const struct abi_type* type = nullptr;
    bool allow_extensions = false;
    const jvalue* value = nullptr;
    int position = -1;
};

struct json_to_bin_stack_entry {
    const struct abi_type* type = nullptr;
    bool allow_extensions = false;
    int position = -1;
    size_t size_insertion_index = 0;
    size_t variant_type_index = 0;
};

struct bin_to_json_stack_entry {
    const struct abi_type* type = nullptr;
    bool allow_extensions = false;
    int position = -1;
    uint32_t array_size = 0;
};

struct json_to_jvalue_state : json_reader_handler<json_to_jvalue_state> {
    std::string& error;
    std::vector<json_to_jvalue_stack_entry> stack;

    json_to_jvalue_state(std::string& error) : error{error} {}
};

struct jvalue_to_bin_state {
    std::string& error;
    std::vector<char>& bin;
    const jvalue* received_value = nullptr;
    std::vector<jvalue_to_bin_stack_entry> stack{};
    bool skipped_extension = false;

    bool get_bool() const { return std::get<bool>(received_value->value); }

    const std::string& get_string() const { return std::get<std::string>(received_value->value); }
};

struct json_to_bin_state : json_reader_handler<json_to_bin_state> {
    std::string& error;
    std::vector<char> bin;
    std::vector<size_insertion> size_insertions{};
    std::vector<json_to_bin_stack_entry> stack{};
    bool skipped_extension = false;

    json_to_bin_state(std::string& error) : error{error} {}
};

struct bin_to_json_state : json_reader_handler<bin_to_json_state> {
    std::string& error;
    eosio::input_stream& bin;
    rapidjson::Writer<rapidjson::StringBuffer>& writer;
    std::vector<bin_to_json_stack_entry> stack{};
    bool skipped_extension = false;

    bin_to_json_state(eosio::input_stream& bin, std::string& error, rapidjson::Writer<rapidjson::StringBuffer>& writer)
        : error{error}, bin{bin}, writer{writer} {}
};

struct abi_serializer {
    ABIEOS_NODISCARD virtual bool json_to_bin(jvalue_to_bin_state& state, bool allow_extensions, const abi_type* type,
                                              event_type event, bool start) const = 0;
    ABIEOS_NODISCARD virtual bool json_to_bin(json_to_bin_state& state, bool allow_extensions, const abi_type* type,
                                              event_type event, bool start) const = 0;
    ABIEOS_NODISCARD virtual bool bin_to_json(bin_to_json_state& state, bool allow_extensions, const abi_type* type,
                                              bool start) const = 0;
};

///////////////////////////////////////////////////////////////////////////////
// serializer function prototypes
///////////////////////////////////////////////////////////////////////////////

ABIEOS_NODISCARD bool json_to_bin(pseudo_optional*, jvalue_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool);
ABIEOS_NODISCARD bool json_to_bin(pseudo_extension*, jvalue_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool);
ABIEOS_NODISCARD bool json_to_bin(pseudo_object*, jvalue_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool start);
ABIEOS_NODISCARD bool json_to_bin(pseudo_array*, jvalue_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool start);
ABIEOS_NODISCARD bool json_to_bin(pseudo_variant*, jvalue_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool start);
template <typename T>
ABIEOS_NODISCARD auto json_to_bin(T*, jvalue_to_bin_state& state, bool allow_extensions, const abi_type*,
                                  event_type event, bool start) -> std::enable_if_t<std::is_arithmetic_v<T>, bool>;
ABIEOS_NODISCARD bool json_to_bin(std::string*, jvalue_to_bin_state& state, bool allow_extensions, const abi_type*,
                                  event_type event, bool start);

template <typename T>
ABIEOS_NODISCARD auto json_to_bin(T*, json_to_bin_state& state, bool allow_extensions, const abi_type*,
                                  event_type event, bool start) -> std::enable_if_t<std::is_arithmetic_v<T>, bool>;
ABIEOS_NODISCARD bool json_to_bin(std::string*, json_to_bin_state& state, bool allow_extensions, const abi_type*,
                                  event_type event, bool start);
ABIEOS_NODISCARD bool json_to_bin(pseudo_optional*, json_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool start);
ABIEOS_NODISCARD bool json_to_bin(pseudo_extension*, json_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool start);
ABIEOS_NODISCARD bool json_to_bin(pseudo_object*, json_to_bin_state& state, bool allow_extensions, const abi_type* type,
                                  event_type event, bool start);
ABIEOS_NODISCARD bool json_to_bin(pseudo_array*, json_to_bin_state& state, bool allow_extensions, const abi_type* type,
                                  event_type event, bool start);
ABIEOS_NODISCARD bool json_to_bin(pseudo_variant*, json_to_bin_state& state, bool allow_extensions,
                                  const abi_type* type, event_type event, bool start);

template <typename T>
ABIEOS_NODISCARD auto bin_to_json(T*, bin_to_json_state& state, bool allow_extensions, const abi_type*, bool start)
    -> std::enable_if_t<std::is_arithmetic_v<T>, bool>;
ABIEOS_NODISCARD bool bin_to_json(std::string*, bin_to_json_state& state, bool allow_extensions, const abi_type*,
                                  bool start);
ABIEOS_NODISCARD bool bin_to_json(pseudo_optional*, bin_to_json_state& state, bool allow_extensions,
                                  const abi_type* type, bool start);
ABIEOS_NODISCARD bool bin_to_json(pseudo_extension*, bin_to_json_state& state, bool allow_extensions,
                                  const abi_type* type, bool start);
ABIEOS_NODISCARD bool bin_to_json(pseudo_object*, bin_to_json_state& state, bool allow_extensions, const abi_type* type,
                                  bool start);
ABIEOS_NODISCARD bool bin_to_json(pseudo_array*, bin_to_json_state& state, bool allow_extensions, const abi_type* type,
                                  bool start);
ABIEOS_NODISCARD bool bin_to_json(pseudo_variant*, bin_to_json_state& state, bool allow_extensions,
                                  const abi_type* type, bool start);

///////////////////////////////////////////////////////////////////////////////
// serializable types
///////////////////////////////////////////////////////////////////////////////

template <typename T, typename State>
ABIEOS_NODISCARD bool json_to_number(T& dest, State& state, event_type event) {
    if (event == event_type::received_bool) {
        dest = state.get_bool();
        return true;
    }
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if constexpr (std::is_integral_v<T>) {
            return decimal_to_binary(dest, state.error, s);
        } else if constexpr (std::is_same_v<T, float>) {
            errno = 0;
            char* end;
            dest = strtof(s.c_str(), &end);
            if (errno || end == s.c_str())
                return set_error(state.error, "number is out of range or has bad format");
            return true;
        } else if constexpr (std::is_same_v<T, double>) {
            errno = 0;
            char* end;
            dest = strtod(s.c_str(), &end);
            if (errno || end == s.c_str())
                return set_error(state.error, "number is out of range or has bad format");
            return true;
        }
    }
    return set_error(state.error, "expected number or boolean");
} // namespace abieos

struct bytes {
    std::vector<char> data;
};

template <typename S>
eosio::result<void> from_bin(bytes& obj, S& stream) {
    return from_bin(obj.data, stream);
}

template <typename S>
eosio::result<void> from_bin(input_buffer& obj, S& stream) {
    uint64_t size;
    auto r = varuint64_from_bin(size, stream);
    if (!r)
        return r;
    const char* buf;
    r = stream.read_reuse_storage(buf, size);
    if (!r)
        return r;
    obj = {buf, buf + size};
    return eosio::outcome::success();
}

template <typename S>
eosio::result<void> to_bin(const bytes& obj, S& stream) {
    return to_bin(obj.data, stream);
}

template <typename S>
eosio::result<void> to_bin(const input_buffer& obj, S& stream) {
    return to_bin(std::string_view{obj.pos, size_t(obj.end - obj.pos)}, stream);
}

template <typename S>
eosio::result<void> from_json(bytes& obj, S& stream) {
    return eosio::from_json_hex(obj.data, stream);
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(bytes*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*sbytes (%d hex digits)\n", int(state.stack.size() * 4), "", int(s.size()));
        if (s.size() & 1)
            return set_error(state, "odd number of hex digits");
        eosio::push_varuint32(state.bin, s.size() / 2);
        return unhex(state.error, s.begin(), s.end(), std::back_inserter(state.bin));
    } else
        return set_error(state, "expected string containing hex digits");
}

ABIEOS_NODISCARD inline bool bin_to_json(bytes*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    uint64_t size;
    auto r = varuint64_from_bin(size, state.bin);
    if (!r)
        return set_error(state, r);
    const char* data;
    r = state.bin.read_reuse_storage(data, size);
    if (!r)
        return set_error(state, r);
    std::string result;
    hex(data, data + size, std::back_inserter(result));
    return state.writer.String(result.c_str(), result.size());
}

template <unsigned size>
struct fixed_binary {
    std::array<uint8_t, size> value{{0}};

    explicit operator std::string() const {
        std::string result;
        hex(value.begin(), value.end(), std::back_inserter(result));
        return result;
    }
};

template <unsigned size>
bool operator==(const fixed_binary<size>& a, const fixed_binary<size>& b) {
    return a.value == b.value;
}

template <unsigned size>
bool operator!=(const fixed_binary<size>& a, const fixed_binary<size>& b) {
    return a.value != b.value;
}

using float128 = fixed_binary<16>;
using checksum160 = fixed_binary<20>;
using checksum256 = fixed_binary<32>;
using checksum512 = fixed_binary<64>;

template <unsigned size, typename S>
eosio::result<void> from_bin(fixed_binary<size>& obj, S& stream) {
    return stream.read(obj.value.data(), size);
}

template <unsigned size, typename S>
eosio::result<void> to_bin(const fixed_binary<size>& obj, S& stream) {
    return stream.write(obj.value.data(), obj.value.size());
}

template <unsigned size, typename S>
eosio::result<void> from_json(fixed_binary<size>& obj, S& stream) {
    std::vector<uint8_t> v;
    auto r = eosio::from_json_hex(v, stream);
    if (!r)
        return r;
    if (v.size() != size)
        return eosio::from_json_error::hex_string_incorrect_length;
    memcpy(obj.value.data(), v.data(), size);
    return eosio::outcome::success();
}

template <typename State, unsigned size>
ABIEOS_NODISCARD bool json_to_bin(fixed_binary<size>*, State& state, bool, const abi_type*, event_type event,
                                  bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*schecksum\n", int(state.stack.size() * 4), "");
        std::vector<uint8_t> v;
        if (!unhex(state.error, s.begin(), s.end(), std::back_inserter(v)))
            return false;
        if (v.size() != size)
            return set_error(state, "hex string has incorrect length");
        state.bin.insert(state.bin.end(), v.begin(), v.end());
        return true;
    } else
        return set_error(state, "expected string containing hex");
}

template <unsigned size>
ABIEOS_NODISCARD inline bool bin_to_json(fixed_binary<size>*, bin_to_json_state& state, bool, const abi_type*,
                                         bool start) {
    fixed_binary<size> v;
    auto r = state.bin.read(v.value.data(), v.value.size());
    if (!r)
        return set_error(state, r);
    std::string result;
    hex(v.value.begin(), v.value.end(), std::back_inserter(result));
    return state.writer.String(result.c_str(), result.size());
}

struct uint128 {
    std::array<uint8_t, 16> value{{0}};

    explicit operator std::string() const { return binary_to_decimal(value); }
};

template <typename S>
eosio::result<void> from_bin(uint128& obj, S& stream) {
    return stream.read(obj.value.data(), obj.value.size());
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(uint128*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*suint128\n", int(state.stack.size() * 4), "");
        std::array<uint8_t, 16> value;
        if (!decimal_to_binary<16>(value, state.error, s))
            return false;
        push_raw(state.bin, value);
        return true;
    } else
        return set_error(state, "expected string containing uint128");
}

ABIEOS_NODISCARD inline bool bin_to_json(uint128*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    uint128 v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    auto result = binary_to_decimal(v.value);
    return state.writer.String(result.c_str(), result.size());
}

struct int128 {
    std::array<uint8_t, 16> value{{0}};

    explicit operator std::string() const {
        auto v = value;
        bool negative = is_negative(v);
        if (negative)
            negate(v);
        auto result = binary_to_decimal(v);
        if (negative)
            result = "-" + result;
        return result;
    }
};

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(int128*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        std::string_view s = state.get_string();
        if (trace_json_to_bin)
            printf("%*sint128\n", int(state.stack.size() * 4), "");
        bool negative = false;
        if (!s.empty() && s[0] == '-') {
            negative = true;
            s = s.substr(1);
        }
        std::array<uint8_t, 16> value;
        if (!decimal_to_binary<16>(value, state.error, s))
            return false;
        if (negative)
            negate(value);
        if (is_negative(value) != negative)
            return set_error(state, "number is out of range");
        push_raw(state.bin, value);
        return true;
    } else
        return set_error(state, "expected string containing int128");
}

ABIEOS_NODISCARD inline bool bin_to_json(int128*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    uint128 v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    bool negative = is_negative(v.value);
    if (negative)
        negate(v.value);
    auto result = binary_to_decimal(v.value);
    if (negative)
        result = "-" + result;
    return state.writer.String(result.c_str(), result.size());
}

template <typename Key, typename S>
eosio::result<void> key_from_bin(Key& obj, S& stream) {
    auto r = stream.read_raw(obj.type);
    if (!r)
        return r;
    if (obj.type == key_type::wa) {
        if constexpr (std::is_same_v<std::decay_t<Key>, public_key>) {
            auto begin = stream.pos;
            r = stream.skip(34);
            if (!r)
                return r;
            uint32_t size;
            r = varuint32_from_bin(size, stream);
            if (!r)
                return r;
            r = stream.skip(size);
            if (!r)
                return r;
            obj.data.resize(stream.pos - begin);
            memcpy(obj.data.data(), begin, obj.data.size());
            return eosio::outcome::success();
        } else if constexpr (std::is_same_v<std::decay_t<Key>, signature>) {
            auto begin = stream.pos;
            r = stream.skip(65);
            if (!r)
                return r;
            uint32_t size;
            r = varuint32_from_bin(size, stream);
            if (!r)
                return r;
            r = stream.skip(size);
            if (!r)
                return r;
            r = varuint32_from_bin(size, stream);
            if (!r)
                return r;
            r = stream.skip(size);
            if (!r)
                return r;
            obj.data.resize(stream.pos - begin);
            memcpy(obj.data.data(), begin, obj.data.size());
            return eosio::outcome::success();
        } else {
            return eosio::stream_error::bad_variant_index;
        }
    } else {
        obj.data.resize(Key::k1r1_size);
        return stream.read(obj.data.data(), obj.data.size());
    }
}

template <typename Key>
inline void key_to_bin(const Key& obj, std::vector<char>& bin) {
    push_raw(bin, obj.type);
    bin.insert(bin.end(), obj.data.begin(), obj.data.end());
}

template <typename Key, typename S>
eosio::result<void> key_to_bin(const Key& obj, S& stream) {
    auto r = eosio::to_bin(obj.type, stream);
    if (!r)
        return r.error();
    return stream.write(obj.data.data(), obj.data.size());
}

template <typename S>
eosio::result<void> from_bin(public_key& obj, S& stream) {
    return key_from_bin(obj, stream);
}

template <typename S>
eosio::result<void> to_bin(const public_key& obj, S& stream) {
    return key_to_bin(obj, stream);
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(public_key*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*spublic_key\n", int(state.stack.size() * 4), "");
        public_key key;
        if (!string_to_public_key(key, state.error, s))
            return false;
        key_to_bin(key, state.bin);
        return true;
    } else
        return set_error(state, "expected string containing public_key");
}

ABIEOS_NODISCARD inline bool bin_to_json(public_key*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    public_key v;
    auto r = key_from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    std::string result;
    if (!public_key_to_string(result, state.error, v))
        return false;
    return state.writer.String(result.c_str(), result.size());
}

template <typename S>
eosio::result<void> from_bin(private_key& obj, S& stream) {
    return key_from_bin(obj, stream);
}

template <typename S>
eosio::result<void> to_bin(const private_key& obj, S& stream) {
    return key_to_bin(obj, stream);
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(private_key*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*sprivate_key\n", int(state.stack.size() * 4), "");
        private_key key;
        if (!string_to_private_key(key, state.error, s))
            return false;
        key_to_bin(key, state.bin);
        return true;
    } else
        return set_error(state, "expected string containing private_key");
}

ABIEOS_NODISCARD inline bool bin_to_json(private_key*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    private_key v;
    auto r = key_from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    std::string result;
    if (!private_key_to_string(result, state.error, v))
        return false;
    return state.writer.String(result.c_str(), result.size());
}

template <typename S>
eosio::result<void> to_bin(const signature& obj, S& stream) {
    return key_to_bin(obj, stream);
}

template <typename S>
eosio::result<void> from_bin(signature& obj, S& stream) {
    return key_from_bin(obj, stream);
}

template <typename S>
eosio::result<void> from_json(signature& obj, S& stream) {
    auto r = stream.get_string();
    if (!r)
        return r.error();
    auto& s = r.value();
    std::string error; // !!!
    if (!string_to_signature(obj, error, r.value()))
        return eosio::from_json_error::invalid_signature;
    return eosio::outcome::success();
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(signature*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*ssignature\n", int(state.stack.size() * 4), "");
        signature key;
        if (!string_to_signature(key, state.error, s))
            return false;
        key_to_bin(key, state.bin);
        return true;
    } else
        return set_error(state, "expected string containing signature");
}

ABIEOS_NODISCARD inline bool bin_to_json(signature*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    signature v;
    auto r = key_from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    std::string result;
    if (!signature_to_string(result, state.error, v))
        return false;
    return state.writer.String(result.c_str(), result.size());
}

struct name {
    uint64_t value = 0;

    constexpr name() = default;
    constexpr explicit name(uint64_t value) : value{value} {}
    constexpr explicit name(const char* str) : value{eosio::string_to_name(str)} {}
    constexpr explicit name(std::string_view str) : value{eosio::string_to_name(str)} {}
    constexpr explicit name(const std::string& str) : value{eosio::string_to_name(str)} {}
    constexpr name(const name&) = default;

    explicit operator std::string() const { return eosio::name_to_string(value); }
};

ABIEOS_NODISCARD inline bool operator==(name a, name b) { return a.value == b.value; }
ABIEOS_NODISCARD inline bool operator!=(name a, name b) { return a.value != b.value; }
ABIEOS_NODISCARD inline bool operator<(name a, name b) { return a.value < b.value; }

template <typename S>
eosio::result<void> from_bin(name& obj, S& stream) {
    return from_bin(obj.value, stream);
}

template <typename S>
eosio::result<void> to_bin(const name& obj, S& stream) {
    return to_bin(obj.value, stream);
}

template <typename S>
eosio::result<void> from_json(name& obj, S& stream) {
    auto r = stream.get_string();
    if (!r)
        return r.error();
    obj.value = eosio::string_to_name(r.value());
    return eosio::outcome::success();
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(name*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        name obj{eosio::string_to_name(state.get_string())};
        if (trace_json_to_bin)
            printf("%*sname: %s (%08llx) %s\n", int(state.stack.size() * 4), "", state.get_string().c_str(),
                   (unsigned long long)obj.value, std::string{obj}.c_str());
        push_raw(state.bin, obj.value);
        return true;
    } else
        return set_error(state, "expected string containing name");
}

ABIEOS_NODISCARD inline bool bin_to_json(name*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    name v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    auto s = std::string{v};
    return state.writer.String(s.c_str(), s.size());
}

struct varuint32 {
    uint32_t value = 0;

    explicit operator std::string() const { return std::to_string(value); }
};

template<typename F>
void convert(const varuint32& src, uint32_t& dst, F&& chooser) {
   dst = src.value;
}

template <typename S>
eosio::result<void> from_bin(varuint32& obj, S& stream) {
    return varuint32_from_bin(obj.value, stream);
}

template <typename S>
eosio::result<void> to_bin(const varuint32& obj, S& stream) {
    return eosio::varuint32_to_bin(obj.value, stream);
}

template <typename S>
eosio::result<void> from_json(varuint32& obj, S& stream) {
    return from_json(obj.value, stream);
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(varuint32*, State& state, bool, const abi_type*, event_type event, bool start) {
    uint32_t x;
    if (!json_to_number(x, state, event))
        return false;
    eosio::push_varuint32(state.bin, x);
    return true;
}

ABIEOS_NODISCARD inline bool bin_to_json(varuint32*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    uint32_t v;
    auto r = varuint32_from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    return state.writer.Uint64(v);
}

struct varint32 {
    int32_t value = 0;

    explicit operator std::string() const { return std::to_string(value); }
};

inline void push_varint32(std::vector<char>& bin, int32_t v) {
    eosio::push_varuint32(bin, (uint32_t(v) << 1) ^ uint32_t(v >> 31));
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(varint32*, State& state, bool, const abi_type*, event_type event, bool start) {
    int32_t x;
    if (!json_to_number(x, state, event))
        return false;
    push_varint32(state.bin, x);
    return true;
}

ABIEOS_NODISCARD inline bool bin_to_json(varint32*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    int32_t v;
    auto r = varint32_from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    return state.writer.Int64(v);
}

struct time_point_sec {
    uint32_t utc_seconds = 0;

    explicit operator std::string() const { return eosio::microseconds_to_str(uint64_t(utc_seconds) * 1'000'000); }
};

ABIEOS_NODISCARD inline bool string_to_time_point_sec(time_point_sec& result, std::string& error, const char* s,
                                                      const char* end) {
    if (eosio::string_to_utc_seconds(result.utc_seconds, s, end, true, true))
        return true;
    else
        return set_error(error, "expected string containing time_point_sec");
}

template <typename S>
eosio::result<void> from_bin(time_point_sec& obj, S& stream) {
    return from_bin(obj.utc_seconds, stream);
}

template <typename S>
eosio::result<void> to_bin(const time_point_sec& obj, S& stream) {
    return to_bin(obj.utc_seconds, stream);
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(time_point_sec*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        time_point_sec obj;
        if (!string_to_time_point_sec(obj, state.error, state.get_string().data(),
                                      state.get_string().data() + state.get_string().size()))
            return false;
        if (trace_json_to_bin)
            printf("%*stime_point_sec: %s (%u) %s\n", int(state.stack.size() * 4), "", state.get_string().c_str(),
                   (unsigned)obj.utc_seconds, std::string{obj}.c_str());
        push_raw(state.bin, obj.utc_seconds);
        return true;
    } else
        return set_error(state, "expected string containing time_point_sec");
}

ABIEOS_NODISCARD inline bool bin_to_json(time_point_sec*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    time_point_sec v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    auto s = std::string{v};
    return state.writer.String(s.c_str(), s.size());
}

struct time_point {
    uint64_t microseconds = 0;

    explicit operator std::string() const { return eosio::microseconds_to_str(microseconds); }
};

ABIEOS_NODISCARD inline bool string_to_time_point(time_point& dest, std::string& error, const std::string& s) {
    if (eosio::string_to_utc_microseconds(dest.microseconds, s.data(), s.data() + s.size()))
        return true;
    else
        return set_error(error, "expected string containing time_point");
}

template <typename S>
eosio::result<void> from_bin(time_point& obj, S& stream) {
    return from_bin(obj.microseconds, stream);
}

template <typename S>
eosio::result<void> to_bin(const time_point& obj, S& stream) {
    return to_bin(obj.microseconds, stream);
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(time_point*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        time_point obj;
        if (!string_to_time_point(obj, state.error, state.get_string()))
            return false;
        if (trace_json_to_bin)
            printf("%*stime_point: %s (%llu) %s\n", int(state.stack.size() * 4), "", state.get_string().c_str(),
                   (unsigned long long)obj.microseconds, std::string{obj}.c_str());
        push_raw(state.bin, obj.microseconds);
        return true;
    } else
        return set_error(state, "expected string containing time_point");
}

ABIEOS_NODISCARD inline bool bin_to_json(time_point*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    time_point v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    auto s = std::string{v};
    return state.writer.String(s.c_str(), s.size());
}

struct block_timestamp {
    static constexpr uint16_t interval_ms = 500;
    static constexpr uint64_t epoch_ms = 946684800000ll; // Year 2000
    uint32_t slot;

    block_timestamp() = default;
    explicit block_timestamp(uint32_t slot) : slot(slot) {}
    explicit block_timestamp(time_point t) { slot = (t.microseconds / 1000 - epoch_ms) / interval_ms; }

    explicit operator time_point() const { return time_point{(slot * (uint64_t)interval_ms + epoch_ms) * 1000}; }
    explicit operator std::string() const { return std::string{(time_point)(*this)}; }
}; // block_timestamp


EOSIO_REFLECT(block_timestamp, slot);

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(block_timestamp*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        time_point tp;
        if (!string_to_time_point(tp, state.error, state.get_string()))
            return false;
        block_timestamp obj{tp};
        if (trace_json_to_bin)
            printf("%*sblock_timestamp: %s (%u) %s\n", int(state.stack.size() * 4), "", state.get_string().c_str(),
                   (unsigned)obj.slot, std::string{obj}.c_str());
        push_raw(state.bin, obj.slot);
        return true;
    } else
        return set_error(state, "expected string containing block_timestamp_type");
}

ABIEOS_NODISCARD inline bool bin_to_json(block_timestamp*, bin_to_json_state& state, bool, const abi_type*,
                                         bool start) {
    uint32_t v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    auto s = std::string{block_timestamp{v}};
    return state.writer.String(s.c_str(), s.size());
}

struct symbol_code {
    uint64_t value = 0;
};

template <typename S>
inline eosio::result<void> from_bin(symbol_code& obj, S& stream) {
    return from_bin(obj.value, stream);
}

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(symbol_code*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*ssymbol_code: %s\n", int(state.stack.size() * 4), "", s.c_str());
        uint64_t v;
        if (!eosio::string_to_symbol_code(v, s.data(), s.data() + s.size()))
            return set_error(state.error, "expected string containing symbol_code");
        push_raw(state.bin, v);
        return true;
    } else
        return set_error(state, "expected string containing symbol_code");
}

ABIEOS_NODISCARD inline bool bin_to_json(symbol_code*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    symbol_code v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    auto result = eosio::symbol_code_to_string(v.value);
    return state.writer.String(result.c_str(), result.size());
}

struct symbol {
    uint64_t value = 0;
};

EOSIO_REFLECT(symbol, value);

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(symbol*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*ssymbol: %s\n", int(state.stack.size() * 4), "", s.c_str());
        uint64_t v;
        if (!eosio::string_to_symbol(v, s.data(), s.data() + s.size()))
            return set_error(state, "expected string containing symbol");
        push_raw(state.bin, v);
        return true;
    } else
        return set_error(state, "expected string containing symbol");
}

ABIEOS_NODISCARD inline bool bin_to_json(symbol*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    uint64_t v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    std::string result{eosio::symbol_to_string(v)};
    return state.writer.String(result.c_str(), result.size());
}

struct asset {
    int64_t amount = 0;
    symbol sym{};
};

EOSIO_REFLECT(asset, amount, sym);

inline eosio::result<void> from_string(asset& result, eosio::input_stream& stream) {
    int64_t amount;
    uint64_t sym;
    if (!eosio::string_to_asset(amount, sym, stream.pos, stream.end, true))
        return eosio::stream_error::invalid_asset_format;
    result = asset{amount, symbol{sym}};
    return eosio::outcome::success();
}

ABIEOS_NODISCARD inline bool string_to_asset(asset& result, std::string& error, const char*& s, const char* end,
                                             bool expect_end) {
    int64_t amount;
    uint64_t sym;
    if (!eosio::string_to_asset(amount, sym, s, end, expect_end))
        return set_error(error, "expected string containing asset");
    result = asset{amount, symbol{sym}};
    return true;
}

ABIEOS_NODISCARD inline bool string_to_asset(asset& result, std::string& error, const char* s, const char* end) {
    return string_to_asset(result, error, s, end, true);
}

inline std::string asset_to_string(const asset& v) { return eosio::asset_to_string(v.amount, v.sym.value); }

template <typename State>
ABIEOS_NODISCARD bool json_to_bin(asset*, State& state, bool, const abi_type*, event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*sasset: %s\n", int(state.stack.size() * 4), "", s.c_str());
        asset v;
        if (!string_to_asset(v, state.error, s.data(), s.data() + s.size()))
            return false;
        push_raw(state.bin, v.amount);
        push_raw(state.bin, v.sym.value);
        return true;
    } else
        return set_error(state, "expected string containing asset");
}

ABIEOS_NODISCARD inline bool bin_to_json(asset*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    asset v{};
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    auto s = asset_to_string(v);
    return state.writer.String(s.c_str(), s.size());
}

///////////////////////////////////////////////////////////////////////////////
// abi types
///////////////////////////////////////////////////////////////////////////////

using extensions_type = std::vector<std::pair<uint16_t, bytes>>;

struct type_def {
    std::string new_type_name{};
    std::string type{};
};

EOSIO_REFLECT(type_def, new_type_name, type);

struct field_def {
    std::string name{};
    std::string type{};
};

EOSIO_REFLECT(field_def, name, type);

struct struct_def {
    std::string name{};
    std::string base{};
    std::vector<field_def> fields{};
};

EOSIO_REFLECT(struct_def, name, base, fields);

struct action_def {
    ::abieos::name name{};
    std::string type{};
    std::string ricardian_contract{};
};

EOSIO_REFLECT(action_def, name, type, ricardian_contract);

struct table_def {
    ::abieos::name name{};
    std::string index_type{};
    std::vector<std::string> key_names{};
    std::vector<std::string> key_types{};
    std::string type{};
};

EOSIO_REFLECT(table_def, name, index_type, key_names, key_types, type);

struct clause_pair {
    std::string id{};
    std::string body{};
};

EOSIO_REFLECT(clause_pair, id, body);

struct error_message {
    uint64_t error_code{};
    std::string error_msg{};
};

EOSIO_REFLECT(error_message, error_code, error_msg);

struct variant_def {
    std::string name{};
    std::vector<std::string> types{};
};

EOSIO_REFLECT(variant_def, name, types);

struct abi_def {
    std::string version{};
    std::vector<type_def> types{};
    std::vector<struct_def> structs{};
    std::vector<action_def> actions{};
    std::vector<table_def> tables{};
    std::vector<clause_pair> ricardian_clauses{};
    std::vector<error_message> error_messages{};
    extensions_type abi_extensions{};
    might_not_exist<std::vector<variant_def>> variants{};
};

EOSIO_REFLECT(abi_def, version, types, structs, actions, tables, ricardian_clauses, error_messages, abi_extensions,
              variants);

ABIEOS_NODISCARD inline bool check_abi_version(const std::string& s, std::string& error) {
    if (s.substr(0, 13) != "eosio::abi/1.")
        return set_error(error, "unsupported abi version");
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// json_to_jvalue
///////////////////////////////////////////////////////////////////////////////

ABIEOS_NODISCARD bool json_to_jobject(jvalue& value, json_to_jvalue_state& state, event_type event, bool start);
ABIEOS_NODISCARD bool json_to_jarray(jvalue& value, json_to_jvalue_state& state, event_type event, bool start);

ABIEOS_NODISCARD inline bool receive_event(struct json_to_jvalue_state& state, event_type event, bool start) {
    if (state.stack.empty())
        return set_error(state, "extra data");
    if (state.stack.size() > max_stack_size)
        return set_error(state, "recursion limit reached");
    if (trace_json_to_jvalue_event)
        printf("(event %d)\n", (int)event);
    auto& v = *state.stack.back().value;
    if (start) {
        state.stack.pop_back();
        if (event == event_type::received_null) {
            v.value = nullptr;
        } else if (event == event_type::received_bool) {
            v.value = state.get_bool();
        } else if (event == event_type::received_string) {
            v.value = std::move(state.get_string());
        } else if (event == event_type::received_start_object) {
            v.value = jobject{};
            return json_to_jobject(v, state, event, start);
        } else if (event == event_type::received_start_array) {
            v.value = jarray{};
            return json_to_jarray(v, state, event, start);
        } else {
            return false;
        }
    } else {
        if (std::holds_alternative<jobject>(v.value))
            return json_to_jobject(v, state, event, start);
        else if (std::holds_alternative<jarray>(v.value))
            return json_to_jarray(v, state, event, start);
        else
            return set_error(state, "extra data");
    }
    return true;
}

ABIEOS_NODISCARD inline bool json_to_jvalue(jvalue& value, std::string& error, std::string_view json) {
    std::string mutable_json{json};
    mutable_json.push_back(0);
    json_to_jvalue_state state{error};
    state.stack.push_back({&value});
    rapidjson::Reader reader;
    rapidjson::InsituStringStream ss(mutable_json.data());
    return reader.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag |
                        rapidjson::kParseNumbersAsStringsFlag>(ss, state);
}

ABIEOS_NODISCARD inline bool json_to_jobject(jvalue& value, json_to_jvalue_state& state, event_type event, bool start) {
    if (start) {
        if (event != event_type::received_start_object)
            return set_error(state, "expected object");
        if (trace_json_to_jvalue)
            printf("%*s{\n", int(state.stack.size() * 4), "");
        state.stack.push_back({&value});
        return true;
    } else if (event == event_type::received_end_object) {
        if (trace_json_to_jvalue)
            printf("%*s}\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        return true;
    }
    auto& stack_entry = state.stack.back();
    if (event == event_type::received_key) {
        stack_entry.key = std::move(state.received_data.key);
        return true;
    } else {
        if (trace_json_to_jvalue)
            printf("%*sfield %s (event %d)\n", int(state.stack.size() * 4), "", stack_entry.key.c_str(), (int)event);
        auto& x = std::get<jobject>(value.value)[stack_entry.key] = {};
        state.stack.push_back({&x});
        return receive_event(state, event, true);
    }
}

ABIEOS_NODISCARD inline bool json_to_jarray(jvalue& value, json_to_jvalue_state& state, event_type event, bool start) {
    if (start) {
        if (event != event_type::received_start_array)
            return set_error(state, "expected array");
        if (trace_json_to_jvalue)
            printf("%*s[\n", int(state.stack.size() * 4), "");
        state.stack.push_back({&value});
        return true;
    } else if (event == event_type::received_end_array) {
        if (trace_json_to_jvalue)
            printf("%*s]\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        return true;
    }
    auto& v = std::get<jarray>(value.value);
    if (trace_json_to_jvalue)
        printf("%*sitem %d (event %d)\n", int(state.stack.size() * 4), "", int(v.size()), (int)event);
    v.emplace_back();
    state.stack.push_back({&v.back()});
    return receive_event(state, event, true);
}

///////////////////////////////////////////////////////////////////////////////
// abi serializer implementations
///////////////////////////////////////////////////////////////////////////////

template <typename F>
constexpr void for_each_abi_type(F f) {
    static_assert(sizeof(float) == 4);
    static_assert(sizeof(double) == 8);

    f("bool", (bool*)nullptr);
    f("int8", (int8_t*)nullptr);
    f("uint8", (uint8_t*)nullptr);
    f("int16", (int16_t*)nullptr);
    f("uint16", (uint16_t*)nullptr);
    f("int32", (int32_t*)nullptr);
    f("uint32", (uint32_t*)nullptr);
    f("int64", (int64_t*)nullptr);
    f("uint64", (uint64_t*)nullptr);
    f("int128", (int128*)nullptr);
    f("uint128", (uint128*)nullptr);
    f("varuint32", (varuint32*)nullptr);
    f("varint32", (varint32*)nullptr);
    f("float32", (float*)nullptr);
    f("float64", (double*)nullptr);
    f("float128", (float128*)nullptr);
    f("time_point", (time_point*)nullptr);
    f("time_point_sec", (time_point_sec*)nullptr);
    f("block_timestamp_type", (block_timestamp*)nullptr);
    f("name", (name*)nullptr);
    f("bytes", (bytes*)nullptr);
    f("string", (std::string*)nullptr);
    f("checksum160", (checksum160*)nullptr);
    f("checksum256", (checksum256*)nullptr);
    f("checksum512", (checksum512*)nullptr);
    f("public_key", (public_key*)nullptr);
    f("private_key", (private_key*)nullptr);
    f("signature", (signature*)nullptr);
    f("symbol", (symbol*)nullptr);
    f("symbol_code", (symbol_code*)nullptr);
    f("asset", (asset*)nullptr);
}

template <typename T>
struct abi_serializer_impl : abi_serializer {
    ABIEOS_NODISCARD bool json_to_bin(jvalue_to_bin_state& state, bool allow_extensions, const abi_type* type,
                                      event_type event, bool start) const override {
        return ::abieos::json_to_bin((T*)nullptr, state, allow_extensions, type, event, start);
    }
    ABIEOS_NODISCARD bool json_to_bin(json_to_bin_state& state, bool allow_extensions, const abi_type* type,
                                      event_type event, bool start) const override {
        return ::abieos::json_to_bin((T*)nullptr, state, allow_extensions, type, event, start);
    }
    ABIEOS_NODISCARD bool bin_to_json(bin_to_json_state& state, bool allow_extensions, const abi_type* type,
                                      bool start) const override {
        return ::abieos::bin_to_json((T*)nullptr, state, allow_extensions, type, start);
    }
};

template <typename T>
inline constexpr auto abi_serializer_for = abi_serializer_impl<T>{};

///////////////////////////////////////////////////////////////////////////////
// abi handling
///////////////////////////////////////////////////////////////////////////////

struct abi_field {
    std::string name{};
    const struct abi_type* type{};
};

struct abi_type {
    std::string name{};
    std::string alias_of_name{};
    const ::abieos::struct_def* struct_def{};
    const ::abieos::variant_def* variant_def{};
    abi_type* alias_of{};
    abi_type* optional_of{};
    abi_type* extension_of{};
    abi_type* array_of{};
    abi_type* base{};
    std::vector<abi_field> fields{};
    bool filled_struct{};
    bool filled_variant{};
    const abi_serializer* ser{};

    abi_type(std::string name = "", std::string alias_of_name = "")
        : name{std::move(name)}, alias_of_name{std::move(alias_of_name)} {}
    abi_type(const abi_type&) = delete;
    abi_type(abi_type&&) = delete;
    abi_type& operator=(const abi_type&) = delete;
    abi_type& operator=(abi_type&&) = delete;
};

struct contract {
    std::map<name, std::string> action_types;
    std::map<name, std::string> table_types;
    std::map<std::string, abi_type> abi_types;
};

template <int i>
bool ends_with(const std::string& s, const char (&suffix)[i]) {
    return s.size() >= i - 1 && !strcmp(s.c_str() + s.size() - (i - 1), suffix);
}

ABIEOS_NODISCARD inline bool get_type(abi_type*& result, std::string& error, std::map<std::string, abi_type>& abi_types,
                                      const std::string& name, int depth) {
    if (depth >= 32)
        return set_error(error, "abi recursion limit reached");
    auto it = abi_types.find(name);
    if (it == abi_types.end()) {
        if (ends_with(name, "?")) {
            abi_type& type = abi_types[name];
            type.name = name;
            if (!get_type(type.optional_of, error, abi_types, name.substr(0, name.size() - 1), depth + 1))
                return false;
            if (type.optional_of->optional_of || type.optional_of->array_of)
                return set_error(error, "optional (?) and array ([]) don't support nesting");
            if (type.optional_of->extension_of)
                return set_error(error, "optional (?) may not contain binary extensions ($)");
            type.ser = &abi_serializer_for<pseudo_optional>;
            result = &type;
            return true;
        } else if (ends_with(name, "[]")) {
            abi_type& type = abi_types[name];
            type.name = name;
            if (!get_type(type.array_of, error, abi_types, name.substr(0, name.size() - 2), depth + 1))
                return false;
            if (type.array_of->array_of || type.array_of->optional_of)
                return set_error(error, "optional (?) and array ([]) don't support nesting");
            if (type.array_of->extension_of)
                return set_error(error, "array ([]) may not contain binary extensions ($)");
            type.ser = &abi_serializer_for<pseudo_array>;
            result = &type;
            return true;
        } else if (ends_with(name, "$")) {
            abi_type& type = abi_types[name];
            type.name = name;
            if (!get_type(type.extension_of, error, abi_types, name.substr(0, name.size() - 1), depth + 1))
                return false;
            if (type.extension_of->extension_of)
                return set_error(error, "binary extensions ($) may not contain binary extensions ($)");
            type.ser = &abi_serializer_for<pseudo_extension>;
            result = &type;
            return true;
        } else
            return set_error(error, "unknown type \"" + name + "\"");
    }
    if (it->second.alias_of) {
        result = it->second.alias_of;
        return true;
    }
    if (it->second.alias_of_name.empty()) {
        result = &it->second;
        return true;
    }
    if (!get_type(result, error, abi_types, it->second.alias_of_name, depth + 1))
        return false;
    it->second.alias_of = result;
    return true;
}

ABIEOS_NODISCARD inline bool fill_struct(std::map<std::string, abi_type>& abi_types, std::string& error, abi_type& type,
                                         int depth) {
    if (depth >= 32)
        return set_error(error, "abi recursion limit reached");
    if (type.filled_struct)
        return true;
    if (!type.struct_def)
        return set_error(error, "abi type \"" + type.name + "\" is not a struct");
    if (!type.struct_def->base.empty()) {
        abi_type* t;
        if (!get_type(t, error, abi_types, type.struct_def->base, depth + 1))
            return false;
        if (!fill_struct(abi_types, error, *t, depth + 1))
            return false;
        type.fields = t->fields;
    }
    for (auto& field : type.struct_def->fields) {
        abi_type* t;
        if (!get_type(t, error, abi_types, field.type, depth + 1))
            return false;
        type.fields.push_back(abi_field{field.name, t});
    }
    type.filled_struct = true;
    return true;
}

ABIEOS_NODISCARD inline bool fill_variant(std::map<std::string, abi_type>& abi_types, std::string& error,
                                          abi_type& type, int depth) {
    if (depth >= 32)
        return set_error(error, "abi recursion limit reached");
    if (type.filled_variant)
        return true;
    if (!type.variant_def)
        return set_error(error, "abi type \"" + type.name + "\" is not a variant");
    for (auto& types : type.variant_def->types) {
        abi_type* t;
        if (!get_type(t, error, abi_types, types, depth + 1))
            return false;
        type.fields.push_back(abi_field{types, t});
    }
    type.filled_variant = true;
    return true;
}

ABIEOS_NODISCARD inline bool fill_contract(contract& c, std::string& error, const abi_def& abi) {
    for (auto& a : abi.actions)
        c.action_types[a.name] = a.type;
    for (auto& t : abi.tables)
        c.table_types[t.name] = t.type;
    for_each_abi_type([&](const char* name, auto* p) {
        auto& type = c.abi_types[name];
        type.name = name;
        type.ser = &abi_serializer_for<std::decay_t<decltype(*p)>>;
    });
    {
        auto& type = c.abi_types["extended_asset"];
        type.name = "extended_asset";
        abi_type *asset_type, *name_type;
        if (!get_type(asset_type, error, c.abi_types, "asset", 0) ||
            !get_type(name_type, error, c.abi_types, "name", 0))
            return false;
        type.fields.push_back(abi_field{"quantity", asset_type});
        type.fields.push_back(abi_field{"contract", name_type});
        type.filled_struct = true;
        type.ser = &abi_serializer_for<pseudo_object>;
    }

    for (auto& t : abi.types) {
        if (t.new_type_name.empty())
            return set_error(error, "abi has a type with a missing name");
        auto [_, inserted] = c.abi_types.try_emplace(t.new_type_name, t.new_type_name, t.type);
        if (!inserted)
            return set_error(error, "abi redefines type \"" + t.new_type_name + "\"");
    }
    for (auto& s : abi.structs) {
        if (s.name.empty())
            return set_error(error, "abi has a struct with a missing name");
        auto [it, inserted] = c.abi_types.try_emplace(s.name, s.name);
        if (!inserted)
            return set_error(error, "abi redefines type \"" + s.name + "\"");
        it->second.struct_def = &s;
        it->second.ser = &abi_serializer_for<pseudo_object>;
    }
    for (auto& v : abi.variants.value) {
        if (v.name.empty())
            return set_error(error, "abi has a variant with a missing name");
        auto [it, inserted] = c.abi_types.try_emplace(v.name, v.name);
        if (!inserted)
            return set_error(error, "abi redefines type \"" + v.name + "\"");
        it->second.variant_def = &v;
        it->second.ser = &abi_serializer_for<pseudo_variant>;
    }
    for (auto& [_, t] : c.abi_types)
        if (!t.alias_of_name.empty())
            if (!get_type(t.alias_of, error, c.abi_types, t.alias_of_name, 0))
                return false;
    for (auto& [_, t] : c.abi_types) {
        if (t.struct_def) {
            if (!fill_struct(c.abi_types, error, t, 0))
                return false;
        } else if (t.variant_def) {
            if (!fill_variant(c.abi_types, error, t, 0))
                return false;
        }
    }
    for (auto& [_, t] : c.abi_types) {
        t.struct_def = nullptr;
        t.variant_def = nullptr;
        if (t.alias_of && t.alias_of->extension_of)
            return set_error(error, "can't use extensions ($) within typedefs");
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// json_to_bin (jvalue)
///////////////////////////////////////////////////////////////////////////////

ABIEOS_NODISCARD inline bool json_to_bin(std::vector<char>& bin, std::string& error, const abi_type* type,
                                         const jvalue& value) {
    jvalue_to_bin_state state{error, bin, &value};
    bool result = [&] {
        if (!type->ser->json_to_bin(state, true, type, get_event_type(value), true))
            return false;
        while (!state.stack.empty()) {
            auto& entry = state.stack.back();
            if (!entry.type->ser->json_to_bin(state, entry.allow_extensions, entry.type, get_event_type(*entry.value),
                                              false))
                return false;
        }
        return true;
    }();
    if (result)
        return true;
    std::string s;
    if (!state.stack.empty() && state.stack[0].type->filled_struct)
        s += state.stack[0].type->name;
    for (auto& entry : state.stack) {
        if (entry.type->array_of)
            s += "[" + std::to_string(entry.position) + "]";
        else if (entry.type->filled_struct) {
            if (entry.position >= 0 && entry.position < (int)entry.type->fields.size())
                s += "." + entry.type->fields[entry.position].name;
        } else if (entry.type->optional_of) {
            s += "<optional>";
        } else if (entry.type->filled_variant) {
            s += "<variant>";
        } else {
            s += "<?>";
        }
    }
    if (!s.empty())
        s += ": ";
    error = s + error;
    return false;
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_optional*, jvalue_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool) {
    if (event == event_type::received_null) {
        state.bin.push_back(0);
        return true;
    }
    state.bin.push_back(1);
    return type->optional_of->ser &&
           type->optional_of->ser->json_to_bin(state, allow_extensions, type->optional_of, event, true);
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_extension*, jvalue_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool) {
    return type->extension_of->ser &&
           type->extension_of->ser->json_to_bin(state, allow_extensions, type->extension_of, event, true);
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_object*, jvalue_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool start) {
    if (start) {
        if (!state.received_value || !std::holds_alternative<jobject>(state.received_value->value))
            return set_error(state.error, "expected object");
        if (trace_jvalue_to_bin)
            printf("%*s{ %d fields, allow_ex=%d\n", int(state.stack.size() * 4), "", int(type->fields.size()),
                   allow_extensions);
        state.stack.push_back({type, allow_extensions, state.received_value, -1});
        return true;
    }
    auto& stack_entry = state.stack.back();
    ++stack_entry.position;
    if (stack_entry.position == (int)type->fields.size()) {
        if (trace_jvalue_to_bin)
            printf("%*s}\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        return true;
    }
    auto& field = stack_entry.type->fields[stack_entry.position];
    auto& obj = std::get<jobject>(stack_entry.value->value);
    auto it = obj.find(field.name);
    if (trace_jvalue_to_bin)
        printf("%*sfield %d/%d: %s (event %d)\n", int(state.stack.size() * 4), "", int(stack_entry.position),
               int(type->fields.size()), std::string{field.name}.c_str(), (int)event);
    if (it == obj.end()) {
        if (field.type->extension_of && allow_extensions) {
            state.skipped_extension = true;
            return true;
        }
        stack_entry.position = -1;
        return set_error(state.error, "expected field \"" + field.name + "\"");
    }
    if (state.skipped_extension)
        return set_error(state.error, "unexpected field \"" + field.name + "\"");
    state.received_value = &it->second;
    return field.type->ser && field.type->ser->json_to_bin(state, allow_extensions && &field == &type->fields.back(),
                                                           field.type, get_event_type(it->second), true);
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_array*, jvalue_to_bin_state& state, bool, const abi_type* type,
                                         event_type event, bool start) {
    if (start) {
        if (!state.received_value || !std::holds_alternative<jarray>(state.received_value->value))
            return set_error(state.error, "expected array");
        if (trace_jvalue_to_bin)
            printf("%*s[ %d elements\n", int(state.stack.size() * 4), "",
                   int(std::get<jarray>(state.received_value->value).size()));
        eosio::push_varuint32(state.bin, std::get<jarray>(state.received_value->value).size());
        state.stack.push_back({type, false, state.received_value, -1});
        return true;
    }
    auto& stack_entry = state.stack.back();
    auto& arr = std::get<jarray>(stack_entry.value->value);
    ++stack_entry.position;
    if (stack_entry.position == (int)arr.size()) {
        if (trace_jvalue_to_bin)
            printf("%*s]\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        return true;
    }
    state.received_value = &arr[stack_entry.position];
    if (trace_jvalue_to_bin)
        printf("%*sitem (event %d)\n", int(state.stack.size() * 4), "", (int)event);
    return type->array_of->ser &&
           type->array_of->ser->json_to_bin(state, false, type->array_of, get_event_type(*state.received_value), true);
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_variant*, jvalue_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool start) {
    if (start) {
        if (!state.received_value || !std::holds_alternative<jarray>(state.received_value->value))
            return set_error(state.error, R"(expected variant: ["type", value])");
        auto& arr = std::get<jarray>(state.received_value->value);
        if (arr.size() != 2)
            return set_error(state.error, R"(expected variant: ["type", value])");
        if (!std::holds_alternative<std::string>(arr[0].value))
            return set_error(state.error, R"(expected variant: ["type", value])");
        auto& typeName = std::get<std::string>(arr[0].value);
        if (trace_jvalue_to_bin)
            printf("%*s[ variant %s\n", int(state.stack.size() * 4), "", typeName.c_str());
        state.stack.push_back({type, allow_extensions, state.received_value, 0});
        return true;
    }
    auto& stack_entry = state.stack.back();
    auto& arr = std::get<jarray>(stack_entry.value->value);
    if (stack_entry.position == 0) {
        auto& typeName = std::get<std::string>(arr[0].value);
        auto it = std::find_if(stack_entry.type->fields.begin(), stack_entry.type->fields.end(),
                               [&](auto& field) { return field.name == typeName; });
        if (it == stack_entry.type->fields.end())
            return set_error(state.error, "type is not valid for this variant");
        eosio::push_varuint32(state.bin, it - stack_entry.type->fields.begin());
        state.received_value = &arr[++stack_entry.position];
        return it->type->ser && it->type->ser->json_to_bin(state, allow_extensions, it->type,
                                                           get_event_type(*state.received_value), true);
    } else {
        if (trace_jvalue_to_bin)
            printf("%*s]\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        return true;
    }
}

template <typename T>
ABIEOS_NODISCARD auto json_to_bin(T*, jvalue_to_bin_state& state, bool, const abi_type*, event_type event, bool start)
    -> std::enable_if_t<std::is_arithmetic_v<T>, bool> {

    T x;
    if (!json_to_number(x, state, event))
        return false;
    push_raw(state.bin, x);
    return true;
}

ABIEOS_NODISCARD inline bool json_to_bin(std::string*, jvalue_to_bin_state& state, bool, const abi_type*,
                                         event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_jvalue_to_bin)
            printf("%*sstring: %s\n", int(state.stack.size() * 4), "", s.c_str());
        eosio::push_varuint32(state.bin, s.size());
        state.bin.insert(state.bin.end(), s.begin(), s.end());
        return true;
    } else
        return set_error(state.error, "expected string");
}

///////////////////////////////////////////////////////////////////////////////
// json_to_bin
///////////////////////////////////////////////////////////////////////////////

ABIEOS_NODISCARD inline bool receive_event(struct json_to_bin_state& state, event_type event, bool start) {
    if (state.stack.empty())
        return false;
    if (trace_json_to_bin_event)
        printf("(event %d %d)\n", (int)event, start);
    auto entry = state.stack.back();
    auto* type = entry.type;
    if (start)
        state.stack.clear();
    if (state.stack.size() > max_stack_size)
        return set_error(state, "recursion limit reached");
    return type->ser && type->ser->json_to_bin(state, entry.allow_extensions, type, event, start);
}

ABIEOS_NODISCARD inline bool json_to_bin(std::vector<char>& bin, std::string& error, const abi_type* type,
                                         std::string_view json) {
    std::string mutable_json{json};
    mutable_json.push_back(0);
    json_to_bin_state state{error};
    state.stack.push_back({type, true});
    rapidjson::Reader reader;
    rapidjson::InsituStringStream ss(mutable_json.data());

    if (!reader.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag |
                      rapidjson::kParseNumbersAsStringsFlag>(ss, state)) {
        if (error.empty())
            error = "failed to parse";
        std::string s;
        if (!state.stack.empty() && state.stack[0].type->filled_struct)
            s += state.stack[0].type->name;
        for (auto& entry : state.stack) {
            if (entry.type->array_of)
                s += "[" + std::to_string(entry.position) + "]";
            else if (entry.type->filled_struct) {
                if (entry.position >= 0 && entry.position < (int)entry.type->fields.size())
                    s += "." + entry.type->fields[entry.position].name;
            } else if (entry.type->optional_of) {
                s += "<optional>";
            } else if (entry.type->filled_variant) {
                s += "<variant>";
            } else {
                s += "<?>";
            }
        }
        if (!s.empty())
            s += ": ";
        error = s + error;
        return false;
    }

    size_t pos = 0;
    for (auto& insertion : state.size_insertions) {
        bin.insert(bin.end(), state.bin.begin() + pos, state.bin.begin() + insertion.position);
        eosio::push_varuint32(bin, insertion.size);
        pos = insertion.position;
    }
    bin.insert(bin.end(), state.bin.begin() + pos, state.bin.end());
    return true;
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_optional*, json_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool) {
    if (event == event_type::received_null) {
        state.bin.push_back(0);
        return true;
    }
    state.bin.push_back(1);
    return type->optional_of->ser &&
           type->optional_of->ser->json_to_bin(state, allow_extensions, type->optional_of, event, true);
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_extension*, json_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool) {
    return type->extension_of->ser &&
           type->extension_of->ser->json_to_bin(state, allow_extensions, type->extension_of, event, true);
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_object*, json_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool start) {
    if (start) {
        if (event != event_type::received_start_object)
            return set_error(state, "expected object");
        if (trace_json_to_bin)
            printf("%*s{ %d fields, allow_ex=%d\n", int(state.stack.size() * 4), "", int(type->fields.size()),
                   allow_extensions);
        state.stack.push_back({type, allow_extensions});
        return true;
    }
    auto& stack_entry = state.stack.back();
    if (event == event_type::received_end_object) {
        if (stack_entry.position + 1 != (ptrdiff_t)type->fields.size()) {
            auto& field = type->fields[stack_entry.position + 1];
            if (!field.type->extension_of || !allow_extensions) {
                stack_entry.position = -1;
                return set_error(state, "expected field \"" + field.name + "\"");
            }
            ++stack_entry.position;
            state.skipped_extension = true;
            return true;
        }
        if (trace_json_to_bin)
            printf("%*s}\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        return true;
    }
    if (event == event_type::received_key) {
        if (++stack_entry.position >= (ptrdiff_t)type->fields.size() || state.skipped_extension)
            return set_error(state, "unexpected field \"" + state.received_data.key + "\"");
        auto& field = type->fields[stack_entry.position];
        if (state.received_data.key != field.name) {
            stack_entry.position = -1;
            return set_error(state, "expected field \"" + field.name + "\"");
        }
        return true;
    } else {
        auto& field = type->fields[stack_entry.position];
        if (trace_json_to_bin)
            printf("%*sfield %d/%d: %s (event %d)\n", int(state.stack.size() * 4), "", int(stack_entry.position),
                   int(type->fields.size()), std::string{field.name}.c_str(), (int)event);
        return field.type->ser &&
               field.type->ser->json_to_bin(state, allow_extensions && &field == &type->fields.back(), field.type,
                                            event, true);
    }
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_array*, json_to_bin_state& state, bool, const abi_type* type,
                                         event_type event, bool start) {
    if (start) {
        if (event != event_type::received_start_array)
            return set_error(state, "expected array");
        if (trace_json_to_bin)
            printf("%*s[\n", int(state.stack.size() * 4), "");
        state.stack.push_back({type, false});
        state.stack.back().size_insertion_index = state.size_insertions.size();
        state.size_insertions.push_back({state.bin.size()});
        return true;
    }
    auto& stack_entry = state.stack.back();
    if (event == event_type::received_end_array) {
        if (trace_json_to_bin)
            printf("%*s]\n", int((state.stack.size() - 1) * 4), "");
        state.size_insertions[stack_entry.size_insertion_index].size = stack_entry.position + 1;
        state.stack.pop_back();
        return true;
    }
    ++stack_entry.position;
    if (trace_json_to_bin)
        printf("%*sitem (event %d)\n", int(state.stack.size() * 4), "", (int)event);
    return type->array_of->ser && type->array_of->ser->json_to_bin(state, false, type->array_of, event, true);
}

ABIEOS_NODISCARD inline bool json_to_bin(pseudo_variant*, json_to_bin_state& state, bool allow_extensions,
                                         const abi_type* type, event_type event, bool start) {
    if (start) {
        if (event != event_type::received_start_array)
            return set_error(state, R"(expected variant: ["type", value])");
        if (trace_json_to_bin)
            printf("%*s[ variant\n", int(state.stack.size() * 4), "");
        state.stack.push_back({type, allow_extensions});
        return true;
    }
    auto& stack_entry = state.stack.back();
    ++stack_entry.position;
    if (event == event_type::received_end_array) {
        if (stack_entry.position != 2)
            return set_error(state, R"(expected variant: ["type", value])");
        if (trace_json_to_bin)
            printf("%*s]\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        return true;
    }
    if (stack_entry.position == 0) {
        if (event == event_type::received_string) {
            auto& typeName = state.get_string();
            if (trace_json_to_bin)
                printf("%*stype: %s\n", int(state.stack.size() * 4), "", typeName.c_str());
            auto it = std::find_if(stack_entry.type->fields.begin(), stack_entry.type->fields.end(),
                                   [&](auto& field) { return field.name == typeName; });
            if (it == stack_entry.type->fields.end())
                return set_error(state, "type is not valid for this variant");
            stack_entry.variant_type_index = it - stack_entry.type->fields.begin();
            eosio::push_varuint32(state.bin, stack_entry.variant_type_index);
            return true;
        } else
            return set_error(state, R"(expected variant: ["type", value])");
    } else if (stack_entry.position == 1) {
        auto& field = stack_entry.type->fields[stack_entry.variant_type_index];
        return field.type->ser && field.type->ser->json_to_bin(state, allow_extensions, field.type, event, true);
    } else {
        return set_error(state, R"(expected variant: ["type", value])");
    }
}

template <typename T>
ABIEOS_NODISCARD auto json_to_bin(T*, json_to_bin_state& state, bool, const abi_type*, event_type event, bool start)
    -> std::enable_if_t<std::is_arithmetic_v<T>, bool> {

    T x;
    if (!json_to_number(x, state, event))
        return false;
    push_raw(state.bin, x);
    return true;
}

ABIEOS_NODISCARD inline bool json_to_bin(std::string*, json_to_bin_state& state, bool, const abi_type*,
                                         event_type event, bool start) {
    if (event == event_type::received_string) {
        auto& s = state.get_string();
        if (trace_json_to_bin)
            printf("%*sstring: %s\n", int(state.stack.size() * 4), "", s.c_str());
        eosio::push_varuint32(state.bin, s.size());
        state.bin.insert(state.bin.end(), s.begin(), s.end());
        return true;
    } else
        return set_error(state, "expected string");
}

///////////////////////////////////////////////////////////////////////////////
// bin_to_json
///////////////////////////////////////////////////////////////////////////////

ABIEOS_NODISCARD inline bool bin_to_json(eosio::input_stream& bin, std::string& error, const abi_type* type,
                                         std::string& dest) {
    if (!type->ser)
        return false;
    rapidjson::StringBuffer buffer{};
    rapidjson::Writer<rapidjson::StringBuffer> writer{buffer};
    bin_to_json_state state{bin, error, writer};
    if (!type->ser || !type->ser->bin_to_json(state, true, type, true))
        return false;
    while (!state.stack.empty()) {
        auto& entry = state.stack.back();
        if (!entry.type->ser || !entry.type->ser->bin_to_json(state, entry.allow_extensions, entry.type, false))
            return false;
        if (state.stack.size() > max_stack_size)
            return set_error(state, "recursion limit reached");
    }
    dest = buffer.GetString();
    return true;
}

ABIEOS_NODISCARD inline bool bin_to_json(pseudo_optional*, bin_to_json_state& state, bool allow_extensions,
                                         const abi_type* type, bool) {
    bool present;
    auto r = from_bin(present, state.bin);
    if (!r)
        return set_error(state, r);
    if (present)
        return type->optional_of->ser &&
               type->optional_of->ser->bin_to_json(state, allow_extensions, type->optional_of, true);
    state.writer.Null();
    return true;
}

ABIEOS_NODISCARD inline bool bin_to_json(pseudo_extension*, bin_to_json_state& state, bool allow_extensions,
                                         const abi_type* type, bool) {
    return type->extension_of->ser &&
           type->extension_of->ser->bin_to_json(state, allow_extensions, type->extension_of, true);
}

ABIEOS_NODISCARD inline bool bin_to_json(pseudo_object*, bin_to_json_state& state, bool allow_extensions,
                                         const abi_type* type, bool start) {
    if (start) {
        if (trace_bin_to_json)
            printf("%*s{ %d fields\n", int(state.stack.size() * 4), "", int(type->fields.size()));
        state.stack.push_back({type, allow_extensions});
        state.writer.StartObject();
        return true;
    }
    auto& stack_entry = state.stack.back();
    if (++stack_entry.position < (ptrdiff_t)type->fields.size()) {
        auto& field = type->fields[stack_entry.position];
        if (trace_bin_to_json)
            printf("%*sfield %d/%d: %s\n", int(state.stack.size() * 4), "", int(stack_entry.position),
                   int(type->fields.size()), std::string{field.name}.c_str());
        if (state.bin.pos == state.bin.end && field.type->extension_of && allow_extensions) {
            state.skipped_extension = true;
            return true;
        }
        state.writer.Key(field.name.c_str(), field.name.length());
        return field.type->ser && field.type->ser->bin_to_json(
                                      state, allow_extensions && &field == &type->fields.back(), field.type, true);
    } else {
        if (trace_bin_to_json)
            printf("%*s}\n", int((state.stack.size() - 1) * 4), "");
        state.stack.pop_back();
        state.writer.EndObject();
        return true;
    }
}

ABIEOS_NODISCARD inline bool bin_to_json(pseudo_array*, bin_to_json_state& state, bool, const abi_type* type,
                                         bool start) {
    if (start) {
        state.stack.push_back({type, false});
        auto r = varuint32_from_bin(state.stack.back().array_size, state.bin);
        if (!r)
            return set_error(state, r);
        if (trace_bin_to_json)
            printf("%*s[ %d items\n", int(state.stack.size() * 4), "", int(state.stack.back().array_size));
        state.writer.StartArray();
        return true;
    }
    auto& stack_entry = state.stack.back();
    if (++stack_entry.position < (ptrdiff_t)stack_entry.array_size) {
        if (trace_bin_to_json)
            printf("%*sitem %d/%d %p %s\n", int(state.stack.size() * 4), "", int(stack_entry.position),
                   int(stack_entry.array_size), type->array_of->ser, type->array_of->name.c_str());
        return type->array_of->ser && type->array_of->ser->bin_to_json(state, false, type->array_of, true);
    } else {
        if (trace_bin_to_json)
            printf("%*s]\n", int((state.stack.size()) * 4), "");
        state.stack.pop_back();
        state.writer.EndArray();
        return true;
    }
}

ABIEOS_NODISCARD inline bool bin_to_json(pseudo_variant*, bin_to_json_state& state, bool allow_extensions,
                                         const abi_type* type, bool start) {
    if (start) {
        state.stack.push_back({type, allow_extensions});
        if (trace_bin_to_json)
            printf("%*s[ variant\n", int(state.stack.size() * 4), "");
        state.writer.StartArray();
        return true;
    }
    auto& stack_entry = state.stack.back();
    if (++stack_entry.position == 0) {
        uint32_t index;
        auto r = varuint32_from_bin(index, state.bin);
        if (!r)
            return set_error(state, r);
        if (index >= stack_entry.type->fields.size())
            return set_error(state, "invalid variant type index");
        auto& f = stack_entry.type->fields[index];
        state.writer.String(f.name.c_str());
        return f.type->ser &&
               f.type->ser->bin_to_json(state, allow_extensions && stack_entry.allow_extensions, f.type, true);
    } else {
        if (trace_bin_to_json)
            printf("%*s]\n", int((state.stack.size()) * 4), "");
        state.stack.pop_back();
        state.writer.EndArray();
        return true;
    }
}

template <typename T>
ABIEOS_NODISCARD auto bin_to_json(T*, bin_to_json_state& state, bool, const abi_type*, bool start)
    -> std::enable_if_t<std::is_arithmetic_v<T>, bool> {

    T v;
    auto r = from_bin(v, state.bin);
    if (!r)
        return set_error(state, r);
    if constexpr (std::is_same_v<T, bool>) {
        return state.writer.Bool(v);
    } else if constexpr (std::is_floating_point_v<T>) {
        return state.writer.Double(v);
    } else if constexpr (sizeof(T) == 8) {
        auto s = std::to_string(v);
        return state.writer.String(s.c_str(), s.size());
    } else if constexpr (std::is_signed_v<T>) {
        return state.writer.Int64(v);
    } else {
        return state.writer.Uint64(v);
    }
}

ABIEOS_NODISCARD inline bool bin_to_json(std::string*, bin_to_json_state& state, bool, const abi_type*, bool start) {
    std::string s;
    auto r = from_bin(s, state.bin);
    if (!r)
        return set_error(state, r);
    return state.writer.String(s.c_str(), s.size());
}

inline namespace literals {
inline constexpr name operator""_n(const char* s, size_t) { return name{s}; }
} // namespace literals

} // namespace abieos
