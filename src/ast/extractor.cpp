#include "extractor.hpp"
#include <regex>

namespace lgcc {

ExtractedModule Extractor::extract(const fs::path& filepath) {
    std::string ext = filepath.extension().string();
    std::string content = read_file(filepath);

    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".cxx") {
        return extract_cpp(filepath, content);
    } else if (ext == ".lua") {
        return extract_lua(filepath, content);
    } else if (ext == ".py") {
        return extract_python(filepath, content);
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

    // Regex for #include
    std::regex inc_re(R"(^[ \t]*#include[ \t]*[<"]([^>"]+)[>"])");
    // Regex for function or class definition
    std::regex class_re(R"(^[ \t]*(class|struct)[ \t]+([a-zA-Z0-9_]+))");
    std::regex fn_re(R"(^[ \t]*(?:[a-zA-Z0-9_:<>&*]+[ \t]+)+([a-zA-Z0-9_:]+)[ \t]*\(([^)]*)\)[ \t]*(?:const)?[ \t]*\{?)");

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_num = static_cast<int>(i + 1);
        const auto& line = lines[i];

        std::smatch m;
        if (std::regex_search(line, m, inc_re)) {
            Import imp;
            imp.module = m[1].str();
            imp.line = line_num;
            mod.imports.push_back(imp);
        } else if (std::regex_search(line, m, class_re)) {
            Symbol sym;
            sym.kind = m[1].str();
            sym.name = m[2].str();
            sym.line = line_num;
            sym.end_line = line_num;
            sym.signature = trim(line);
            sym.is_top = true;
            mod.symbols.push_back(sym);
        } else if (std::regex_search(line, m, fn_re)) {
            std::string fname = m[1].str();
            if (fname != "if" && fname != "while" && fname != "for" && fname != "switch") {
                Symbol sym;
                sym.kind = "function";
                sym.name = fname;
                sym.line = line_num;
                sym.end_line = line_num;
                sym.signature = trim(line);
                sym.is_top = true;
                mod.symbols.push_back(sym);
            }
        }
    }

    return mod;
}

ExtractedModule Extractor::extract_lua(const fs::path& filepath, const std::string& content) {
    ExtractedModule mod;
    mod.filepath = filepath;
    mod.language = "lua";

    // Fast C++ Lua extractor
    auto lines = split_lines(content);
    std::regex req_re(R"((?:require\s*\(?\s*['"]([^'"]+)['"]\s*\)?))");
    std::regex fn_re(R"(^\s*(local\s+function\s+([a-zA-Z0-9_]+)\s*\((.*?)\)|function\s+([a-zA-Z0-9_\.:]+)\s*\((.*?)\)|(?:local\s+)?([a-zA-Z0-9_\.:]+)\s*=\s*function\s*\((.*?)\)))");

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_num = static_cast<int>(i + 1);
        const auto& line = lines[i];

        std::smatch m;
        if (std::regex_search(line, m, req_re)) {
            Import imp;
            imp.module = m[1].str();
            imp.line = line_num;
            mod.imports.push_back(imp);
        }

        if (std::regex_search(line, m, fn_re)) {
            Symbol sym;
            sym.kind = "function";
            sym.name = m[2].matched ? m[2].str() : (m[4].matched ? m[4].str() : m[6].str());
            sym.line = line_num;
            sym.end_line = line_num;
            sym.signature = trim(line);
            sym.is_top = true;
            mod.symbols.push_back(sym);
        }
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

    for (size_t i = 0; i < lines.size(); ++i) {
        int line_num = static_cast<int>(i + 1);
        const auto& line = lines[i];

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
            sym.end_line = line_num;
            sym.signature = trim(line);
            sym.is_top = true;
            mod.symbols.push_back(sym);
        } else if (std::regex_search(line, m, def_re)) {
            Symbol sym;
            sym.kind = "function";
            sym.name = m[1].str();
            sym.line = line_num;
            sym.end_line = line_num;
            sym.signature = trim(line);
            sym.is_top = (line.find_first_not_of(" \t") == 0);
            mod.symbols.push_back(sym);
        }
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
        ss << "      \"is_top\": " << (sym.is_top ? "true" : "false") << "\n";
        ss << "    }" << (i + 1 < mod.symbols.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

} // namespace lgcc
