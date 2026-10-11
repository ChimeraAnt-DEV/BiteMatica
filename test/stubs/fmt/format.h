// Host-build stub for fmt/format.h. See test/stubs/jni.h.
#pragma once
#include <string>
#include <string_view>
namespace fmt {
using string_view = std::string_view;
template <typename... Args> struct format_arg_store {};
template <typename... Args>
format_arg_store<Args...> make_format_args(const Args &...) {
  return {};
}
template <typename... Args>
std::string vformat(string_view, format_arg_store<Args...>) {
  return {};
}
} // namespace fmt
