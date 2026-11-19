#pragma once

// Staged, reviewable fixes. Pure file logic: no ImGui, no display.
//   stage()  copies src/ and test/ to a temp directory, writes the proposal into the copy,
//            and compiles and tests the copy. The project on disk is never written.
//   apply()  writes the proposal to the real file only when the caller confirmed AND the
//            staged build and tests passed AND the file still matches the proposal's base.

#include <string>

#include "fixer.hpp"

namespace app::apply_fix {

// Unified diff (Myers O(ND) algorithm), with 3 lines of context per hunk. Returns an empty
// string when the texts are equal.
//   --- a/<path>
//   +++ b/<path>
//   @@ -<start>,<count> +<start>,<count> @@
std::string unified_diff(const std::string& old_text, const std::string& new_text,
                         const std::string& path);

struct StageResult {
    bool compiled = false;
    bool tested = false;
    std::string compile_output;
    std::string test_output;
    std::string error;  // set when staging could not run at all (bad path, copy failure)

    bool green() const { return compiled && tested && error.empty(); }
};

// Runs the proposal in a copy of the project. `main_class` is the test runner's entry point.
StageResult stage(const std::string& project_dir, const fixer::Proposal& proposal,
                  const std::string& main_class = "demo.TestRunner");

struct ApplyResult {
    bool written = false;
    bool conflict = false;   // the file on disk no longer matches proposal.base_source
    std::string backup_path; // set when a backup was written
    std::string error;
};

// Writes proposal.new_source over the project file. Refuses (and writes nothing) unless
// `confirmed` is true, `staged.green()`, and the file still matches proposal.base_source.
// The original is copied to <project>/out/backups/<timestamp>_<Name>.java first.
ApplyResult apply(const std::string& project_dir, const fixer::Proposal& proposal,
                  const StageResult& staged, bool confirmed);

}  // namespace app::apply_fix
