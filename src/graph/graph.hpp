#pragma once

#include "symbol_table.hpp"

namespace lgcc {

// SPEC(graph-edge-extractor | tier:B | depends_on:[graph-symbol-table] | blocks:[ir-graph-builder]): Extract cross-file call-graph and type-reference dependency edges.
// Description: Resolve AST call sites and type annotations against the global symbol table to generate directed dependency edges E.
// Criterion: review: "Extracted graph contains verified caller->callee edges across different modules"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"graph_symbol_table"},"kind":"call"},"kind":"definition","name":"graph_edge_extractor","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"graph_edge_extractor_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"graph_edge_extractor"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(ir-graph-builder | tier:B | depends_on:[graph-edge-extractor] | blocks:[router-distance-decay]): Construct global multi-file repository symbol dependency graph.
// Description: Assemble and serialize global repository symbol DAG G=(V, E) into compact adjacency representation.
// Criterion: review: "scripts/bundle_ir.py --graph returns full repository symbol DAG with verified node and edge counts"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"graph_edge_extractor"},"kind":"call"},"kind":"definition","name":"ir_graph_builder","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"ir_graph_builder_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"ir_graph_builder"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(lgcc-topo-spec-stitcher | tier:B | depends_on:[lgcc-spec-scanner] | blocks:[prismpm-jit-verifier]): Topologically assemble distributed specification patches into canonical .lex.tex document.
// Description: Implement topological specification stitcher in lgcc assembling distributed specification fragments into a unified, mathematically sound .lex.tex document ordered by dependency graph.
// Criterion: assert: "lgcc --stitch-specs produces valid .lex.tex document preserving topological ordering of all types, definitions, and theorems"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"lgcc_spec_scanner"},"kind":"call"},"kind":"definition","name":"lgcc_topo_spec_stitcher","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"lgcc_topo_spec_stitcher_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"lgcc_topo_spec_stitcher"},"kind":"call"},"right":{"kind":"bool","value":true}}}



struct GraphEdge {
    std::string from_fqn;
    std::string to_fqn;
    std::string kind; // "call", "import", "type"
    double weight = 1.0;
};

struct DependencyGraph {
    std::unordered_map<std::string, GlobalSymbol> nodes;
    std::unordered_map<std::string, std::vector<GraphEdge>> adj; // adjacency list: node -> outgoing edges
    std::unordered_map<std::string, std::vector<GraphEdge>> rev_adj; // reverse adjacency: node -> incoming edges
    std::vector<GraphEdge> edges;
};

class GraphBuilder {
public:
    static DependencyGraph build_graph(const fs::path& repo_root);
    static std::string to_json(const DependencyGraph& graph);
};

} // namespace lgcc
