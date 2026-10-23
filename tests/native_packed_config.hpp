// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

namespace tests {
// Pack the generated fixture's data/interface trees and park its loose inputs.
// Call only inside an owned synthetic deployment, before a game session starts.
void pack_native_session_configuration();
// Replace one hint reference inside an owned generated archive before Run.
void write_native_packed_missing_hint();
}
