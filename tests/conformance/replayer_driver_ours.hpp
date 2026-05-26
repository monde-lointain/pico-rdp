/* Stream A.0 — ReplayerDriver adapter for OUR renderer (rdp_core).
 *
 * Mirrors the Angrylion oracle wrapper (replayer_driver_angrylion_oracle.cpp)
 * but drives our rdpx_* C ABI instead of Angrylion's original C names. Injected
 * into ReplayerState::make_gpu so the side-by-side harness can compare our
 * renderer's RDRAM / hidden-RDRAM / scanout against the oracle.
 *
 * Host-only test infrastructure (NOT orthodox-enforced).
 */

#pragma once

#include <memory>
#include "replayer_driver.hpp"

namespace RDP
{
// Factory: builds a ReplayerDriver bound to our rdp_core renderer. Returns null
// if a singleton instance already exists (our stub keeps file-static state, like
// the oracle). Wired into ReplayerState::make_gpu by the A.0 executable.
std::unique_ptr<ReplayerDriver> create_replayer_driver_ours(CommandInterface &player,
                                                            ReplayerEventInterface &iface);

// Factory for ReplayerState::gpu_scaled. rdp_core is native-resolution only and
// has no upscaler, so this returns a no-op stub (it absorbs register/frame calls
// without touching the singleton engine). The native VI suites never compare its
// scanout; the per-scanline-*upscale* suites (which would) are not registered.
std::unique_ptr<ReplayerDriver> create_replayer_driver_ours_scaled(CommandInterface &player,
                                                                   ReplayerEventInterface &iface);
}
