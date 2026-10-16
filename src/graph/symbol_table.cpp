#include "symbol_table.hpp"

namespace lgcc {

std::vector<fs::path> SymbolTable::scan_repository(const fs::path& repo_root) {
    std::vector<fs::path> result;
    static const std::unordered_set<std::string> ignore_dirs = {
        ".git", "build", "target", ".lazy", "vendor", "__pycache__", ".venv", "venv", "node_modules"
    };
    static const std::unordered_set<std::string> valid_exts = {
        ".cpp", ".hpp", ".h", ".cc", ".cxx", ".lua", ".py", ".rs"
    };

    if (!fs::exists(repo_root) || !fs::is_directory(repo_root)) {
        return result;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(repo_root, fs::directory_options::skip_permission_denied)) {
            if (entry.is_directory()) {
                if (ignore_dirs.count(entry.path().filename().string())) {
                    continue;
                }
            } else if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (valid_exts.count(ext)) {
                    bool ignored = false;
                    for (const auto& part : entry.path()) {
                        if (ignore_dirs.count(part.string())) {
                            ignored = true;
                            break;
                        }
                    }
                    if (!ignored) {
                        result.push_back(entry.path());
                    }
                }
            }
        }
    } catch (...) {}

    std::sort(result.begin(), result.end());
    return result;
}

std::unordered_map<std::string, GlobalSymbol> SymbolTable::build_table(const fs::path& repo_root) {
    std::unordered_map<std::string, GlobalSymbol> table;
    auto files = scan_repository(repo_root);

    for (const auto& file : files) {
        auto mod = Extractor::extract(file);
        std::string mod_stem = file.stem().string();

        for (const auto& sym : mod.symbols) {
            std::string norm_name = sym.name;
            size_t pos = 0;
            while ((pos = norm_name.find("::", pos)) != std::string::npos) {
                norm_name.replace(pos, 2, ".");
                pos += 1;
            }
            std::string fqn = mod_stem + "." + norm_name;

            auto it = table.find(fqn);
            if (it == table.end()) {
                GlobalSymbol gsym;
                gsym.fqn = fqn;
                gsym.filepath = file;
                gsym.line = sym.line;
                gsym.end_line = sym.end_line;
                gsym.signature = sym.signature;
                gsym.kind = sym.kind;
                gsym.token_cost = estimate_tokens(sym.signature);
                gsym.calls = sym.calls;
                table[fqn] = std::move(gsym);
            } else {
                // Merge calls from .cpp implementation into existing .hpp entry (or vice versa)
                std::unordered_set<std::string> seen(it->second.calls.begin(), it->second.calls.end());
                for (const auto& c : sym.calls) {
                    if (!seen.count(c)) {
                        seen.insert(c);
                        it->second.calls.push_back(c);
                    }
                }
                if ((sym.end_line - sym.line) > (it->second.end_line - it->second.line)) {
                    it->second.filepath = file;
                    it->second.line = sym.line;
                    it->second.end_line = sym.end_line;
                }
            }
        }
    }

    return table;
}

std::string SymbolTable::to_json(const std::unordered_map<std::string, GlobalSymbol>& table) {
    std::ostringstream ss;
    ss << "{\n";
    size_t count = 0;
    for (const auto& [fqn, sym] : table) {
        ss << "  \"" << escape_json(fqn) << "\": {\n";
        ss << "    \"file\": \"" << escape_json(sym.filepath.string()) << "\",\n";
        ss << "    \"line\": " << sym.line << ",\n";
        ss << "    \"kind\": \"" << escape_json(sym.kind) << "\",\n";
        ss << "    \"tokens\": " << sym.token_cost << ",\n";
        ss << "    \"signature\": \"" << escape_json(sym.signature) << "\"\n";
        ss << "  }" << (++count < table.size() ? "," : "") << "\n";
    }
    ss << "}\n";
    return ss.str();
}

} // namespace lgcc
