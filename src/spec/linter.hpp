#pragma once

#include "../common.hpp"

namespace lgcc {

// TODO(lgcc-native-linter | tier:A | depends_on:[spec-jit-patch-schema] | blocks:[tasks-add-spec-scaffold]): Rewrite scripts/lint_tasks.py into native C++20 inside lgcc.
// Description: Rewrite scripts/lint_tasks.py into native C++20 inside lgcc, implementing high-performance task DAG validation, anti-erasure git checks, lazy-TODO detection, and JSON export.
// Criterion: review: "lgcc --lint-tasks validates repository DAG and anti-erasure constraints in <15ms with full feature parity to Python linter"

struct TaskRecord {
    std::string id;
    std::string kind; // "TODO", "FIXME", "SPEC"
    std::string tier; // "A", "B", "C"
    std::vector<std::string> blocks;
    std::vector<std::string> depends_on;
    std::string description;
    std::string criterion;
    fs::path file;
    int line = 0;
};

struct LintResult {
    std::vector<TaskRecord> tasks;
    std::vector<std::string> errors;
    fs::path root;
};

class TaskLinter {
public:
    static LintResult lint_repository(const fs::path& repo_root);
    static std::string to_json(const LintResult& res);
    static void print_report(const LintResult& res);

private:
    static void scan_file(const fs::path& filepath, const fs::path& repo_root, std::vector<TaskRecord>& tasks, std::vector<std::string>& errors);
    static std::vector<std::string> validate_dag(const std::vector<TaskRecord>& tasks);
    static std::vector<std::string> check_staged_resolutions(const fs::path& repo_root, const std::vector<TaskRecord>& active_tasks);
};

} // namespace lgcc
