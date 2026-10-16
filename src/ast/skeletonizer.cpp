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
            if (!in_fn_body) result += "\n";
            continue;
        }

        // Count braces
        int open_b = 0;
        int close_b = 0;
        for (char c : line) {
            if (c == '{') open_b++;
            if (c == '}') close_b++;
        }

        if (!in_fn_body && starts_with(trimmed, "namespace ") && line.find('{') != std::string::npos) {
            namespace_depth++;
            brace_depth += (open_b - close_b);
            result += line + "\n";
            continue;
        }

        if (!in_fn_body && brace_depth == namespace_depth) {
            // Check if this opens a function body
            if (open_b > 0 && trimmed.find("class ") == std::string::npos &&
                trimmed.find("struct ") == std::string::npos &&
                trimmed.find("enum ") == std::string::npos &&
                trimmed.find("namespace ") == std::string::npos) {
                
                auto brace_pos = line.find('{');
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

std::string Skeletonizer::skeletonize_lua(const fs::path& filepath, const std::string& content) {
    // If nvim -l is available, run high-speed treesitter skeletonizer
    std::string lua_script = 
        "local f = io.open([[" + filepath.string() + "]], 'r')\n"
        "if not f then return end\n"
        "local content = f:read('*a')\n"
        "f:close()\n"
        "local parser = vim.treesitter.get_string_parser(content, 'lua')\n"
        "local root = parser:parse()[1]:root()\n"
        "local symbols = {}\n"
        "for child in root:iter_children() do\n"
        "  if child:type() == 'function_declaration' then\n"
        "    local srow, _, erow, _ = child:range()\n"
        "    local name_node = child:field('name')[1]\n"
        "    local param_node = child:field('parameters')[1]\n"
        "    local name_text = name_node and vim.treesitter.get_node_text(name_node, content) or 'anonymous'\n"
        "    local param_text = param_node and vim.treesitter.get_node_text(param_node, content) or '()'\n"
        "    table.insert(symbols, { line = srow + 1, end_line = erow + 1, sig = 'function ' .. name_text .. param_text })\n"
        "  end\n"
        "end\n"
        "local lines = {}\n"
        "for l in content:gmatch('[^\\r\\n]*') do table.insert(lines, l) end\n"
        "table.sort(symbols, function(a, b) return a.line > b.line end)\n"
        "for _, s in ipairs(symbols) do\n"
        "  for i = s.line, s.end_line do lines[i] = nil end\n"
        "  lines[s.line] = s.sig .. '\\n  -- ... [stub]\\nend'\n"
        "end\n"
        "local out = {}\n"
        "for _, l in pairs(lines) do table.insert(out, l) end\n"
        "io.stdout:write(table.concat(out, '\\n'))\n";

    FILE* pipe = popen("nvim -l - 2>/dev/null", "w");
    if (pipe) {
        fwrite(lua_script.data(), 1, lua_script.size(), pipe);
        pclose(pipe);
    }

    // Fallback: fast regex/line based Lua skeletonizer
    auto lines = split_lines(content);
    std::string result;
    bool in_fn = false;
    std::regex fn_re(R"(^\s*(local\s+function\s+([a-zA-Z0-9_]+)\s*\((.*?)\)|function\s+([a-zA-Z0-9_\.:]+)\s*\((.*?)\)|(?:local\s+)?([a-zA-Z0-9_\.:]+)\s*=\s*function\s*\((.*?)\)))");

    for (const auto& line : lines) {
        if (!in_fn) {
            std::smatch m;
            if (std::regex_search(line, m, fn_re)) {
                result += line + "\n  -- ... [stub]\nend\n";
                in_fn = true;
                continue;
            }
            result += line + "\n";
        } else {
            if (trim(line) == "end") {
                in_fn = false;
            }
        }
    }

    return result.empty() ? content : result;
}

std::string Skeletonizer::skeletonize_python(const fs::path&, const std::string& content) {
    // Zero-overhead python skeletonizer: preserve class, signatures, docstrings; stub def bodies
    auto lines = split_lines(content);
    std::string result;
    bool in_docstring = false;
    bool in_def = false;
    int def_indent = 0;

    std::regex def_re(R"(^([ \t]*)(?:async[ \t]+)?def[ \t]+[a-zA-Z0-9_]+\s*\([^)]*\))");

    for (const auto& line : lines) {
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
                // Out of function body
                in_def = false;
                result += line + "\n";
            } else {
                // Check docstring
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
