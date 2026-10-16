#include "extractor.hpp"
#include <regex>

namespace lgcc {

static void extract_calls_from_lines(const std::vector<std::string>& lines, int start_line, int end_line, const std::string& self_name, std::vector<std::string>& out_calls) {
    static const std::regex call_re(R"(([a-zA-Z_][a-zA-Z0-9_]*(?:::[a-zA-Z_][a-zA-Z0-9_]*|\.[a-zA-Z_][a-zA-Z0-9_]*)*)\s*\()");
    static const std::regex type_re(R"(\b([A-Z][a-zA-Z0-9_]+)\b)");
    static const std::unordered_set<std::string> ignore_calls = {
        "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof",
        "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast",
        "function", "local", "elseif", " defined", "typeof", "type", "tostring",
        "tonumber", "ipairs", "pairs", "print", "error", "pcall", "xpcall",
        "std::string", "std::vector", "std::unordered_map", "std::unordered_set",
        "std::pair", "std::regex", "std::smatch", "std::ostringstream", "std::stringstream",
        "std::ifstream", "std::ofstream", "std::max", "std::min", "std::sort", "std::move"
    };

    std::string short_self = self_name;
    auto colon_pos = short_self.rfind("::");
    if (colon_pos != std::string::npos) short_self = short_self.substr(colon_pos + 2);
    auto dot_pos = short_self.rfind('.');
    if (dot_pos != std::string::npos) short_self = short_self.substr(dot_pos + 1);

    std::unordered_set<std::string> seen(out_calls.begin(), out_calls.end());
    int s = std::max(1, start_line);
    int e = std::min(static_cast<int>(lines.size()), std::max(s, end_line));

    for (int idx = s - 1; idx < e; ++idx) {
        const std::string& line = lines[idx];
        std::string t = trim(line);
        if (starts_with(t, "//") || starts_with(t, "--") || starts_with(t, "#")) continue;

        auto begin = std::sregex_iterator(line.begin(), line.end(), call_re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string callee = (*it)[1].str();
            if (idx == s - 1 && (callee == self_name || callee == short_self)) continue;
            if (!ignore_calls.count(callee) && !seen.count(callee)) {
                seen.insert(callee);
                out_calls.push_back(callee);
            }
        }

        auto tbegin = std::sregex_iterator(line.begin(), line.end(), type_re);
        for (auto it = tbegin; it != end; ++it) {
            std::string tref = (*it)[1].str();
            if (tref == self_name || tref == short_self) continue;
            if (!ignore_calls.count(tref) && !seen.count(tref)) {
                seen.insert(tref);
                out_calls.push_back(tref);
            }
        }
    }
}

ExtractedModule Extractor::extract(const fs::path& filepath) {
    std::string ext = filepath.extension().string();
    std::string content = read_file(filepath);

    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".cxx") {
        return extract_cpp(filepath, content);
    } else if (ext == ".lua") {
        return extract_lua(filepath, content);
    } else if (ext == ".py") {
        return extract_python(filepath, content);
    } else if (ext == ".rs") {
        return extract_rust(filepath, content);
    }

    ExtractedModule mod;
    mod.filepath = filepath;
    mod.language = "generic";
    return mod;
}

ExtractedModule Extractor::extract_cpp(const fs::path& filepath, const std::string& content) {
    ExtractedModule mod;
    mod.filepath = filepath;
    mod.language = "cpp";

    auto lines = split_lines(content);

    std::regex inc_re(R"(^[ \t]*#include[ \t]*[<"]([^>"]+)[>"])");
    std::regex class_re(R"(^[ \t]*(class|struct)[ \t]+([a-zA-Z_][a-zA-Z0-9_]*)[ \t]*(?:final)?[ \t]*(?::[^{;]+)?\{?)");
    std::regex fn_re(R"(^[ \t]*(?:(?:static|inline|virtual|explicit|constexpr|extern[ \t]+"C")[ \t]+)*(?:[a-zA-Z_][a-zA-Z0-9_:<>&*,\t ]*?[ \t*&]+)([a-zA-Z_][a-zA-Z0-9_:]*)[ \t]*\(([^;{}]*)\)[ \t]*(?:const)?[ \t]*(?:noexcept)?[ \t]*(?:\{|;|$))");

    static const std::unordered_set<std::string> invalid_fn_names = {
        "if", "while", "for", "switch", "catch", "return", "sizeof", "else", "delete", "throw"
    };

    int brace_depth = 0;
    int namespace_depth = 0;
    int class_depth = 0;
    std::string current_class;
    int current_class_brace = -1;
    int in_fn_Idx = -1;
    int fn_entry_brace = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_num = static_cast<int>(i + 1);
        const auto& line = lines[i];
        std::string trimmed = trim(line);

        if (starts_with(trimmed, "//") || starts_with(trimmed, "/*") || starts_with(trimmed, "*")) {
            continue;
        }

        std::smatch m;
        if (std::regex_search(line, m, inc_re)) {
            Import imp;
            imp.module = m[1].str();
            imp.line = line_num;
            mod.imports.push_back(imp);
        }

        int open_b = 0;
        int close_b = 0;
        count_braces_outside_strings(line, open_b, close_b);

        if (in_fn_Idx == -1) {
            if (starts_with(trimmed, "namespace ") && open_b > 0) {
                namespace_depth += (open_b - close_b);
                brace_depth += (open_b - close_b);
                continue;
            }

            if (brace_depth <= namespace_depth + class_depth) {
                if (std::regex_search(line, m, class_re) && trimmed.back() != ';') {
                    Symbol sym;
                    sym.kind = m[1].str();
                    sym.name = m[2].str();
                    sym.line = line_num;
                    sym.end_line = line_num;
                    sym.signature = trimmed;
                    sym.is_top = true;
                    mod.symbols.push_back(sym);
                    if (open_b > close_b) {
                        current_class = sym.name;
                        current_class_brace = brace_depth;
                        class_depth++;
                    }
                    brace_depth += (open_b - close_b);
                    continue;
                } else if (std::regex_search(line, m, fn_re)) {
                    std::string fname = m[1].str();
                    // Extract short name after :: if present
                    auto colon_pos = fname.rfind("::");
                    std::string short_name = (colon_pos != std::string::npos) ? fname.substr(colon_pos + 2) : fname;

                    // Reject variable declarations with constructor syntax when inside function or lacking return type keywords
                    bool is_decl_or_def = (open_b > 0) || (brace_depth == namespace_depth + class_depth && !starts_with(trimmed, "std::regex ") && !starts_with(trimmed, "std::vector<") && !starts_with(trimmed, "std::stringstream "));
                    if (!invalid_fn_names.count(short_name) && is_decl_or_def) {
                        Symbol sym;
                        sym.kind = (colon_pos != std::string::npos || !current_class.empty()) ? "method" : "function";
                        if (colon_pos == std::string::npos && !current_class.empty()) {
                            sym.name = current_class + "::" + short_name;
                        } else {
                            sym.name = fname;
                        }
                        sym.line = line_num;
                        sym.end_line = line_num;
                        auto brace_pos = trimmed.find('{');
                        sym.signature = (brace_pos != std::string::npos) ? trim(trimmed.substr(0, brace_pos)) : trimmed;
                        sym.is_top = current_class.empty();
                        mod.symbols.push_back(sym);

                        if (open_b > close_b) {
                            in_fn_Idx = static_cast<int>(mod.symbols.size()) - 1;
                            fn_entry_brace = brace_depth;
                        }
                    }
                }
            }
        }

        brace_depth += (open_b - close_b);
        if (brace_depth < 0) brace_depth = 0;

        if (in_fn_Idx != -1 && brace_depth <= fn_entry_brace) {
            mod.symbols[in_fn_Idx].end_line = line_num;
            in_fn_Idx = -1;
            fn_entry_brace = -1;
        }

        if (!current_class.empty() && brace_depth <= current_class_brace) {
            for (auto it = mod.symbols.rbegin(); it != mod.symbols.rend(); ++it) {
                if ((it->kind == "class" || it->kind == "struct") && it->name == current_class) {
                    it->end_line = line_num;
                    break;
                }
            }
            current_class.clear();
            current_class_brace = -1;
            if (class_depth > 0) class_depth--;
        }
    }

    for (auto& sym : mod.symbols) {
        extract_calls_from_lines(lines, sym.line, sym.end_line, sym.name, sym.calls);
    }

    return mod;
}

ExtractedModule Extractor::extract_lua(const fs::path& filepath, const std::string& content) {
    ExtractedModule mod;
    mod.filepath = filepath;
    mod.language = "lua";

    auto lines = split_lines(content);
    std::regex req_re(R"((?:require\s*\(?\s*['"]([^'"]+)['"]\s*\)?))");
    std::regex fn_re(R"(^\s*(local\s+function\s+([a-zA-Z0-9_]+)\s*\((.*?)\)|function\s+([a-zA-Z0-9_\.:]+)\s*\((.*?)\)|(?:local\s+)?([a-zA-Z0-9_\.:]+)\s*=\s*function\s*\((.*?)\)))");

    int active_sym = -1;
    int active_indent = 0;

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_num = static_cast<int>(i + 1);
        const auto& line = lines[i];
        std::string trimmed = trim(line);

        std::smatch m;
        if (!starts_with(trimmed, "--") && std::regex_search(line, m, req_re)) {
            Import imp;
            imp.module = m[1].str();
            imp.line = line_num;
            mod.imports.push_back(imp);
        }

        auto first_non_space = line.find_first_not_of(" \t");
        int indent = (first_non_space == std::string::npos) ? 0 : static_cast<int>(first_non_space);

        if (active_sym == -1 && indent == 0 && !starts_with(trimmed, "--") && std::regex_search(line, m, fn_re)) {
            Symbol sym;
            sym.kind = "function";
            sym.name = m[2].matched ? m[2].str() : (m[4].matched ? m[4].str() : m[6].str());
            auto dot_pos = sym.name.find_first_of(".:");
            if (dot_pos != std::string::npos && (starts_with(sym.name, "M.") || starts_with(sym.name, "M:"))) {
                sym.name = sym.name.substr(dot_pos + 1);
            }
            sym.line = line_num;
            sym.end_line = line_num;
            sym.signature = trimmed;
            sym.is_top = true;
            mod.symbols.push_back(sym);
            if (!ends_with(trimmed, "end")) {
                active_sym = static_cast<int>(mod.symbols.size()) - 1;
                active_indent = indent;
            }
        } else if (active_sym != -1) {
            if (indent == active_indent && (trimmed == "end" || starts_with(trimmed, "end)") || starts_with(trimmed, "end,"))) {
                mod.symbols[active_sym].end_line = line_num;
                active_sym = -1;
            }
        }
    }

    for (auto& sym : mod.symbols) {
        extract_calls_from_lines(lines, sym.line, sym.end_line, sym.name, sym.calls);
    }

    return mod;
}

ExtractedModule Extractor::extract_python(const fs::path& filepath, const std::string& content) {
    ExtractedModule mod;
    mod.filepath = filepath;
    mod.language = "python";

    auto lines = split_lines(content);
    std::regex imp_re(R"(^[ \t]*(?:import|from)[ \t]+([a-zA-Z0-9_\.]+))");
    std::regex class_re(R"(^[ \t]*class[ \t]+([a-zA-Z0-9_]+))");
    std::regex def_re(R"(^[ \t]*(?:async[ \t]+)?def[ \t]+([a-zA-Z0-9_]+)\s*\((.*?)\))");

    std::vector<std::pair<int, int>> open_syms; // (sym_idx, indent)

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_num = static_cast<int>(i + 1);
        const auto& line = lines[i];
        std::string trimmed = trim(line);
        if (trimmed.empty() || starts_with(trimmed, "#")) continue;

        auto first_non_space = line.find_first_not_of(" \t");
        int indent = (first_non_space == std::string::npos) ? 0 : static_cast<int>(first_non_space);

        while (!open_syms.empty() && indent <= open_syms.back().second) {
            mod.symbols[open_syms.back().first].end_line = line_num - 1;
            open_syms.pop_back();
        }

        std::smatch m;
        if (std::regex_search(line, m, imp_re)) {
            Import imp;
            imp.module = m[1].str();
            imp.line = line_num;
            mod.imports.push_back(imp);
        } else if (std::regex_search(line, m, class_re)) {
            Symbol sym;
            sym.kind = "class";
            sym.name = m[1].str();
            sym.line = line_num;
            sym.end_line = static_cast<int>(lines.size());
            sym.signature = trimmed;
            sym.is_top = (indent == 0);
            mod.symbols.push_back(sym);
            open_syms.push_back({static_cast<int>(mod.symbols.size()) - 1, indent});
        } else if (std::regex_search(line, m, def_re)) {
            Symbol sym;
            sym.kind = (indent == 0) ? "function" : "method";
            sym.name = m[1].str();
            sym.line = line_num;
            sym.end_line = static_cast<int>(lines.size());
            sym.signature = trimmed;
            sym.is_top = (indent == 0);
            mod.symbols.push_back(sym);
            open_syms.push_back({static_cast<int>(mod.symbols.size()) - 1, indent});
        }
    }

    for (auto& sym : mod.symbols) {
        extract_calls_from_lines(lines, sym.line, sym.end_line, sym.name, sym.calls);
    }

    return mod;
}

ExtractedModule Extractor::extract_rust(const fs::path& filepath, const std::string& content) {
    ExtractedModule mod;
    mod.filepath = filepath;
    mod.language = "rust";

    auto lines = split_lines(content);
    std::regex use_re(R"(^[ \t]*use[ \t]+([a-zA-Z0-9_:]+))");
    std::regex type_re(R"(^[ \t]*(?:pub[ \t]+)?(struct|enum|trait)[ \t]+([a-zA-Z0-9_]+))");
    std::regex fn_re(R"(^[ \t]*(?:pub[ \t]+)?(?:unsafe[ \t]+)?(?:extern[ \t]+"C"[ \t]+)?fn[ \t]+([a-zA-Z0-9_]+)\s*\()");

    int brace_depth = 0;
    int active_sym = -1;
    int entry_brace = -1;

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_num = static_cast<int>(i + 1);
        const auto& line = lines[i];
        std::string trimmed = trim(line);
        if (starts_with(trimmed, "//")) continue;

        std::smatch m;
        if (std::regex_search(line, m, use_re)) {
            Import imp;
            imp.module = m[1].str();
            imp.line = line_num;
            mod.imports.push_back(imp);
        } else if (active_sym == -1 && std::regex_search(line, m, type_re)) {
            Symbol sym;
            sym.kind = m[1].str();
            sym.name = m[2].str();
            sym.line = line_num;
            sym.end_line = line_num;
            auto bpos = trimmed.find('{');
            sym.signature = (bpos != std::string::npos) ? trim(trimmed.substr(0, bpos)) : trimmed;
            sym.is_top = true;
            mod.symbols.push_back(sym);
            if (line.find('{') != std::string::npos && line.find('}') == std::string::npos) {
                active_sym = static_cast<int>(mod.symbols.size()) - 1;
                entry_brace = brace_depth;
            }
        } else if (active_sym == -1 && std::regex_search(line, m, fn_re)) {
            Symbol sym;
            sym.kind = "function";
            sym.name = m[1].str();
            sym.line = line_num;
            sym.end_line = line_num;
            auto bpos = trimmed.find('{');
            sym.signature = (bpos != std::string::npos) ? trim(trimmed.substr(0, bpos)) : trimmed;
            sym.is_top = true;
            mod.symbols.push_back(sym);
            if (line.find('{') != std::string::npos && line.find('}') == std::string::npos) {
                active_sym = static_cast<int>(mod.symbols.size()) - 1;
                entry_brace = brace_depth;
            }
        }

        for (char c : line) {
            if (c == '{') brace_depth++;
            else if (c == '}') brace_depth--;
        }
        if (brace_depth < 0) brace_depth = 0;

        if (active_sym != -1 && brace_depth <= entry_brace) {
            mod.symbols[active_sym].end_line = line_num;
            active_sym = -1;
            entry_brace = -1;
        }
    }

    for (auto& sym : mod.symbols) {
        extract_calls_from_lines(lines, sym.line, sym.end_line, sym.name, sym.calls);
    }

    return mod;
}

std::string Extractor::to_json(const ExtractedModule& mod) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"file\": \"" << escape_json(mod.filepath.string()) << "\",\n";
    ss << "  \"language\": \"" << escape_json(mod.language) << "\",\n";

    ss << "  \"imports\": [\n";
    for (size_t i = 0; i < mod.imports.size(); ++i) {
        const auto& imp = mod.imports[i];
        ss << "    {\n";
        ss << "      \"module\": \"" << escape_json(imp.module) << "\",\n";
        ss << "      \"line\": " << imp.line << "\n";
        ss << "    }" << (i + 1 < mod.imports.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    ss << "  \"symbols\": [\n";
    for (size_t i = 0; i < mod.symbols.size(); ++i) {
        const auto& sym = mod.symbols[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << escape_json(sym.name) << "\",\n";
        ss << "      \"kind\": \"" << escape_json(sym.kind) << "\",\n";
        ss << "      \"line\": " << sym.line << ",\n";
        ss << "      \"end_line\": " << sym.end_line << ",\n";
        ss << "      \"signature\": \"" << escape_json(sym.signature) << "\",\n";
        ss << "      \"calls\": [";
        for (size_t c = 0; c < sym.calls.size(); ++c) {
            ss << "\"" << escape_json(sym.calls[c]) << "\"" << (c + 1 < sym.calls.size() ? ", " : "");
        }
        ss << "],\n";
        ss << "      \"is_top\": " << (sym.is_top ? "true" : "false") << "\n";
        ss << "    }" << (i + 1 < mod.symbols.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

} // namespace lgcc
