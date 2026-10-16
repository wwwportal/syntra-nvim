#include "router.hpp"

namespace lgcc {

std::unordered_map<std::string, double> ContextRouter::compute_distance_decay(
    const DependencyGraph& g, const std::vector<std::string>& targets, double decay_factor) {
    
    std::unordered_map<std::string, double> weights;
    std::queue<std::pair<std::string, int>> q;
    std::unordered_set<std::string> visited;

    for (const auto& t : targets) {
        if (g.nodes.count(t)) {
            q.push({t, 0});
            visited.insert(t);
            weights[t] = 1.0;
        }
    }

    while (!q.empty()) {
        auto [curr, dist] = q.front();
        q.pop();

        // Outgoing dependencies (callees / referenced types)
        auto it = g.adj.find(curr);
        if (it != g.adj.end()) {
            for (const auto& edge : it->second) {
                if (!visited.count(edge.to_fqn)) {
                    visited.insert(edge.to_fqn);
                    int next_dist = dist + 1;
                    weights[edge.to_fqn] = std::pow(decay_factor, next_dist);
                    q.push({edge.to_fqn, next_dist});
                }
            }
        }
        // Incoming callers (1-hop callers at slightly discounted decay)
        auto rev_it = g.rev_adj.find(curr);
        if (rev_it != g.rev_adj.end() && dist == 0) {
            for (const auto& edge : rev_it->second) {
                if (!visited.count(edge.from_fqn)) {
                    visited.insert(edge.from_fqn);
                    int next_dist = dist + 1;
                    weights[edge.from_fqn] = std::pow(decay_factor, next_dist) * 0.75;
                    q.push({edge.from_fqn, next_dist});
                }
            }
        }
    }

    return weights;
}

std::unordered_map<std::string, double> ContextRouter::compute_personalized_pagerank(
    const DependencyGraph& g, const std::vector<std::string>& targets, double alpha, int max_iter) {

    std::unordered_map<std::string, double> p;
    std::unordered_map<std::string, double> p0;

    size_t N = g.nodes.size();
    if (N == 0) return p;

    double init_val = targets.empty() ? (1.0 / N) : (1.0 / targets.size());
    for (const auto& t : targets) {
        if (g.nodes.count(t)) {
            p0[t] = init_val;
            p[t] = init_val;
        }
    }
    if (p0.empty()) {
        for (const auto& [fqn, _] : g.nodes) {
            p0[fqn] = 1.0 / N;
            p[fqn] = 1.0 / N;
        }
    }

    for (int iter = 0; iter < max_iter; ++iter) {
        std::unordered_map<std::string, double> next_p;
        for (const auto& [node, base_prob] : p0) {
            next_p[node] = (1.0 - alpha) * base_prob;
        }

        for (const auto& [src, edges] : g.adj) {
            double src_prob = p[src];
            if (src_prob > 0 && !edges.empty()) {
                double total_w = 0.0;
                for (const auto& e : edges) total_w += e.weight;
                if (total_w <= 0.0) total_w = static_cast<double>(edges.size());
                for (const auto& e : edges) {
                    next_p[e.to_fqn] += (alpha * src_prob) * (e.weight / total_w);
                }
            }
        }

        p = std::move(next_p);
    }

    return p;
}

std::unordered_map<std::string, double> ContextRouter::compute_composite_scores(
    const DependencyGraph& g, const std::vector<std::string>& targets) {

    auto dist_weights = compute_distance_decay(g, targets);
    auto ppr_weights = compute_personalized_pagerank(g, targets);

    std::unordered_map<std::string, double> scores;
    for (const auto& [fqn, _] : g.nodes) {
        // router-topological-pruner: prune disconnected zero-relevance subgraphs
        if (!dist_weights.count(fqn)) {
            continue;
        }
        double d = dist_weights[fqn];
        double p = ppr_weights.count(fqn) ? ppr_weights[fqn] : 0.0;
        scores[fqn] = 0.6 * d + 0.4 * p;
    }

    return scores;
}

RouteResult ContextRouter::allocate_budget(
    const DependencyGraph& g, const std::vector<std::string>& targets, int token_budget) {

    RouteResult res;
    res.budget = token_budget;
    res.importance_scores = compute_composite_scores(g, targets);

    std::unordered_set<std::string> selected_ids;
    int current_tokens = 0;

    // First pass: Always include target symbols
    for (const auto& t : targets) {
        auto it = g.nodes.find(t);
        if (it != g.nodes.end() && !selected_ids.count(t)) {
            if (current_tokens + it->second.token_cost <= token_budget) {
                res.selected_symbols.push_back(it->second);
                current_tokens += it->second.token_cost;
                selected_ids.insert(t);
                res.coverage_score += res.importance_scores[t];
            }
        }
    }

    // Sort candidate symbols by density = importance / max(1, token_cost)
    std::vector<std::pair<std::string, double>> ranking;
    ranking.reserve(res.importance_scores.size());

    for (const auto& [fqn, score] : res.importance_scores) {
        if (selected_ids.count(fqn) || score <= 0.0) continue;
        auto it = g.nodes.find(fqn);
        int cost = (it != g.nodes.end()) ? std::max(1, it->second.token_cost) : 1;
        double density = score / cost;
        ranking.push_back({fqn, density});
    }

    std::sort(ranking.begin(), ranking.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.second - b.second) > 1e-9) return a.second > b.second;
        return a.first < b.first;
    });

    // Helper for router-connectivity-guard: check if candidate connects to an already-selected symbol
    auto is_connected_to_selected = [&](const std::string& cand) -> bool {
        if (selected_ids.empty()) return true;
        auto rev_it = g.rev_adj.find(cand);
        if (rev_it != g.rev_adj.end()) {
            for (const auto& e : rev_it->second) {
                if (selected_ids.count(e.from_fqn)) return true;
            }
        }
        auto adj_it = g.adj.find(cand);
        if (adj_it != g.adj.end()) {
            for (const auto& e : adj_it->second) {
                if (selected_ids.count(e.to_fqn)) return true;
            }
        }
        return false;
    };

    // Multi-pass knapsack with connectivity guard so 1-hop, 2-hop, ..., k-hop chains remain connected
    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto& [fqn, _] : ranking) {
            if (selected_ids.count(fqn)) continue;
            if (!is_connected_to_selected(fqn)) continue;
            auto it = g.nodes.find(fqn);
            if (it != g.nodes.end()) {
                int cost = it->second.token_cost;
                if (current_tokens + cost <= token_budget) {
                    res.selected_symbols.push_back(it->second);
                    current_tokens += cost;
                    selected_ids.insert(fqn);
                    res.coverage_score += res.importance_scores[fqn];
                    progress = true;
                }
            }
        }
    }

    // Extract induced Code Graph edges over selected_ids
    for (const auto& e : g.edges) {
        if (selected_ids.count(e.from_fqn) && selected_ids.count(e.to_fqn)) {
            res.selected_edges.push_back(e);
        }
    }

    res.total_tokens = current_tokens;
    return res;
}

std::string ContextRouter::to_json(const RouteResult& res) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"budget\": " << res.budget << ",\n";
    ss << "  \"total_tokens\": " << res.total_tokens << ",\n";
    ss << "  \"coverage_score\": " << res.coverage_score << ",\n";
    ss << "  \"selected_count\": " << res.selected_symbols.size() << ",\n";
    ss << "  \"edge_count\": " << res.selected_edges.size() << ",\n";
    ss << "  \"symbols\": [\n";
    for (size_t i = 0; i < res.selected_symbols.size(); ++i) {
        const auto& s = res.selected_symbols[i];
        ss << "    {\n";
        ss << "      \"name\": \"" << escape_json(s.fqn) << "\",\n";
        ss << "      \"kind\": \"" << escape_json(s.kind) << "\",\n";
        ss << "      \"file\": \"" << escape_json(s.filepath.string()) << "\",\n";
        ss << "      \"line\": " << s.line << ",\n";
        ss << "      \"end_line\": " << s.end_line << ",\n";
        ss << "      \"tokens\": " << s.token_cost << ",\n";
        ss << "      \"signature\": \"" << escape_json(s.signature) << "\"\n";
        ss << "    }" << (i + 1 < res.selected_symbols.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";
    ss << "  \"edges\": [\n";
    for (size_t i = 0; i < res.selected_edges.size(); ++i) {
        const auto& e = res.selected_edges[i];
        ss << "    {\n";
        ss << "      \"source\": \"" << escape_json(e.from_fqn) << "\",\n";
        ss << "      \"target\": \"" << escape_json(e.to_fqn) << "\",\n";
        ss << "      \"kind\": \"" << escape_json(e.kind) << "\"\n";
        ss << "    }" << (i + 1 < res.selected_edges.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

} // namespace lgcc
