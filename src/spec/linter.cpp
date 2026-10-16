#include "linter.hpp"
#include "stitcher.hpp"
#include <regex>
#include <cstdio>
#include <array>


namespace lgcc {

static const std::unordered_set<std::string> SCAN_EXTENSIONS = {
    ".lua", ".sh", ".bash", ".py", ".cpp", ".hpp", ".h", ".cc", ".cxx"
};

static const std::unordered_set<std::string> IGNORE_DIRS = {
    ".git", "build", "target", "__pycache__", ".venv", "venv", "vendor", ".lazy"
};

static std::string exec_command(const std::string& cmd, const fs::path& cwd) {
    std::string full_cmd = "cd \"" + cwd.string() + "\" && " + cmd + " 2>/dev/null";
    std::array<char, 256> buffer;
    std::string result;
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

void TaskLinter::scan_file(const fs::path& filepath, const fs::path& repo_root, std::vector<TaskRecord>& tasks, std::vector<std::string>& errors) {
    std::string content = read_file(filepath);
    auto lines = split_lines(content);
    std::string rel_path = fs::relative(filepath, repo_root).string();

    std::regex task_header_re(
        R"(^[ \t]*(?:#|//|--)\s*(TODO|FIXME|SPEC)\(([a-zA-Z0-9_\-]+)\s*\|\s*tier:([ABC])((?:\s*\|\s*(?:blocks|depends_on):\[[a-zA-Z0-9_\-,\s]*\])*)\):(?:\s*(.*))?$)"
    );
    std::regex lazy_todo_re(
        R"(^[ \t]*(?:#|//|--)\s*(TODO|FIXME|SPEC)(?!\([a-zA-Z0-9_\-]+\s*\|).*$)",
        std::regex_constants::icase
    );
    std::regex desc_re(R"(^[ \t]*(?:#|//|--)\s*Description:\s*(.+)$)");
    std::regex crit_re(R"(^[ \t]*(?:#|//|--)\s*Criterion:\s*(assert:\s*.+|review:\s*.+)$)");
    std::regex comment_re(R"(^[ \t]*(?:#|//|--))");

    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];

        // Check for lazy / untyped TODOs
        if (std::regex_match(line, lazy_todo_re)) {
            if (!std::regex_match(line, task_header_re)) {
                errors.push_back(
                    rel_path + ":" + std::to_string(i + 1) + ": Unstructured task comment.\n" +
                    "  Found: " + trim(line) + "\n" +
                    "  Must match: -- TODO(<id> | tier:<A|B|C> [| blocks:[...]] [| depends_on:[...]]):"
                );
                continue;
            }
        }

        std::smatch m;
        if (std::regex_match(line, m, task_header_re)) {
            TaskRecord rec;
            rec.kind = m[1].str();
            rec.id = m[2].str();
            rec.tier = m[3].str();
            rec.file = rel_path;
            rec.line = static_cast<int>(i + 1);

            std::string attrs_str = m[4].str();
            std::string inline_desc = m[5].str();
            if (!inline_desc.empty()) rec.description = trim(inline_desc);

            std::regex blocks_re(R"(blocks:\[([a-zA-Z0-9_\-,\s]*)\])");
            std::regex deps_re(R"(depends_on:\[([a-zA-Z0-9_\-,\s]*)\])");
            std::smatch bm, dm;
            if (std::regex_search(attrs_str, bm, blocks_re)) {
                std::stringstream ss(bm[1].str());
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim(item);
                    if (!item.empty()) rec.blocks.push_back(item);
                }
            }
            if (std::regex_search(attrs_str, dm, deps_re)) {
                std::stringstream ss(dm[1].str());
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim(item);
                    if (!item.empty()) rec.depends_on.push_back(item);
                }
            }

            // Lookahead for Description: and Criterion: up to 5 lines
            size_t j = i + 1;
            while (j < lines.size() && j <= i + 5) {
                const auto& next_line = lines[j];
                std::smatch dm2, cm2;
                if (std::regex_match(next_line, dm2, desc_re)) {
                    rec.description = trim(dm2[1].str());
                }
                if (std::regex_match(next_line, cm2, crit_re)) {
                    rec.criterion = trim(cm2[1].str());
                }
                if (!rec.description.empty() && !rec.criterion.empty()) {
                    break;
                }
                if (!std::regex_search(next_line, comment_re)) {
                    break;
                }
                j++;
            }

            if (rec.description.empty()) {
                errors.push_back(rel_path + ":" + std::to_string(i + 1) + ": Task '" + rec.id + "' missing 'Description: <summary>'");
            }
            if (rec.criterion.empty()) {
                errors.push_back(rel_path + ":" + std::to_string(i + 1) + ": Task '" + rec.id + "' missing 'Criterion: assert: ...' or 'Criterion: review: ...'");
            }

            tasks.push_back(rec);
        }
    }
}

std::vector<std::string> TaskLinter::validate_dag(const std::vector<TaskRecord>& tasks) {
    std::vector<std::string> errors;
    std::unordered_map<std::string, const TaskRecord*> task_ids;
    std::unordered_set<std::string> seen;

    for (const auto& t : tasks) {
        if (seen.count(t.id)) {
            errors.push_back("Duplicate task ID '" + t.id + "' found at " + t.file.string() + ":" + std::to_string(t.line));
        }
        seen.insert(t.id);
        task_ids[t.id] = &t;
    }

    for (const auto& t : tasks) {
        for (const auto& b : t.blocks) {
            if (!task_ids.count(b)) {
                errors.push_back(t.file.string() + ":" + std::to_string(t.line) + ": Task '" + t.id + "' blocks non-existent task '" + b + "'");
            }
        }
        for (const auto& d : t.depends_on) {
            if (!task_ids.count(d)) {
                errors.push_back(t.file.string() + ":" + std::to_string(t.line) + ": Task '" + t.id + "' depends on non-existent task '" + d + "'");
            }
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> adj;
    for (const auto& t : tasks) {
        adj[t.id] = t.blocks;
    }

    std::unordered_map<std::string, int> visited; // 0=unvisited, 1=visiting, 2=visited
    auto dfs = [&](auto& self, const std::string& node, std::vector<std::string>& path) -> void {
        visited[node] = 1;
        for (const auto& nxt : adj[node]) {
            if (visited[nxt] == 1) {
                std::string cycle_str;
                for (const auto& p : path) cycle_str += p + " -> ";
                cycle_str += nxt;
                errors.push_back("Cyclic dependency detected: " + cycle_str);
            } else if (visited[nxt] == 0) {
                path.push_back(nxt);
                self(self, nxt, path);
                path.pop_back();
            }
        }
        visited[node] = 2;
    };

    for (const auto& [tid, _] : task_ids) {
        if (visited[tid] == 0) {
            std::vector<std::string> path = { tid };
            dfs(dfs, tid, path);
        }
    }

    return errors;
}

std::vector<std::string> TaskLinter::check_staged_resolutions(const fs::path& repo_root, const std::vector<TaskRecord>& active_tasks) {
    std::vector<std::string> errors;
    std::string diff = exec_command("git diff --cached -U0", repo_root);
    if (diff.empty()) return errors;

    std::vector<std::string> raw_deleted_todos;
    std::regex deleted_task_re(R"((?:TODO|FIXME|SPEC)\(([a-zA-Z0-9_\-]+)\s*\|)");
    auto lines = split_lines(diff);
    for (const auto& line : lines) {
        if (starts_with(line, "-") && !starts_with(line, "---")) {
            std::smatch m;
            if (std::regex_search(line, m, deleted_task_re)) {
                raw_deleted_todos.push_back(m[1].str());
            }
        }
    }

    std::unordered_set<std::string> active_ids;
    for (const auto& t : active_tasks) active_ids.insert(t.id);

    std::vector<std::string> deleted_todos;
    for (const auto& tid : raw_deleted_todos) {
        if (!active_ids.count(tid)) {
            deleted_todos.push_back(tid);
        }
    }

    if (!deleted_todos.empty()) {
        std::string staged_files_raw = exec_command("git diff --cached --name-only", repo_root);
        auto staged_files = split_lines(staged_files_raw);
        std::vector<fs::path> staged_evidence;
        for (const auto& sf : staged_files) {
            if (starts_with(sf, "logs/") || starts_with(sf, "tests/")) {
                staged_evidence.push_back(repo_root / sf);
            }
        }

        for (const auto& tid : deleted_todos) {
            if (staged_evidence.empty()) {
                errors.push_back(
                    "Anti-Erasure Violation: Task '" + tid + "' is being removed, but no test or log artifact in logs/ " +
                    "is staged to prove resolution. Revert the deletion or stage the proof log."
                );
                continue;
            }

            bool found = false;
            for (const auto& ef : staged_evidence) {
                if (fs::exists(ef) && fs::file_size(ef) > 0) {
                    std::string ef_content = read_file(ef);
                    if (ef_content.find(tid) != std::string::npos) {
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                errors.push_back(
                    "Anti-Erasure Violation: Task '" + tid + "' is being removed, but none of the staged log/test files " +
                    "mention task ID '" + tid + "'. The resolution log must explicitly reference '" + tid + "' and contain non-empty data."
                );
            }
        }
    }

    return errors;
}

LintResult TaskLinter::lint_repository(const fs::path& repo_root) {
    LintResult res;
    res.root = repo_root;

    for (const auto& entry : fs::recursive_directory_iterator(repo_root, fs::directory_options::skip_permission_denied)) {
        if (entry.is_directory()) {
            std::string dir_name = entry.path().filename().string();
            if (IGNORE_DIRS.count(dir_name)) continue;
        }
        if (!entry.is_regular_file()) continue;

        bool skip = false;
        for (auto it = entry.path().begin(); it != entry.path().end(); ++it) {
            if (IGNORE_DIRS.count(it->string())) {
                skip = true;
                break;
            }
        }
        if (skip) continue;

        std::string ext = entry.path().extension().string();
        if (SCAN_EXTENSIONS.count(ext)) {
            scan_file(entry.path(), repo_root, res.tasks, res.errors);
        }
    }

    auto dag_errors = validate_dag(res.tasks);
    res.errors.insert(res.errors.end(), dag_errors.begin(), dag_errors.end());

    auto resolution_errors = check_staged_resolutions(repo_root, res.tasks);
    res.errors.insert(res.errors.end(), resolution_errors.begin(), resolution_errors.end());

    // Validate in-tree LexLean specification contracts via Rust/C FFI bridge
    auto frags = SpecStitcher::scan_repository(repo_root);
    if (!frags.empty()) {
        auto stitched = SpecStitcher::stitch(frags, "Syntra.Spec");
        if (!stitched.success) {
            res.errors.push_back("Specification stitch error: " + stitched.error);
        } else {
            auto ffi_res = SpecStitcher::verify_ffi_contracts(stitched.stitched_semantic_json, "Syntra.Spec");
            if (!ffi_res.success) {
                res.errors.push_back("FFI Contract Verification Violation: " + ffi_res.error);
            } else {
                res.ffi_defs_verified = ffi_res.defs_checked;
                res.ffi_theorems_verified = ffi_res.theorems_checked;
            }
        }
    }

    return res;
}

void TaskLinter::print_report(const LintResult& res) {
    std::cout << "======================================================================\n";
    std::cout << "SOURCE-FIRST TASK & INVARIANT LINTER (syntra-nvim / C++ native)\n";
    std::cout << "======================================================================\n\n";

    if (!res.errors.empty()) {
        std::cout << "[FAILED] Found " << res.errors.size() << " task specification errors:\n\n";
        for (const auto& err : res.errors) {
            std::cout << "  ✖ " << err << "\n";
        }
        std::cout << "\nPlease fix these in code comments before committing.\n";
        return;
    }

    std::cout << "[PASS] Verified " << res.tasks.size() << " in-line task contracts across codebase.\n";
    std::cout << "[PASS] FFI Contract Verifier (prod_codegen C ABI): "
              << res.ffi_defs_verified << " definitions, "
              << res.ffi_theorems_verified << " theorems verified.\n\n";
    std::cout << "Live In-Tree Task DAG:\n";


    for (const auto& tier : {"A", "B", "C"}) {
        std::vector<const TaskRecord*> tier_tasks;
        for (const auto& t : res.tasks) {
            if (t.tier == tier) tier_tasks.push_back(&t);
        }

        if (!tier_tasks.empty()) {
            std::cout << "\n--- Tier " << tier << " ---\n";
            for (const auto* t : tier_tasks) {
                std::string deps_str;
                if (!t->depends_on.empty()) {
                    deps_str = " (depends on: ";
                    for (size_t i = 0; i < t->depends_on.size(); ++i) {
                        if (i > 0) deps_str += ", ";
                        deps_str += t->depends_on[i];
                    }
                    deps_str += ")";
                }

                std::string blocks_str;
                if (!t->blocks.empty()) {
                    blocks_str = " -> blocks [";
                    for (size_t i = 0; i < t->blocks.size(); ++i) {
                        if (i > 0) blocks_str += ", ";
                        blocks_str += t->blocks[i];
                    }
                    blocks_str += "]";
                }

                std::cout << "  ● " << t->id << " (" << t->file.string() << ":" << t->line << ")"
                          << deps_str << blocks_str << "\n";
                std::cout << "    Desc: " << t->description << "\n";
                std::cout << "    Crit: " << t->criterion << "\n";
            }
        }
    }

    std::cout << "\n======================================================================\n";
}

std::string TaskLinter::to_json(const LintResult& res) {
    std::ostringstream ss;
    ss << "{\"root\":\"" << escape_json(res.root.string()) << "\",";
    ss << "\"errors\":[";
    for (size_t i = 0; i < res.errors.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << escape_json(res.errors[i]) << "\"";
    }
    ss << "],\"tasks\":[";
    for (size_t i = 0; i < res.tasks.size(); ++i) {
        if (i > 0) ss << ",";
        const auto& t = res.tasks[i];
        ss << "{\"id\":\"" << escape_json(t.id) << "\","
           << "\"kind\":\"" << escape_json(t.kind) << "\","
           << "\"tier\":\"" << escape_json(t.tier) << "\","
           << "\"file\":\"" << escape_json(t.file.string()) << "\","
           << "\"line\":" << t.line << ","
           << "\"desc\":\"" << escape_json(t.description) << "\","
           << "\"criterion\":\"" << escape_json(t.criterion) << "\","
           << "\"blocks\":[";
        for (size_t b = 0; b < t.blocks.size(); ++b) {
            if (b > 0) ss << ",";
            ss << "\"" << escape_json(t.blocks[b]) << "\"";
        }
        ss << "],\"depends_on\":[";
        for (size_t d = 0; d < t.depends_on.size(); ++d) {
            if (d > 0) ss << ",";
            ss << "\"" << escape_json(t.depends_on[d]) << "\"";
        }
        ss << "]}";
    }
    ss << "]}";
    return ss.str();
}

int TaskLinter::fix_repository(const fs::path& repo_root, bool apply) {
    auto res = lint_repository(repo_root);
    std::unordered_set<std::string> used_ids;
    for (const auto& t : res.tasks) used_ids.insert(t.id);

    std::regex task_header_re(
        R"(^[ \t]*(?:#|//|--)\s*(TODO|FIXME|SPEC)\(([a-zA-Z0-9_\-]+)\s*\|\s*tier:([ABC])((?:\s*\|\s*(?:blocks|depends_on):\[[a-zA-Z0-9_\-,\s]*\])*)\):(?:\s*(.*))?$)"
    );
    std::regex lazy_todo_re(
        R"(^([ \t]*(?:#|//|--))\s*(TODO|FIXME|SPEC)\s*:?\s*(.*)$)",
        std::regex_constants::icase
    );

    int fixed_count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(repo_root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (!SCAN_EXTENSIONS.count(ext)) continue;
        bool ignored = false;
        for (const auto& part : entry.path()) {
            if (IGNORE_DIRS.count(part.string())) { ignored = true; break; }
        }
        if (ignored) continue;

        std::string content = read_file(entry.path());
        auto lines = split_lines(content);
        bool modified = false;
        std::vector<std::string> new_lines;
        std::string stem = entry.path().stem().string();
        std::string rel_path = fs::relative(entry.path(), repo_root).string();

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            std::smatch m;
            if (!std::regex_match(line, task_header_re) && std::regex_match(line, m, lazy_todo_re)) {
                std::string prefix = m[1].str();
                std::string kind = m[2].str();
                for (char& c : kind) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                std::string raw_desc = trim(m[3].str());
                if (raw_desc.empty()) raw_desc = "Resolve " + kind + " in " + stem;

                // Synthesize deterministic slug ID
                std::string slug;
                int words = 0;
                bool in_w = false;
                for (char c : raw_desc) {
                    if (std::isalnum(static_cast<unsigned char>(c))) {
                        slug += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        in_w = true;
                    } else if (in_w) {
                        words++;
                        if (words >= 3) break;
                        slug += '-';
                        in_w = false;
                    }
                }
                while (!slug.empty() && slug.back() == '-') slug.pop_back();
                if (slug.empty()) slug = "task";
                std::string base_id = stem + "-" + slug;
                for (char& c : base_id) if (c == '_') c = '-';
                std::string task_id = base_id;
                int suffix = 2;
                while (used_ids.count(task_id)) {
                    task_id = base_id + "-" + std::to_string(suffix++);
                }
                used_ids.insert(task_id);

                std::string h1 = prefix + " " + kind + "(" + task_id + " | tier:B): " + raw_desc;
                std::string h2 = prefix + " Description: " + raw_desc;
                std::string h3 = prefix + " Criterion: review: \"" + raw_desc + " is implemented and verified\"";

                std::cout << "[SYNTHESIZE] " << rel_path << ":" << (i + 1) << " -> " << task_id << "\n"
                          << "  " << h1 << "\n  " << h2 << "\n  " << h3 << "\n";

                new_lines.push_back(h1);
                new_lines.push_back(h2);
                new_lines.push_back(h3);
                modified = true;
                fixed_count++;
            } else {
                new_lines.push_back(line);
            }
        }

        if (modified && apply) {
            std::ostringstream out;
            for (const auto& l : new_lines) out << l << "\n";
            write_file(entry.path(), out.str());
        }
    }

    if (fixed_count == 0) {
        std::cout << "[PASS] No unstructured TODO/FIXME comments found.\n";
    } else if (apply) {
        std::cout << "[APPLIED] Converted " << fixed_count << " unstructured comment(s) into Task Graph contracts.\n";
    } else {
        std::cout << "[DRY-RUN] Synthesized " << fixed_count << " contract(s). Re-run with --apply to write changes.\n";
    }
    return fixed_count;
}

} // namespace lgcc
