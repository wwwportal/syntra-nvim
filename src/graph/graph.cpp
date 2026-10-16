// TODO(convert-code-graph-tensors | tier:B): convert code graph to tensors
// Description: convert code graph to tensors
// Criterion: review: "convert code graph to tensors"

#include "graph.hpp"

namespace lgcc {

DependencyGraph GraphBuilder::build_graph(const fs::path& repo_root) {
    DependencyGraph g;
    g.nodes = SymbolTable::build_table(repo_root);
    auto files = SymbolTable::scan_repository(repo_root);

    // Index symbols by module stem, by short name, and by Class.method suffix
    std::unordered_map<std::string, std::vector<std::string>> mod_to_symbols;
    std::unordered_map<std::string, std::vector<std::string>> suffix_to_fqns;

    for (const auto& [fqn, sym] : g.nodes) {
        std::string mod_stem = sym.filepath.stem().string();
        mod_to_symbols[mod_stem].push_back(fqn);

        // Register full suffix after module stem (e.g. "Extractor.extract" and "extract")
        auto first_dot = fqn.find('.');
        if (first_dot != std::string::npos) {
            std::string after_mod = fqn.substr(first_dot + 1);
            suffix_to_fqns[after_mod].push_back(fqn);
            auto last_dot = after_mod.rfind('.');
            if (last_dot != std::string::npos) {
                suffix_to_fqns[after_mod.substr(last_dot + 1)].push_back(fqn);
            }
        }
        suffix_to_fqns[fqn].push_back(fqn);
    }

    // Track (from_fqn -> to_fqn) to avoid duplicate edges and prefer "call" > "type" > "import"
    std::unordered_map<std::string, GraphEdge> best_edges;
    auto add_edge = [&](const std::string& from_fqn, const std::string& to_fqn, const std::string& kind, double weight) {
        if (from_fqn == to_fqn) return;
        std::string key = from_fqn + "->" + to_fqn;
        auto it = best_edges.find(key);
        if (it == best_edges.end() || weight > it->second.weight) {
            GraphEdge e;
            e.from_fqn = from_fqn;
            e.to_fqn = to_fqn;
            e.kind = kind;
            e.weight = weight;
            best_edges[key] = e;
        }
    };

    // 1. Resolve AST call-graph and type-reference edges (graph-edge-extractor)
    auto lang_group = [](const fs::path& p) -> std::string {
        std::string ext = p.extension().string();
        if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc" || ext == ".cxx" || ext == ".rs") return "native";
        if (ext == ".lua") return "lua";
        if (ext == ".py") return "py";
        return ext;
    };

    for (const auto& [from_fqn, sym] : g.nodes) {
        std::string from_mod = sym.filepath.stem().string();
        std::string from_lang = lang_group(sym.filepath);

        for (const auto& raw_call : sym.calls) {
            std::string norm_call = raw_call;
            size_t pos = 0;
            while ((pos = norm_call.find("::", pos)) != std::string::npos) {
                norm_call.replace(pos, 2, ".");
                pos += 1;
            }
            if (starts_with(norm_call, "M.") || starts_with(norm_call, "M:")) {
                norm_call = norm_call.substr(2);
            }
            bool is_qualified = (norm_call.find('.') != std::string::npos);

            auto it = suffix_to_fqns.find(norm_call);
            if (it != suffix_to_fqns.end()) {
                for (const auto& target_fqn : it->second) {
                    const auto& target_sym = g.nodes.at(target_fqn);
                    if (lang_group(target_sym.filepath) != from_lang) continue;

                    std::string to_mod = target_sym.filepath.stem().string();
                    // If calling an unqualified method name (e.g. "to_json"), only match within same module
                    if (!is_qualified && target_sym.kind == "method" && from_mod != to_mod) {
                        continue;
                    }

                    bool is_type = (target_sym.kind == "class" || target_sym.kind == "struct" || target_sym.kind == "enum" || target_sym.kind == "trait");
                    add_edge(from_fqn, target_fqn, is_type ? "type" : "call", is_type ? 1.5 : 2.0);
                }
            }
        }
    }

    // 2. Connect module-level import edges for imported modules not yet linked by direct call
    for (const auto& file : files) {
        auto mod = Extractor::extract(file);
        std::string from_mod = file.stem().string();
        if (!mod_to_symbols.count(from_mod) || mod_to_symbols[from_mod].empty()) continue;

        for (const auto& imp : mod.imports) {
            std::string to_mod = fs::path(imp.module).stem().string();
            if (to_mod != from_mod && mod_to_symbols.count(to_mod) && !mod_to_symbols[to_mod].empty()) {
                const std::string& from_rep = mod_to_symbols[from_mod].front();
                for (const auto& to_sym : mod_to_symbols[to_mod]) {
                    add_edge(from_rep, to_sym, "import", 0.5);
                }
            }
        }
    }

    g.edges.reserve(best_edges.size());
    for (const auto& [_, e] : best_edges) {
        g.edges.push_back(e);
        g.adj[e.from_fqn].push_back(e);
        g.rev_adj[e.to_fqn].push_back(e);
    }

    std::sort(g.edges.begin(), g.edges.end(), [](const GraphEdge& a, const GraphEdge& b) {
        if (a.from_fqn != b.from_fqn) return a.from_fqn < b.from_fqn;
        return a.to_fqn < b.to_fqn;
    });

    return g;
}

std::string GraphBuilder::to_json(const DependencyGraph& graph) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"node_count\": " << graph.nodes.size() << ",\n";
    ss << "  \"edge_count\": " << graph.edges.size() << ",\n";

    ss << "  \"nodes\": [\n";
    size_t n_idx = 0;
    for (const auto& [fqn, sym] : graph.nodes) {
        ss << "    {\n";
        ss << "      \"id\": \"" << escape_json(fqn) << "\",\n";
        ss << "      \"kind\": \"" << escape_json(sym.kind) << "\",\n";
        ss << "      \"file\": \"" << escape_json(sym.filepath.string()) << "\",\n";
        ss << "      \"line\": " << sym.line << ",\n";
        ss << "      \"end_line\": " << sym.end_line << ",\n";
        ss << "      \"tokens\": " << sym.token_cost << ",\n";
        ss << "      \"signature\": \"" << escape_json(sym.signature) << "\"\n";
        ss << "    }" << (++n_idx < graph.nodes.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";

    ss << "  \"edges\": [\n";
    for (size_t i = 0; i < graph.edges.size(); ++i) {
        const auto& e = graph.edges[i];
        ss << "    {\n";
        ss << "      \"source\": \"" << escape_json(e.from_fqn) << "\",\n";
        ss << "      \"target\": \"" << escape_json(e.to_fqn) << "\",\n";
        ss << "      \"kind\": \"" << escape_json(e.kind) << "\",\n";
        ss << "      \"weight\": " << e.weight << "\n";
        ss << "    }" << (i + 1 < graph.edges.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

} // namespace lgcc
