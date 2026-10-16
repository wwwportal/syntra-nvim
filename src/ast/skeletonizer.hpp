#pragma once

#include "../common.hpp"
#include "extractor.hpp"

namespace lgcc {

// TODO(ir-contract-skeletonizer | tier:A | depends_on:[ir-ast-extractor] | blocks:[lgcc-dep-bundle]): Skeletonize implementation bodies while preserving signatures and invariants.
// Description: Replace function and method bodies with stubs while retaining complete type annotations, docstring invariants, and interface definitions.
// Criterion: review: "Skeletonizer achieves >70% character and token reduction on target modules while preserving all exported signatures"

struct SkeletonMetrics {
    size_t orig_chars = 0;
    size_t skel_chars = 0;
    int orig_tokens = 0;
    int skel_tokens = 0;
    size_t orig_lines = 0;
    size_t skel_lines = 0;
    double char_reduction = 0.0;
    double token_reduction = 0.0;
};

class Skeletonizer {
public:
    static std::string skeletonize(const fs::path& filepath);
    static SkeletonMetrics compute_metrics(const fs::path& filepath);

private:
    static std::string skeletonize_cpp(const fs::path& filepath, const std::string& content);
    static std::string skeletonize_lua(const fs::path& filepath, const std::string& content);
    static std::string skeletonize_python(const fs::path& filepath, const std::string& content);
};

} // namespace lgcc
