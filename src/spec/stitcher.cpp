#include "stitcher.hpp"
#include "../graph/symbol_table.hpp"
#include "syntra_spec_bridge.h"
#include <regex>
#include <queue>
#include <cstdio>
#include <array>
#include <sys/wait.h>


namespace lgcc {

struct CmdOutput {
    int exit_code = -1;
    std::string output;
};

static CmdOutput exec_cmd_capture(const std::string& cmd, const fs::path& cwd = "") {
    std::string full_cmd;
    if (!cwd.empty()) {
        full_cmd = "cd \"" + cwd.string() + "\" && " + cmd + " 2>&1";
    } else {
        full_cmd = cmd + " 2>&1";
    }
    std::array<char, 512> buffer;
    std::string result;
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return { -1, "Failed to open process pipe" };
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int status = pclose(pipe);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return { code, result };
}


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
    if (first) {
        json_ss << "{\"body\":{\"kind\":\"bool\",\"value\":true},\"kind\":\"definition\",\"name\":\"spec_root\",\"parameters\":[],\"result\":{\"kind\":\"bool\"}},";
        json_ss << "{\"kind\":\"theorem\",\"name\":\"spec_root_verified\",\"parameters\":[],\"proof\":{\"kind\":\"reflexivity\"},\"statement\":{\"kind\":\"eq\",\"left\":{\"arguments\":[],\"function\":{\"name\":\"spec_root\"},\"kind\":\"call\"},\"right\":{\"kind\":\"bool\",\"value\":true}}}";
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

FfiVerifyResult SpecStitcher::verify_ffi_contracts(const std::string& semantic_json, const std::string& module_name) {
    FfiVerifyResult res;

    // 1. Verify static C ABI invariants exported by syntra-spec-bridge
    if (prod_syntra_spec_tasks_add_spec_scaffold() == 0 ||
        prod_syntra_spec_lgcc_spec_scanner() == 0 ||
        prod_syntra_spec_lgcc_topo_spec_stitcher() == 0 ||
        prod_syntra_spec_prismpm_jit_verifier() == 0 ||
        prod_syntra_spec_prismpm_rust_bridge() == 0 ||
        prod_syntra_spec_lgcc_lexlean_verifier() == 0) {
        res.error = "Static C ABI invariant check failed in syntra_spec_bridge.";
        return res;
    }

    // 2. Verify semantic module and generate C ABI bindings via prod_ir / prod_codegen FFI
    uint32_t defs = 0;
    uint32_t thms = 0;
    std::vector<char> err_buf(4096, 0);
    int32_t status = syntra_ffi_verify_semantic_module(
        semantic_json.c_str(),
        &defs,
        &thms,
        err_buf.data(),
        err_buf.size()
    );
    if (status != 0) {
        res.error = std::string(err_buf.data());
        return res;
    }

    std::vector<char> header_buf(65536, 0);
    std::vector<char> rust_buf(131072, 0);
    int32_t abi_status = syntra_ffi_generate_c_abi(
        module_name.c_str(),
        semantic_json.c_str(),
        header_buf.data(),
        header_buf.size(),
        rust_buf.data(),
        rust_buf.size(),
        err_buf.data(),
        err_buf.size()
    );
    if (abi_status != 0) {
        res.error = std::string(err_buf.data());
        return res;
    }

    res.success = true;
    res.defs_checked = defs;
    res.theorems_checked = thms;
    res.c_header = std::string(header_buf.data());
    res.rust_source = std::string(rust_buf.data());
    return res;
}

VerifyResult SpecStitcher::verify_specs(const fs::path& repo_root, const std::string& module_name, const fs::path& specs_dir_in, bool keep_stitched) {
    VerifyResult res;

    // 1. Locate valid LexLean specs workspace
    fs::path specs_dir = specs_dir_in;
    if (specs_dir.empty() || !fs::exists(specs_dir / "lexlean.toml")) {
        std::vector<fs::path> candidates = {
            repo_root / "specs",
            repo_root.parent_path() / "prover" / "specs",
            fs::path("/home/yassine/code/prover/specs")
        };
        for (const auto& cand : candidates) {
            if (fs::exists(cand / "lexlean.toml") && fs::exists(cand / "lakefile.toml")) {
                specs_dir = cand;
                break;
            }
        }
    }

    if (specs_dir.empty() || !fs::exists(specs_dir / "lexlean.toml")) {
        res.error = "Could not locate a valid LexLean specs workspace (with lexlean.toml and lakefile.toml).";
        return res;
    }

    // 2. Locate prismpm and lexlean binaries
    std::string prismpm_bin;
    std::vector<std::string> prism_cands = {
        "/home/yassine/code/prover/vendor/prismpm/target/debug/prismpm",
        (specs_dir.parent_path() / "vendor/prismpm/target/debug/prismpm").string(),
        "prismpm"
    };
    for (const auto& c : prism_cands) {
        if (fs::exists(c)) {
            prismpm_bin = c;
            break;
        }
        auto w = exec_cmd_capture("which " + c);
        if (w.exit_code == 0 && !trim(w.output).empty()) {
            prismpm_bin = trim(w.output);
            break;
        }
    }

    std::string lexlean_bin;
    std::vector<std::string> lex_cands = {
        "/home/yassine/code/prover/vendor/prismpm/vendor/lexlean/target/debug/lexlean",
        (specs_dir.parent_path() / "vendor/prismpm/vendor/lexlean/target/debug/lexlean").string(),
        "lexlean"
    };
    for (const auto& c : lex_cands) {
        if (fs::exists(c)) {
            lexlean_bin = c;
            break;
        }
        auto w = exec_cmd_capture("which " + c);
        if (w.exit_code == 0 && !trim(w.output).empty()) {
            lexlean_bin = trim(w.output);
            break;
        }
    }

    if (prismpm_bin.empty() || lexlean_bin.empty()) {
        res.error = "Missing required prismpm or lexlean toolchain binaries.";
        return res;
    }

    // 3. Qualify module name if simple identifier was passed
    std::string full_mod_name = module_name;
    if (full_mod_name.find('.') == std::string::npos) {
        full_mod_name = "Foundation.Specification.V1." + full_mod_name;
    }

    // 4. Scan and stitch in-tree fragments
    auto frags = scan_repository(repo_root);
    auto stitch_res = stitch(frags, full_mod_name);
    if (!stitch_res.success) {
        res.error = "Specification stitch error: " + stitch_res.error;
        return res;
    }

    // 4b. Validate stitched semantic module via Rust/C FFI (prod_ir + prod_codegen::CBindings)
    auto ffi_res = verify_ffi_contracts(stitch_res.stitched_semantic_json, full_mod_name);
    if (!ffi_res.success) {
        res.error = "Rust/C FFI Contract Verifier error: " + ffi_res.error;
        return res;
    }
    res.ffi_defs_verified = ffi_res.defs_checked;
    res.ffi_theorems_verified = ffi_res.theorems_checked;

    // 5. Write stitched specification into specs workspace
    std::string rel_mod_path = full_mod_name;
    for (char& c : rel_mod_path) {
        if (c == '.') c = '/';
    }

    std::string entrypoint_rel = "src/" + rel_mod_path + ".lex.tex";
    fs::path target_file = specs_dir / entrypoint_rel;
    bool file_existed_before = fs::exists(target_file);
    std::string orig_target_content = file_existed_before ? read_file(target_file) : "";

    fs::create_directories(target_file.parent_path());
    write_file(target_file, stitch_res.stitched_lex_tex);
    res.stitched_file = target_file.string();

    // 6. Backup and update lexlean.toml with sorted entrypoints
    fs::path toml_path = specs_dir / "lexlean.toml";
    fs::path lock_path = specs_dir / "lexlean.lock";
    std::string orig_toml = read_file(toml_path);
    std::string orig_lock = fs::exists(lock_path) ? read_file(lock_path) : "";

    std::string updated_toml = orig_toml;
    auto ep_start = updated_toml.find("entrypoints = [");
    if (ep_start != std::string::npos) {
        auto ep_end = updated_toml.find("]", ep_start);
        if (ep_end != std::string::npos) {
            std::string ep_block = updated_toml.substr(ep_start, ep_end - ep_start + 1);
            std::vector<std::string> entrypoints;
            std::regex str_re("\"([^\"]+)\"");
            auto begin = std::sregex_iterator(ep_block.begin(), ep_block.end(), str_re);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                entrypoints.push_back((*it)[1].str());
            }
            entrypoints.push_back(entrypoint_rel);
            std::sort(entrypoints.begin(), entrypoints.end());
            entrypoints.erase(std::unique(entrypoints.begin(), entrypoints.end()), entrypoints.end());

            std::ostringstream new_ep;
            new_ep << "entrypoints = [\n";
            for (const auto& ep : entrypoints) {
                new_ep << "  \"" << ep << "\",\n";
            }
            new_ep << "]";
            updated_toml.replace(ep_start, ep_end - ep_start + 1, new_ep.str());
            write_file(toml_path, updated_toml);
        }
    }

    // 7. Execute LexLean fmt, lock, check, verify and PrismPM check
    std::ostringstream combined_out;
    exec_cmd_capture("\"" + lexlean_bin + "\" fmt --project \"" + toml_path.string() + "\" \"" + entrypoint_rel + "\"", specs_dir);
    auto lock_out = exec_cmd_capture("\"" + lexlean_bin + "\" lock --project \"" + toml_path.string() + "\"", specs_dir);
    combined_out << "[lexlean lock]\n" << lock_out.output;


    CmdOutput check_out, verify_out, prism_out;
    if (lock_out.exit_code == 0) {
        check_out = exec_cmd_capture("\"" + lexlean_bin + "\" check --project \"" + toml_path.string() + "\"", specs_dir);
        combined_out << "[lexlean check]\n" << check_out.output;
    }
    if (lock_out.exit_code == 0 && check_out.exit_code == 0) {
        verify_out = exec_cmd_capture("\"" + lexlean_bin + "\" verify --project \"" + toml_path.string() + "\"", specs_dir);
        combined_out << "[lexlean verify]\n" << verify_out.output;
    }
    if (lock_out.exit_code == 0 && check_out.exit_code == 0 && verify_out.exit_code == 0) {
        prism_out = exec_cmd_capture("\"" + prismpm_bin + "\" check -p \"" + specs_dir.string() + "\"", specs_dir);
        combined_out << "[prismpm check]\n" << prism_out.output;
    }

    res.output = combined_out.str();

    // 8. Restore workspace state unless --keep was requested
    if (!keep_stitched) {
        write_file(toml_path, orig_toml);
        if (!orig_lock.empty()) {
            write_file(lock_path, orig_lock);
        }
        if (file_existed_before) {
            write_file(target_file, orig_target_content);
        } else {
            std::error_code ec;
            fs::remove(target_file, ec);
        }
    }

    // 9. Parse verification metadata
    if (lock_out.exit_code == 0 && check_out.exit_code == 0 && verify_out.exit_code == 0 && prism_out.exit_code == 0) {
        res.success = true;
        std::smatch vm, pm;
        std::regex verify_re(R"(verified\s+(\d+)\s+modules;\s+attestation\s+([0-9a-fA-F]+))");
        if (std::regex_search(verify_out.output, vm, verify_re)) {
            res.modules_verified = std::stoi(vm[1].str());
            res.attestation = vm[2].str();
        }
        std::regex prism_re(R"(semantic ID:\s*([0-9a-fA-F]+))");
        if (std::regex_search(prism_out.output, pm, prism_re)) {
            res.semantic_id = pm[1].str();
        }
    } else {
        res.success = false;
        res.error = "Machine-checked verification failed.";
    }

    return res;
}

} // namespace lgcc

