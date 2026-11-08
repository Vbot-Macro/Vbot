#pragma once
#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>

namespace vbot::util {

inline std::string sanitizeName(std::string_view in, std::string_view fallback = "last") {
    std::string out;
    for (char c : in) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_') out.push_back(c);
        else if (c == ' ') out.push_back('_');
        if (out.size() >= 40) break;
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string(fallback) : out;
}

inline int parseIntClamped(std::string_view s, int lo, int hi, int fallback) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    if (s.empty()) return fallback;
    long long v = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (res.ec == std::errc::result_out_of_range) return hi;
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) return fallback;
    return static_cast<int>(std::clamp<long long>(v, lo, hi));
}

inline int roundDownToEven(int v) {
    return v - (v % 2);
}

}
