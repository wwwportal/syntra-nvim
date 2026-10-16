#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <cstdint>
#include <cmath>

namespace fs = std::filesystem;

namespace lgcc {

struct Symbol {
    std::string name;
    std::string kind; // "function", "class", "method"
    int line = 0;
    int end_line = 0;
    std::string signature;
    std::string docstring;
    std::vector<std::string> params;
    std::string return_type;
    std::vector<std::string> calls;
    bool is_top = true;
};

struct Import {
    std::string module;
    std::vector<std::string> names;
    int line = 0;
};

inline int estimate_tokens(std::string_view text) {
    return std::max(1, static_cast<int>(text.size() / 4));
}

inline std::string trim(std::string_view s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(start, end - start + 1));
}

inline bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

inline bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

inline std::string escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

inline std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline bool write_file(const fs::path& path, std::string_view content) {
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file) return false;
    file << content;
    return true;
}

inline std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string_view::size_type start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        auto len = end - start;
        if (len > 0 && text[start + len - 1] == '\r') {
            len--;
        }
        lines.emplace_back(text.substr(start, len));
        start = end + 1;
    }
    return lines;
}

} // namespace lgcc
