#include "skeletonizer.hpp"
#include <regex>

namespace lgcc {

std::string Skeletonizer::skeletonize(const fs::path& filepath) {
    std::string ext = filepath.extension().string();
    std::string content = read_file(filepath);

    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".cxx") {
        return skeletonize_cpp(filepath, content);
    } else if (ext == ".lua") {
        return skeletonize_lua(filepath, content);
    } else if (ext == ".py") {
        return skeletonize_python(filepath, content);
    } else if (ext == ".rs") {
        return skeletonize_rust(filepath, content);
    }

    return content;
}

std::string Skeletonizer::skeletonize_cpp(const fs::path&, const std::string& content) {
    auto lines = split_lines(content);
    std::string result;
    int brace_depth = 0;
    int namespace_depth = 0;
    bool in_fn_body = false;

    for (const auto& line : lines) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }
        // Skip regular comments in skeleton to keep interface compact, preserve /// doc comments
        if (starts_with(trimmed, "//") && !starts_with(trimmed, "///")) {
            continue;
        }

        int open_b = 0;
        int close_b = 0;
        count_braces_outside_strings(line, open_b, close_b);

        if (!in_fn_body && starts_with(trimmed, "namespace ") && open_b > 0) {
            namespace_depth++;
            brace_depth += (open_b - close_b);
            result += line + "\n";
            continue;
        }

        if (!in_fn_body && brace_depth == namespace_depth) {
            if (open_b > 0 && trimmed.find("class ") == std::string::npos &&
                trimmed.find("struct ") == std::string::npos &&
                trimmed.find("enum ") == std::string::npos &&
                trimmed.find("namespace ") == std::string::npos) {
                
                auto brace_pos = line.rfind('{');
                result += line.substr(0, brace_pos) + " { /* stub */ }\n";
                brace_depth += (open_b - close_b);
                if (brace_depth > namespace_depth) {
                    in_fn_body = true;
                }
                continue;
            }
        }

        if (in_fn_body) {
            brace_depth += (open_b - close_b);
            if (brace_depth <= namespace_depth) {
                brace_depth = namespace_depth;
                in_fn_body = false;
            }
            continue;
        }

        brace_depth += (open_b - close_b);
        result += line + "\n";
    }

    return result;
}

std::string Skeletonizer::skeletonize_lua(const fs::path&, const std::string& content) {
    auto lines = split_lines(content);
    std::string result;
    bool in_block = false;
    int block_indent = 0;
    std::string block_closer = "end";

    std::regex fn_re(R"(^\s*(local\s+function\s+([a-zA-Z0-9_]+)\s*\((.*?)\)|function\s+([a-zA-Z0-9_\.:]+)\s*\((.*?)\)|(?:local\s+)?([a-zA-Z0-9_\.:]+)\s*=\s*function\s*\((.*?)\)))");
    std::regex cmd_re(R"(^\s*vim\.api\.nvim_create_(user_command|autocmd)\s*\()");

    for (const auto& line : lines) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        auto first_non_space = line.find_first_not_of(" \t");
        int indent = (first_non_space == std::string::npos) ? 0 : static_cast<int>(first_non_space);

        if (!in_block) {
            // Preserve LuaDoc comments (---), skip non-doc comments (--)
            if (starts_with(trimmed, "--") && !starts_with(trimmed, "---")) {
                continue;
            }

            std::smatch m;
            if (std::regex_search(line, m, fn_re)) {
                if (ends_with(trimmed, "end")) {
                    result += line + "\n";
                } else {
                    result += line + " -- [stub] end\n";
                    in_block = true;
                    block_indent = indent;
                    block_closer = "end";
                }
                continue;
            }

            if (indent == 0 && std::regex_search(line, m, cmd_re)) {
                result += line + " -- [stub] })\n";
                in_block = true;
                block_indent = indent;
                block_closer = "})";
                continue;
            }

            // Collapse multiline top-level table literals `local foo = {`
            if (indent == 0 && ends_with(trimmed, "{") && trimmed.find("function") == std::string::npos) {
                result += line + " -- [table] }\n";
                in_block = true;
                block_indent = indent;
                block_closer = "}";
                continue;
            }

            result += line + "\n";
        } else {
            if (indent == block_indent) {
                if (block_closer == "end" && (trimmed == "end" || starts_with(trimmed, "end)") || starts_with(trimmed, "end,"))) {
                    in_block = false;
                } else if (block_closer == "})" && (trimmed == "})" || starts_with(trimmed, "})"))) {
                    in_block = false;
                } else if (block_closer == "}" && (trimmed == "}" || starts_with(trimmed, "}"))) {
                    in_block = false;
                }
            }
        }
    }

    return result.empty() ? content : result;
}

std::string Skeletonizer::skeletonize_python(const fs::path&, const std::string& content) {
    auto lines = split_lines(content);
    std::string result;
    bool in_docstring = false;
    bool in_def = false;
    int def_indent = 0;

    std::regex def_re(R"(^([ \t]*)(?:async[ \t]+)?def[ \t]+[a-zA-Z0-9_]+\s*\([^)]*\))");

    for (const auto& line : lines) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || starts_with(trimmed, "#")) continue;

        auto first_char = line.find_first_not_of(" \t");
        int indent = (first_char == std::string::npos) ? 0 : static_cast<int>(first_char);

        std::smatch m;
        if (std::regex_search(line, m, def_re)) {
            result += line + "\n";
            in_def = true;
            def_indent = indent;
            continue;
        }

        if (in_def) {
            if (first_char != std::string::npos && indent <= def_indent) {
                in_def = false;
                result += line + "\n";
            } else {
                if (line.find("\"\"\"") != std::string::npos || line.find("'''") != std::string::npos) {
                    result += line + "\n";
                    in_docstring = !in_docstring;
                } else if (in_docstring) {
                    result += line + "\n";
                } else if (result.find("...") == std::string::npos || !ends_with(result, "    ...\n")) {
                    result += std::string(def_indent + 4, ' ') + "...\n";
                }
            }
        } else {
            result += line + "\n";
        }
    }

    return result;
}

std::string Skeletonizer::skeletonize_rust(const fs::path&, const std::string& content) {
    auto lines = split_lines(content);
    std::string result;
    int brace_depth = 0;
    bool in_fn_body = false;
    int fn_entry_brace = 0;

    std::regex fn_re(R"(^[ \t]*(?:pub[ \t]+)?(?:unsafe[ \t]+)?(?:extern[ \t]+"C"[ \t]+)?fn[ \t]+[a-zA-Z0-9_]+\s*\()");

    for (const auto& line : lines) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (starts_with(trimmed, "//") && !starts_with(trimmed, "///")) continue;

        int open_b = 0;
        int close_b = 0;
        count_braces_outside_strings(line, open_b, close_b);

        if (!in_fn_body && std::regex_search(line, fn_re) && open_b > 0) {
            auto bpos = line.rfind('{');
            result += line.substr(0, bpos) + "{ /* stub */ }\n";
            fn_entry_brace = brace_depth;
            brace_depth += (open_b - close_b);
            if (brace_depth > fn_entry_brace) {
                in_fn_body = true;
            }
            continue;
        }

        if (in_fn_body) {
            brace_depth += (open_b - close_b);
            if (brace_depth <= fn_entry_brace) {
                brace_depth = fn_entry_brace;
                in_fn_body = false;
            }
            continue;
        }

        brace_depth += (open_b - close_b);
        result += line + "\n";
    }

    return result;
}

SkeletonMetrics Skeletonizer::compute_metrics(const fs::path& filepath) {
    std::string orig = read_file(filepath);
    std::string skel = skeletonize(filepath);

    SkeletonMetrics m;
    m.orig_chars = orig.size();
    m.skel_chars = skel.size();
    m.orig_tokens = estimate_tokens(orig);
    m.skel_tokens = estimate_tokens(skel);
    m.orig_lines = split_lines(orig).size();
    m.skel_lines = split_lines(skel).size();

    m.char_reduction = m.orig_chars > 0 ? (1.0 - static_cast<double>(m.skel_chars) / m.orig_chars) * 100.0 : 0.0;
    m.token_reduction = m.orig_tokens > 0 ? (1.0 - static_cast<double>(m.skel_tokens) / m.orig_tokens) * 100.0 : 0.0;

    return m;
}

} // namespace lgcc
