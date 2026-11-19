#pragma once

#include <string>

#include "catalog.hpp"
#include "engine.hpp"
#include "verify.hpp"

namespace app {

// State of the Generate panel. Behavior is unchanged from the single-window app.
struct GenerateState {
    int selected = 0;
    char name[64] = "Demo";
    std::string output;
    std::string status;
};

// Pattern picker, class-name field, and the verified output.
void draw_generate_panel(GenerateState& state, const wb::Catalog& catalog, wb::Verifier& verifier);

}  // namespace app
