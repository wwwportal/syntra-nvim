#include "common.hpp"
#include "ast/extractor.hpp"
#include "ast/skeletonizer.hpp"
#include "graph/symbol_table.hpp"
#include "graph/graph.hpp"
#include "router/router.hpp"
#include "spec/stitcher.hpp"
#include "spec/linter.hpp"

using namespace lgcc;

// TODO(lgcc-dep-bundle | tier:A | depends_on:[ir-contract-skeletonizer] | blocks:[lgcc-task-synthesizer, graph-repo-scanner]): Compile hermetic dependency context and type signatures into intermediate representation before model invocation.
// Description: Preprocess target files by bundling imported interfaces, external function stubs, and type definitions instead of raw binary streams or isolated files.
// Criterion: review: "lgcc CLI accepts --bundle flag that aggregates local imported module stubs without raw binary degradation"

// TODO(ir-schema-formatter | tier:B | depends_on:[router-budget-knapsack] | blocks:[lgcc-cli-router-hook]): Emit unified compressed context IR schema for model prompt.
// Description: Format target file, budget-allocated symbol skeletons, and dependency topology into compact prompt intermediate representation.
// Criterion: review: "Formatted IR stream preserves all type contracts while achieving >70% compression"

std::string bundle_target(const fs::path& target_path) {
    auto target_abs = fs::absolute(target_path);
    std::string target_content = read_file(target_abs);
    auto lines = split_lines(target_content);

    std::ostringstream bundle;
    bundle << "==============================================================================\n";
    bundle << "TARGET FILE: " << target_abs.string() << "\n";
    bundle << "==============================================================================\n";
    for (size_t i = 0; i < lines.size(); ++i) {
        bundle << (i + 1) << ": " << lines[i] << "\n";
    }
    bundle << "\n";

    // Extract imports and discover local dependencies
    auto mod = Extractor::extract(target_abs);
    auto repo_root = target_abs.parent_path();
    while (repo_root.has_parent_path() && !fs::exists(repo_root / ".git")) {
        repo_root = repo_root.parent_path();
    }

    std::unordered_map<std::string, std::string> dep_skeletons;
    for (const auto& imp : mod.imports) {
        std::string mod_stem = fs::path(imp.module).stem().string();
        // Candidate file paths
        std::vector<fs::path> candidates = {
            target_abs.parent_path() / (mod_stem + ".lua"),
            target_abs.parent_path() / (mod_stem + ".py"),
            target_abs.parent_path() / (mod_stem + ".hpp"),
            target_abs.parent_path() / (mod_stem + ".h"),
            repo_root / "nvim/lua" / (mod_stem + ".lua"),
            repo_root / "src" / (mod_stem + ".hpp"),
            repo_root / "src" / (mod_stem + ".h")
        };

        for (const auto& cand : candidates) {
            if (fs::exists(cand) && cand != target_abs) {
                if (!dep_skeletons.count(cand.string())) {
                    dep_skeletons[cand.string()] = Skeletonizer::skeletonize(cand);
                }
                break;
            }
        }
    }

    if (!dep_skeletons.empty()) {
        bundle << "==============================================================================\n";
        bundle << "DEPENDENCY SKELETONS & INTERFACE CONTRACTS (" << dep_skeletons.size() << " local module(s))\n";
        bundle << "==============================================================================\n";
        for (const auto& [dep_file, skel] : dep_skeletons) {
            bundle << "\n--- INTERFACE CONTRACT: " << dep_file << " ---\n";
            bundle << skel << "\n";
            bundle << "--- END CONTRACT ---\n";
        }
    }

    return bundle.str();
}

std::string format_ir_bundle(const fs::path& target_path, const RouteResult& res) {
    auto target_abs = fs::absolute(target_path);
    std::string target_content = read_file(target_abs);
    auto lines = split_lines(target_content);

    std::ostringstream bundle;
    bundle << "==============================================================================\n";
    bundle << "TARGET FILE: " << target_abs.string() << "\n";
    bundle << "==============================================================================\n";
    for (size_t i = 0; i < lines.size(); ++i) {
        bundle << (i + 1) << ": " << lines[i] << "\n";
    }
    bundle << "\n";

    std::vector<std::string> unique_files;
    std::unordered_set<std::string> seen_files;
    for (const auto& sym : res.selected_symbols) {
        std::string fstr = sym.filepath.string();
        if (sym.filepath != target_abs && !seen_files.count(fstr)) {
            seen_files.insert(fstr);
            unique_files.push_back(fstr);
        }
    }

    if (!unique_files.empty()) {
        bundle << "==============================================================================\n";
        bundle << "ROUTED CONTEXT IR (" << res.selected_symbols.size() 
               << " symbols, " << res.total_tokens << " tokens / budget " << res.budget << ")\n";
        bundle << "==============================================================================\n";
        for (const auto& f : unique_files) {
            bundle << "\n--- INTERFACE CONTRACT: " << f << " ---\n";
            bundle << Skeletonizer::skeletonize(f) << "\n";
            bundle << "--- END CONTRACT ---\n";
        }
    }

    return bundle.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: lgcc [--extract <file> | --skeletonize <file> | --bundle <file> | "
                  << "--graph [root] | --route <file> --budget <N> | --metrics <file> | --scan-repo [root] | "
                  << "--lint-tasks [--json] [root] | --scan-specs [root] | --stitch-specs [mod] [root] | --verify-specs [mod] [root]]\n";
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "--extract" && argc >= 3) {
        fs::path p = argv[2];
        if (!fs::exists(p)) {
            std::cerr << "Error: File not found: " << p << "\n";
            return 1;
        }
        auto mod = Extractor::extract(p);
        std::cout << Extractor::to_json(mod);
        return 0;
    }

    if (mode == "--skeletonize" && argc >= 3) {
        fs::path p = argv[2];
        if (!fs::exists(p)) {
            std::cerr << "Error: File not found: " << p << "\n";
            return 1;
        }
        std::cout << Skeletonizer::skeletonize(p);
        return 0;
    }

    if (mode == "--bundle" && argc >= 3) {
        fs::path p = argv[2];
        if (!fs::exists(p)) {
            std::cerr << "Error: File not found: " << p << "\n";
            return 1;
        }
        std::cout << bundle_target(p);
        return 0;
    }

    if (mode == "--metrics" && argc >= 3) {
        fs::path p = argv[2];
        if (!fs::exists(p)) {
            std::cerr << "Error: File not found: " << p << "\n";
            return 1;
        }
        auto m = Skeletonizer::compute_metrics(p);
        std::cout << "Metrics for " << p.string() << ":\n";
        std::cout << "  Original:  " << m.orig_chars << " chars | " << m.orig_tokens << " tokens | " << m.orig_lines << " lines\n";
        std::cout << "  Skeleton:  " << m.skel_chars << " chars | " << m.skel_tokens << " tokens | " << m.skel_lines << " lines\n";
        std::cout << "  Reduction: " << m.char_reduction << "% chars | " << m.token_reduction << "% tokens\n";
        return 0;
    }

    if (mode == "--scan-repo") {
        fs::path root = (argc >= 3) ? argv[2] : fs::current_path();
        auto files = SymbolTable::scan_repository(root);
        for (const auto& f : files) {
            std::cout << f.string() << "\n";
        }
        return 0;
    }

    if (mode == "--graph") {
        fs::path root = (argc >= 3) ? argv[2] : fs::current_path();
        auto g = GraphBuilder::build_graph(root);
        std::cout << GraphBuilder::to_json(g);
        return 0;
    }

    if (mode == "--route" && argc >= 3) {
        fs::path target = argv[2];
        int budget = 4000;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--budget" && i + 1 < argc) {
                budget = std::stoi(argv[i + 1]);
                break;
            }
        }
        fs::path root = target.parent_path();
        while (root.has_parent_path() && !fs::exists(root / ".git")) {
            root = root.parent_path();
        }
        auto g = GraphBuilder::build_graph(root);
        std::string stem = target.stem().string();
        std::vector<std::string> target_symbols;
        for (const auto& [fqn, _] : g.nodes) {
            if (starts_with(fqn, stem + ".")) {
                target_symbols.push_back(fqn);
            }
        }
        auto res = ContextRouter::allocate_budget(g, target_symbols, budget);

        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--format" && i + 1 < argc && std::string(argv[i + 1]) == "ir") {
                std::cout << format_ir_bundle(target, res);
                return 0;
            }
        }

        std::cout << ContextRouter::to_json(res);
        return 0;
    }

    if (mode == "--scan-specs") {
        fs::path root = (argc >= 3) ? argv[2] : fs::current_path();
        auto frags = SpecStitcher::scan_repository(root);
        std::cout << "Discovered " << frags.size() << " in-tree specification fragments:\n";
        for (const auto& f : frags) {
            std::cout << "  ● [" << f.tier << "] " << f.id << " (" << f.source_file.string() << ":" << f.line << ")\n";
            std::cout << "    Desc: " << f.description << "\n";
            if (!f.declarations_json.empty()) {
                std::cout << "    [lexlean:declarations] (" << f.declarations_json.size() << " bytes)\n";
            }
            if (!f.theorems_json.empty()) {
                std::cout << "    [lexlean:theorems] (" << f.theorems_json.size() << " bytes)\n";
            }
        }
        return 0;
    }

    if (mode == "--stitch-specs") {
        fs::path root = fs::current_path();
        std::string mod_name = "Syntra.StitchedSpec";
        if (argc >= 3) mod_name = argv[2];
        if (argc >= 4) root = argv[3];

        auto frags = SpecStitcher::scan_repository(root);
        auto res = SpecStitcher::stitch(frags, mod_name);
        if (!res.success) {
            std::cerr << "Error stitching specifications: " << res.error << "\n";
            return 1;
        }
        std::cout << res.stitched_lex_tex;
        return 0;
    }

    if (mode == "--verify-specs" || mode == "--verify") {
        fs::path root = fs::current_path();
        std::string mod_name = "Foundation.Specification.V1.JITSpec";
        bool keep = false;

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--keep" || arg == "-k") {
                keep = true;
            } else if (arg.rfind("--", 0) != 0) {
                if (mod_name == "Foundation.Specification.V1.JITSpec") {
                    mod_name = arg;
                } else {
                    root = arg;
                }
            }
        }

        auto res = SpecStitcher::verify_specs(root, mod_name, "", keep);
        if (!res.success) {
            std::cerr << "Verification failed: " << res.error << "\n";
            if (!res.output.empty()) {
                std::cerr << res.output << "\n";
            }
            return 1;
        }

        std::cout << "● Machine-Checked Proof Replay Succeeded!\n";
        std::cout << "  Module:           " << mod_name << "\n";
        std::cout << "  Modules Verified: " << res.modules_verified << "\n";
        if (!res.semantic_id.empty()) {
            std::cout << "  Semantic ID:      " << res.semantic_id << "\n";
        }
        if (!res.attestation.empty()) {
            std::cout << "  Attestation:      " << res.attestation << "\n";
        }
        std::cout << "  PrismPM Status:   Sound (Zero unproven obligations)\n";
        return 0;
    }

    if (mode == "--lint-tasks" || mode == "--lint") {
        fs::path root = fs::current_path();
        bool json_mode = false;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--json") {
                json_mode = true;
            } else if (arg.rfind("--", 0) != 0) {
                root = arg;
            }
        }
        auto res = TaskLinter::lint_repository(root);
        if (json_mode) {
            std::cout << TaskLinter::to_json(res) << "\n";
        } else {
            TaskLinter::print_report(res);
        }
        return res.errors.empty() ? 0 : 1;
    }

    std::cerr << "Unknown mode or missing arguments: " << mode << "\n";
    return 1;
}
