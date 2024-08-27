#pragma once

#include "map_macro.h"
#include <type_traits>

namespace sysio { namespace reflection {

   template <typename T>
   struct has_for_each_field {
    private:
      struct F {
         template <typename A, typename B>
         void operator()(const A&, const B&);
      };

      template <typename C>
      static char test(decltype(sysio_for_each_field((C*)nullptr, std::declval<F>()))*);

      template <typename C>
      static long test(...);

    public:
      static constexpr bool value = sizeof(test<T>((void*)nullptr)) == sizeof(char);
   };

   template <typename T>
   inline constexpr bool has_for_each_field_v = has_for_each_field<T>::value;

#define SYSIO_REFLECT_MEMBER(STRUCT, FIELD)                                                                            \
   f(#FIELD, [](auto p) -> decltype(&std::decay_t<decltype(*p)>::FIELD) { return &std::decay_t<decltype(*p)>::FIELD; });

#define SYSIO_REFLECT_STRIP_BASEbase
#define SYSIO_REFLECT_BASE(STRUCT, BASE)                                                                               \
   static_assert(std::is_base_of_v<SYSIO_REFLECT_STRIP_BASE##BASE, STRUCT>, #BASE " is not a base class of " #STRUCT); \
   sysio_for_each_field((SYSIO_REFLECT_STRIP_BASE##BASE*)nullptr, f);

#define SYSIO_REFLECT_SIGNATURE(STRUCT, ...)                                                                           \
   [[maybe_unused]] inline constexpr const char* get_type_name(STRUCT*) { return #STRUCT; }                                      \
   template <typename F>                                                                                               \
   constexpr void sysio_for_each_field(STRUCT*, F f)

/**
 * SYSIO_REFLECT(<struct>, <member or base spec>...)
 * Each parameter should be either the keyword 'base' followed by a base class of the struct or
 * an identifier which names a non-static data member of the struct.
 */
#define SYSIO_REFLECT(...)                                                                                             \
   SYSIO_REFLECT_SIGNATURE(__VA_ARGS__) { SYSIO_MAP_REUSE_ARG0(SYSIO_REFLECT_INTERNAL, __VA_ARGS__) }

// Identity the keyword 'base' followed by at least one token
#define SYSIO_REFLECT_SELECT_I(a, b, c, d, ...) SYSIO_REFLECT_##d
#define SYSIO_REFLECT_IS_BASE() ~, ~
#define SYSIO_REFLECT_IS_BASE_TESTbase ~, SYSIO_REFLECT_IS_BASE

#define SYSIO_APPLY(m, x) m x
#define SYSIO_CAT(x, y) x##y
#define SYSIO_REFLECT_INTERNAL(STRUCT, FIELD)                                                                          \
   SYSIO_APPLY(SYSIO_REFLECT_SELECT_I, (SYSIO_CAT(SYSIO_REFLECT_IS_BASE_TEST, FIELD()), MEMBER, BASE, MEMBER))         \
   (STRUCT, FIELD)

}} // namespace sysio::reflection
