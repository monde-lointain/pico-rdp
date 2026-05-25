/* A.2: Vulkan-free declaration of the side-by-side replayer driver factory.
 *
 * Upstream declared create_side_by_side_driver() in replayer_driver.hpp next to
 * the Vulkan-coupled parallel/angrylion factories. To keep replayer_driver.hpp
 * Vulkan-free, the (already Vulkan-free) side-by-side combinator declaration was
 * moved here. Its implementation is vendored verbatim in side_by_side.cpp.
 *
 * NOT orthodox-enforced (vendored host-only test infrastructure).
 */

#pragma once

#include <memory>
#include "replayer_driver.hpp"

namespace RDP
{
// Fans every command out to `first` then `second`, toggling the event
// interface's context index (0 / 1) around each so the harness can compare the
// two drivers' RDRAM / scanout. Used as ReplayerState::combined.
std::unique_ptr<ReplayerDriver> create_side_by_side_driver(ReplayerDriver *first, ReplayerDriver *second,
                                                           ReplayerEventInterface &iface);
}
