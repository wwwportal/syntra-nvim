use crate::graph::graph::{DependencyGraph, GraphEdge};
use crate::graph::symbol_table::GlobalSymbol;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet, VecDeque};

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

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct RouteResult {
    pub selected_symbols: Vec<GlobalSymbol>,
    pub selected_edges: Vec<GraphEdge>,
    pub total_tokens: usize,
    pub budget: usize,
    pub coverage_score: f64,
    pub importance_scores: HashMap<String, f64>,
}

pub struct ContextRouter;

impl ContextRouter {
    pub fn compute_distance_decay(
        g: &DependencyGraph,
        targets: &[String],
        decay_factor: f64,
    ) -> HashMap<String, f64> {
        let mut weights: HashMap<String, f64> = HashMap::new();
        let mut q: VecDeque<(String, usize)> = VecDeque::new();
        let mut visited: HashSet<String> = HashSet::new();

        for t in targets {
            if g.nodes.contains_key(t) {
                q.push_back((t.clone(), 0));
                visited.insert(t.clone());
                weights.insert(t.clone(), 1.0);
            }
        }

        while let Some((curr, dist)) = q.pop_front() {
            if let Some(edges) = g.adj.get(&curr) {
                for edge in edges {
                    if !visited.contains(&edge.to_fqn) {
                        visited.insert(edge.to_fqn.clone());
                        let next_dist = dist + 1;
                        weights.insert(edge.to_fqn.clone(), decay_factor.powi(next_dist as i32));
                        q.push_back((edge.to_fqn.clone(), next_dist));
                    }
                }
            }

            if let Some(rev_edges) = g.rev_adj.get(&curr) {
                if dist == 0 {
                    for edge in rev_edges {
                        if !visited.contains(&edge.from_fqn) {
                            visited.insert(edge.from_fqn.clone());
                            let next_dist = dist + 1;
                            weights.insert(
                                edge.from_fqn.clone(),
                                decay_factor.powi(next_dist as i32) * 0.75,
                            );
                            q.push_back((edge.from_fqn.clone(), next_dist));
                        }
                    }
                }
            }
        }

        weights
    }

    pub fn compute_personalized_pagerank(
        g: &DependencyGraph,
        targets: &[String],
        alpha: f64,
        max_iter: usize,
    ) -> HashMap<String, f64> {
        let mut ppr: HashMap<String, f64> = HashMap::new();
        if g.nodes.is_empty() {
            return ppr;
        }

        let mut teleport: HashMap<String, f64> = HashMap::new();
        let target_weight = if !targets.is_empty() {
            1.0 / targets.len() as f64
        } else {
            1.0 / g.nodes.len() as f64
        };

        for t in targets {
            if g.nodes.contains_key(t) {
                teleport.insert(t.clone(), target_weight);
            }
        }

        if teleport.is_empty() {
            let uniform = 1.0 / g.nodes.len() as f64;
            for fqn in g.nodes.keys() {
                teleport.insert(fqn.clone(), uniform);
            }
        }

        for (k, v) in &teleport {
            ppr.insert(k.clone(), *v);
        }

        for _ in 0..max_iter {
            let mut next_ppr: HashMap<String, f64> = HashMap::new();
            for (fqn, t_val) in &teleport {
                next_ppr.insert(fqn.clone(), (1.0 - alpha) * t_val);
            }

            for (node, score) in &ppr {
                if *score <= 0.0 {
                    continue;
                }
                if let Some(edges) = g.adj.get(node) {
                    if !edges.is_empty() {
                        let out_weight = score / edges.len() as f64;
                        for edge in edges {
                            *next_ppr.entry(edge.to_fqn.clone()).or_insert(0.0) += alpha * out_weight;
                        }
                    } else {
                        for (t_node, t_val) in &teleport {
                            *next_ppr.entry(t_node.clone()).or_insert(0.0) += alpha * score * t_val;
                        }
                    }
                } else {
                    for (t_node, t_val) in &teleport {
                        *next_ppr.entry(t_node.clone()).or_insert(0.0) += alpha * score * t_val;
                    }
                }
            }

            ppr = next_ppr;
        }

        ppr
    }

    pub fn compute_composite_scores(
        g: &DependencyGraph,
        targets: &[String],
    ) -> HashMap<String, f64> {
        let decay = Self::compute_distance_decay(g, targets, 0.5);
        let ppr = Self::compute_personalized_pagerank(g, targets, 0.85, 30);

        let mut composite = HashMap::new();
        for (fqn, _) in &g.nodes {
            let d_val = *decay.get(fqn).unwrap_or(&0.0);
            let p_val = *ppr.get(fqn).unwrap_or(&0.0);

            // Topological pruning: only keep nodes with positive structural connection
            if d_val > 0.0 || p_val > 0.001 {
                let score = 0.6 * d_val + 0.4 * (p_val * 10.0).min(1.0);
                composite.insert(fqn.clone(), score);
            }
        }

        composite
    }

    pub fn allocate_budget(
        g: &DependencyGraph,
        targets: &[String],
        token_budget: usize,
    ) -> RouteResult {
        let mut res = RouteResult {
            budget: token_budget,
            ..Default::default()
        };

        let scores = Self::compute_composite_scores(g, targets);
        res.importance_scores = scores.clone();

        let mut candidate_list: Vec<(String, f64)> = scores.into_iter().collect();
        candidate_list.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));

        let mut selected_fqns: HashSet<String> = HashSet::new();
        let mut current_tokens = 0;

        // Guaranteed inclusion of target symbols
        for t in targets {
            if let Some(sym) = g.nodes.get(t) {
                if !selected_fqns.contains(t) && current_tokens + sym.token_cost <= token_budget {
                    selected_fqns.insert(t.clone());
                    current_tokens += sym.token_cost;
                    res.selected_symbols.push(sym.clone());
                    res.coverage_score += res.importance_scores.get(t).unwrap_or(&1.0);
                }
            }
        }

        // Greedy knapsack context allocation with connectivity guard
        for (cand_fqn, score) in candidate_list {
            if selected_fqns.contains(&cand_fqn) {
                continue;
            }

            if let Some(sym) = g.nodes.get(&cand_fqn) {
                if current_tokens + sym.token_cost <= token_budget {
                    // Connectivity guard: ensure import or call path reaches selected set
                    let mut connected = selected_fqns.is_empty();
                    if !connected {
                        if let Some(edges) = g.adj.get(&cand_fqn) {
                            for e in edges {
                                if selected_fqns.contains(&e.to_fqn) {
                                    connected = true;
                                    break;
                                }
                            }
                        }
                    }
                    if !connected {
                        if let Some(rev_edges) = g.rev_adj.get(&cand_fqn) {
                            for e in rev_edges {
                                if selected_fqns.contains(&e.from_fqn) {
                                    connected = true;
                                    break;
                                }
                            }
                        }
                    }

                    if connected {
                        selected_fqns.insert(cand_fqn);
                        current_tokens += sym.token_cost;
                        res.selected_symbols.push(sym.clone());
                        res.coverage_score += score;
                    }
                }
            }
        }

        for e in &g.edges {
            if selected_fqns.contains(&e.from_fqn) && selected_fqns.contains(&e.to_fqn) {
                res.selected_edges.push(e.clone());
            }
        }

        res.total_tokens = current_tokens;
        res
    }

    pub fn to_json(res: &RouteResult) -> String {
        serde_json::to_string_pretty(res).unwrap_or_else(|_| "{}".to_string())
    }
}
