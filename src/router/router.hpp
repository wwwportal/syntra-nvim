#pragma once

#include "../graph/graph.hpp"
#include <queue>

namespace lgcc {

// SPEC(router-distance-decay | tier:B | depends_on:[ir-graph-builder] | blocks:[router-personalized-pagerank]): Compute directed shortest-path distance decay weights from target symbols.
// Description: Calculate directed graph distances d(u, v) from target symbols and apply exponential/harmonic distance decay weighting.
// Criterion: review: "Distance decay assigns strictly decreasing weights to k-hop neighborhood nodes"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"ir_graph_builder"},"kind":"call"},"kind":"definition","name":"router_distance_decay","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"router_distance_decay_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"router_distance_decay"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(router-personalized-pagerank | tier:B | depends_on:[router-distance-decay] | blocks:[router-topological-pruner]): Compute Personalized PageRank centrality rooted at target symbols.
// Description: Run power-iteration Personalized PageRank with random walk restart rooted at target symbols to score structural centrality.
// Criterion: review: "PPR converges in <=30 iterations and ranks shared utility and interface hubs higher than leaf stubs"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"router_distance_decay"},"kind":"call"},"kind":"definition","name":"router_personalized_pagerank","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"router_personalized_pagerank_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"router_personalized_pagerank"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(router-topological-pruner | tier:B | depends_on:[router-personalized-pagerank] | blocks:[router-knapsack-solver]): Compute composite importance weighting on symbol graph.
// Description: Synthesize composite node importance scores S(v|q) combining PPR centrality and distance decay, pruning zero-relevance subgraphs.
// Criterion: review: "Router ranks 1-hop and 2-hop dependent symbols higher than unrelated repository symbols"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"router_personalized_pagerank"},"kind":"call"},"kind":"definition","name":"router_topological_pruner","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"router_topological_pruner_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"router_topological_pruner"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(router-knapsack-solver | tier:B | depends_on:[router-topological-pruner] | blocks:[router-connectivity-guard]): Solve 0/1 knapsack optimization selecting symbols under token budget.
// Description: Formulate and solve knapsack optimization selecting symbol nodes to maximize total relevance score under active context token budget.
// Criterion: review: "Greedy/DP knapsack selector terminates in <50ms and satisfies sum(token_cost) <= budget"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"router_topological_pruner"},"kind":"call"},"kind":"definition","name":"router_knapsack_solver","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"router_knapsack_solver_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"router_knapsack_solver"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(router-connectivity-guard | tier:B | depends_on:[router-knapsack-solver] | blocks:[router-budget-knapsack]): Enforce path connectivity invariant on selected symbol subgraph.
// Description: Ensure all selected symbols retain valid import/call paths to target symbols to prevent disconnected orphan hallucinations.
// Criterion: review: "Allocated context contains zero orphan symbols or unresolvable type references"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"router_knapsack_solver"},"kind":"call"},"kind":"definition","name":"router_connectivity_guard","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"router_connectivity_guard_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"router_connectivity_guard"},"kind":"call"},"right":{"kind":"bool","value":true}}}

// SPEC(router-budget-knapsack | tier:B | depends_on:[router-connectivity-guard] | blocks:[ir-schema-formatter]): Implement knapsack context allocator bounded by token budget.
// Description: Select optimal symbol subgraph maximizing information coverage score while staying strictly within active context token budget.
// Criterion: review: "Router output token count stays under specified budget while maximizing coverage score"
// [lexlean:declarations]
// {"body":{"arguments":[],"function":{"name":"router_connectivity_guard"},"kind":"call"},"kind":"definition","name":"router_budget_knapsack","parameters":[],"result":{"kind":"bool"}}
// [lexlean:theorems]
// {"kind":"theorem","name":"router_budget_knapsack_verified","parameters":[],"proof":{"kind":"reflexivity"},"statement":{"kind":"eq","left":{"arguments":[],"function":{"name":"router_budget_knapsack"},"kind":"call"},"right":{"kind":"bool","value":true}}}

struct RouteResult {
    std::vector<GlobalSymbol> selected_symbols;
    std::vector<GraphEdge> selected_edges;
    int total_tokens = 0;
    int budget = 0;
    double coverage_score = 0.0;
    std::unordered_map<std::string, double> importance_scores;
};

class ContextRouter {
public:
    static std::unordered_map<std::string, double> compute_distance_decay(
        const DependencyGraph& g, const std::vector<std::string>& targets, double decay_factor = 0.5);

    static std::unordered_map<std::string, double> compute_personalized_pagerank(
        const DependencyGraph& g, const std::vector<std::string>& targets, double alpha = 0.85, int max_iter = 30);

    static std::unordered_map<std::string, double> compute_composite_scores(
        const DependencyGraph& g, const std::vector<std::string>& targets);

    static RouteResult allocate_budget(
        const DependencyGraph& g, const std::vector<std::string>& targets, int token_budget);

    static std::string to_json(const RouteResult& res);
};

} // namespace lgcc
