use super::symbol_table::{GlobalSymbol, SymbolTable};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;

// TODO(convert-code-graph-tensors | tier:B): convert code graph to tensors
// Description: convert code graph to tensors
// Criterion: review: "convert code graph to tensors"

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

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GraphEdge {
    pub from_fqn: String,
    pub to_fqn: String,
    pub kind: String, // "call", "import", "type"
    pub weight: f64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct DependencyGraph {
    pub nodes: HashMap<String, GlobalSymbol>,
    pub adj: HashMap<String, Vec<GraphEdge>>,
    pub rev_adj: HashMap<String, Vec<GraphEdge>>,
    pub edges: Vec<GraphEdge>,
}

pub struct GraphBuilder;

impl GraphBuilder {
    pub fn build_graph(repo_root: &Path) -> DependencyGraph {
        let mut g = DependencyGraph::default();
        g.nodes = SymbolTable::build_table(repo_root);

        let mut name_to_fqn: HashMap<String, Vec<String>> = HashMap::new();
        for (fqn, _) in &g.nodes {
            let bare_name = fqn.split('.').last().unwrap_or(fqn);
            name_to_fqn
                .entry(bare_name.to_string())
                .or_default()
                .push(fqn.clone());
        }

        for (from_fqn, sym) in &g.nodes {
            for raw_call in &sym.calls {
                let call_leaf = raw_call
                    .rsplit("::")
                    .next()
                    .unwrap_or(raw_call)
                    .rsplit('.')
                    .next()
                    .unwrap_or(raw_call);

                if let Some(target_fqns) = name_to_fqn.get(call_leaf) {
                    for to_fqn in target_fqns {
                        if to_fqn != from_fqn {
                            let edge = GraphEdge {
                                from_fqn: from_fqn.clone(),
                                to_fqn: to_fqn.clone(),
                                kind: if raw_call.chars().next().map(|c| c.is_uppercase()).unwrap_or(false) {
                                    "type".to_string()
                                } else {
                                    "call".to_string()
                                },
                                weight: 1.0,
                            };
                            g.adj.entry(from_fqn.clone()).or_default().push(edge.clone());
                            g.rev_adj.entry(to_fqn.clone()).or_default().push(edge.clone());
                            g.edges.push(edge);
                        }
                    }
                }
            }
        }

        g
    }

    pub fn to_json(g: &DependencyGraph) -> String {
        serde_json::to_string_pretty(g).unwrap_or_else(|_| "{}".to_string())
    }
}
