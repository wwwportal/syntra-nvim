#include "graph.hpp"

namespace lgcc {

DependencyGraph GraphBuilder::build_graph(const fs::path& repo_root) {
    DependencyGraph g;
    g.nodes = SymbolTable::build_table(repo_root);
    auto files = SymbolTable::scan_repository(repo_root);

    // Map module stem to list of its symbols
    std::unordered_map<std::string, std::vector<std::string>> mod_to_symbols;
    for (const auto& [fqn, sym] : g.nodes) {
        std::string mod_stem = sym.filepath.stem().string();
        mod_to_symbols[mod_stem].push_back(fqn);
    }

    for (const auto& file : files) {
        auto mod = Extractor::extract(file);
        std::string from_mod = file.stem().string();

        for (const auto& imp : mod.imports) {
            std::string to_mod = fs::path(imp.module).stem().string();
            // If imported module is local and tracked
            if (mod_to_symbols.count(to_mod)) {
                // Connect symbols from caller to callee module
                for (const auto& from_sym : mod_to_symbols[from_mod]) {
                    for (const auto& to_sym : mod_to_symbols[to_mod]) {
                        GraphEdge e;
                        e.from_fqn = from_sym;
                        e.to_fqn = to_sym;
                        e.kind = "import";
                        e.weight = 1.0;
                        g.edges.push_back(e);
                        g.adj[from_sym].push_back(e);
                        g.rev_adj[to_sym].push_back(e);
                    }
                }
            }
        }
    }

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
        ss << "      \"file\": \"" << escape_json(sym.filepath.string()) << "\",\n";
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
        ss << "      \"kind\": \"" << escape_json(e.kind) << "\"\n";
        ss << "    }" << (i + 1 < graph.edges.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";

    return ss.str();
}

} // namespace lgcc
