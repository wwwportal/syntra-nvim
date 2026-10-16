#include "stitcher.hpp"
#include "../graph/symbol_table.hpp"
#include <regex>
#include <queue>

namespace lgcc {

static std::string strip_comment_prefix(std::string_view line) {
    auto s = trim(line);
    if (starts_with(s, "//")) s = trim(s.substr(2));
    else if (starts_with(s, "--")) s = trim(s.substr(2));
    else if (starts_with(s, "#")) s = trim(s.substr(1));
    return std::string(s);
}

std::vector<SpecFragment> SpecStitcher::scan_file(const fs::path& filepath) {
    std::vector<SpecFragment> fragments;
    std::string content = read_file(filepath);
    auto lines = split_lines(content);

    // Matches // SPEC(id | tier:A | ...) or // TODO(id | tier:A | ...)
    std::regex spec_header_re(
        R"(^[ \t]*(?:#|//|--)\s*(SPEC|TODO|FIXME)\(([a-zA-Z0-9_\-]+)\s*\|\s*tier:([ABC])((?:\s*\|\s*(?:blocks|depends_on):\[[a-zA-Z0-9_\-,\s]*\])*)\):(?:\s*(.*))?$)"
    );
    std::regex desc_re(R"(^[ \t]*(?:#|//|--)\s*Description:\s*(.+)$)");
    std::regex crit_re(R"(^[ \t]*(?:#|//|--)\s*Criterion:\s*(assert:\s*.+|review:\s*.+)$)");
    std::regex decl_tag_re(R"(^[ \t]*(?:#|//|--)\s*\[lexlean:declarations\]\s*$)");
    std::regex thm_tag_re(R"(^[ \t]*(?:#|//|--)\s*\[lexlean:(?:theorems|theorem)\]\s*$)");

    for (size_t i = 0; i < lines.size(); ++i) {
        std::smatch m;
        if (std::regex_match(lines[i], m, spec_header_re)) {
            SpecFragment frag;
            frag.id = m[2].str();
            frag.tier = m[3].str();
            frag.source_file = filepath;
            frag.line = static_cast<int>(i + 1);

            std::string attrs = m[4].str();
            std::string inline_desc = m[5].str();
            if (!inline_desc.empty()) frag.description = trim(inline_desc);

            // Parse blocks and depends_on
            std::regex blocks_re(R"(blocks:\[([a-zA-Z0-9_\-,\s]*)\])");
            std::regex deps_re(R"(depends_on:\[([a-zA-Z0-9_\-,\s]*)\])");
            std::smatch bm, dm;
            if (std::regex_search(attrs, bm, blocks_re)) {
                auto b_str = bm[1].str();
                std::stringstream ss(b_str);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim(item);
                    if (!item.empty()) frag.blocks.push_back(item);
                }
            }
            if (std::regex_search(attrs, dm, deps_re)) {
                auto d_str = dm[1].str();
                std::stringstream ss(d_str);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim(item);
                    if (!item.empty()) frag.depends_on.push_back(item);
                }
            }

            // Scan following lines for Description, Criterion, and LexLean payloads
            size_t j = i + 1;
            enum class CaptureMode { None, Declarations, Theorems };
            CaptureMode mode = CaptureMode::None;
            std::ostringstream decl_ss;
            std::ostringstream thm_ss;

            while (j < lines.size()) {
                const auto& cur = lines[j];
                auto trimmed = trim(cur);

                // Stop if we hit a non-comment line or another SPEC/TODO tag
                if (!starts_with(trimmed, "//") && !starts_with(trimmed, "--") && !starts_with(trimmed, "#")) {
                    break;
                }
                if (std::regex_match(cur, spec_header_re)) {
                    break;
                }

                if (std::regex_match(cur, decl_tag_re)) {
                    mode = CaptureMode::Declarations;
                    j++;
                    continue;
                }
                if (std::regex_match(cur, thm_tag_re)) {
                    mode = CaptureMode::Theorems;
                    j++;
                    continue;
                }

                std::smatch dm2, cm2;
                if (mode == CaptureMode::None) {
                    if (std::regex_match(cur, dm2, desc_re)) {
                        frag.description = trim(dm2[1].str());
                        j++;
                        continue;
                    }
                    if (std::regex_match(cur, cm2, crit_re)) {
                        frag.criterion = trim(cm2[1].str());
                        j++;
                        continue;
                    }
                }

                // Append lines to active capture mode
                std::string stripped = strip_comment_prefix(cur);
                if (mode == CaptureMode::Declarations) {
                    decl_ss << stripped << "\n";
                } else if (mode == CaptureMode::Theorems) {
                    thm_ss << stripped << "\n";
                }
                j++;
            }

            frag.declarations_json = trim(decl_ss.str());
            frag.theorems_json = trim(thm_ss.str());
            fragments.push_back(frag);
        }
    }

    return fragments;
}

std::vector<SpecFragment> SpecStitcher::scan_repository(const fs::path& repo_root) {
    std::vector<SpecFragment> all_frags;
    auto files = SymbolTable::scan_repository(repo_root);
    for (const auto& file : files) {
        auto frags = scan_file(file);
        all_frags.insert(all_frags.end(), frags.begin(), frags.end());
    }
    return all_frags;
}

StitchResult SpecStitcher::stitch(const std::vector<SpecFragment>& fragments, const std::string& module_name) {
    StitchResult result;
    result.fragments = fragments;

    std::unordered_map<std::string, const SpecFragment*> frag_map;
    for (const auto& f : fragments) {
        if (frag_map.count(f.id)) {
            result.success = false;
            result.error = "Duplicate specification fragment ID: " + f.id;
            return result;
        }
        frag_map[f.id] = &f;
    }

    // Build directed graph: u -> v means u must come before v (u is prerequisite of v)
    // If v depends_on u, then edge u -> v.
    // If u blocks v, then edge u -> v.
    std::unordered_map<std::string, std::unordered_set<std::string>> outgoing;
    std::unordered_map<std::string, int> in_degree;
    for (const auto& f : fragments) {
        if (!in_degree.count(f.id)) in_degree[f.id] = 0;
    }

    for (const auto& f : fragments) {
        // v depends_on u: u -> v
        for (const auto& dep : f.depends_on) {
            if (frag_map.count(dep)) {
                if (outgoing[dep].insert(f.id).second) {
                    in_degree[f.id]++;
                }
            }
        }
        // u blocks v: u -> v
        for (const auto& blk : f.blocks) {
            if (frag_map.count(blk)) {
                if (outgoing[f.id].insert(blk).second) {
                    in_degree[blk]++;
                }
            }
        }
    }

    // Kahn's algorithm for topological sorting
    std::queue<std::string> q;
    for (const auto& [id, deg] : in_degree) {
        if (deg == 0) q.push(id);
    }

    std::vector<std::string> order;
    while (!q.empty()) {
        std::string curr = q.front();
        q.pop();
        order.push_back(curr);

        for (const auto& nxt : outgoing[curr]) {
            in_degree[nxt]--;
            if (in_degree[nxt] == 0) {
                q.push(nxt);
            }
        }
    }

    if (order.size() != fragments.size()) {
        result.success = false;
        result.error = "Cycle detected in specification fragment dependency DAG";
        return result;
    }

    result.topo_order = order;

    // Stitch semantic declarations in topological order
    std::vector<std::string> all_declarations;
    for (const auto& id : order) {
        const auto* frag = frag_map[id];
        if (!frag->declarations_json.empty()) {
            all_declarations.push_back(frag->declarations_json);
        }
        if (!frag->theorems_json.empty()) {
            all_declarations.push_back(frag->theorems_json);
        }
    }

    // Build unified JSON
    std::ostringstream json_ss;
    json_ss << "{\"declarations\":[";
    bool first = true;
    for (const auto& d : all_declarations) {
        std::string raw = trim(d);
        if (raw.empty()) continue;
        // Strip outer array brackets if present
        if (starts_with(raw, "[") && ends_with(raw, "]")) {
            raw = trim(raw.substr(1, raw.size() - 2));
        }
        if (raw.empty()) continue;

        if (!first) json_ss << ",";
        json_ss << raw;
        first = false;
    }
    json_ss << "],\"spec\":\"lexlean/semantic-module/1\"}";
    result.stitched_semantic_json = json_ss.str();

    // Wrap in LexLean document
    std::ostringstream doc_ss;
    doc_ss << "\\begin{lexlean}{" << module_name << "}\n";
    doc_ss << "\\useglossary{lexlean.std.bool@1.1.0}\n";
    doc_ss << "\\useglossary{lexlean.std.nat@1.1.0}\n";
    doc_ss << "\\title{Boolean}\n";
    doc_ss << "\\begin{semanticmodule}\n";
    doc_ss << "\\semanticdata{" << result.stitched_semantic_json << "}\n";
    doc_ss << "\\end{semanticmodule}\n";
    doc_ss << "\\end{lexlean}\n";
    result.stitched_lex_tex = doc_ss.str();

    result.success = true;
    return result;
}

} // namespace lgcc
