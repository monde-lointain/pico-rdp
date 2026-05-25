/* Vulkan-free ReplayerState + event sink for the conformance harness (A.2).
 *
 * Re-hosted from upstream conformance_utils.hpp. Upstream's ReplayerState owned
 * a Vulkan::Context / Vulkan::Device, called device->wait_idle() in its dtor,
 * created the angrylion + parallel(GPU) drivers via free functions, and drove
 * device->next_frame_context() / begin_renderdoc_capture() between iterations.
 *
 * This version drops ALL Vulkan:
 *   - No Vulkan::Context / Vulkan::Device, no wait_idle, no renderdoc, no
 *     init_loader / init_instance_and_device, no Granite global managers.
 *   - The concrete drivers (reference = A.3 angrylion oracle, gpu / gpu_scaled =
 *     A.0 our-renderer adapter) are supplied through INJECTED FACTORIES so this
 *     library never names or links those targets. A.0 plugs them in.
 *   - state.device is a NullDevice shim exposing the no-op hooks the suites call
 *     (begin/end_renderdoc_capture, next_frame_context, init_renderdoc_capture).
 *
 * The Interface event sink, RNG-driven randomize_rdram / clear_rdram, and the
 * iteration bookkeeping are UNCHANGED from upstream so test vectors and RDRAM
 * compares stay bit-exact. RNG comes from portable_rng.hpp (deterministic float
 * distribution).
 *
 * NOT orthodox-enforced (vendored host-only test infrastructure).
 */

#pragma once

#include <functional>
#include <memory>
#include <string.h>
#include <vector>

#include "conformance_compare.hpp"
#include "portable_rng.hpp"
#include "rdp_command_builder.hpp"
#include "replayer_driver.hpp"
#include "side_by_side.hpp"

namespace RDP
{
struct Interface : ReplayerEventInterface
{
	// Same byte layout as upstream Interface::RGBA; shared with the standalone
	// compare helpers so compare_image() can be Vulkan-free.
	using RGBA = ScanoutRGBA;

	inline void update_screen(const void *data, unsigned width, unsigned height, unsigned pitch) override;
	inline void notify_command(Op cmd_id, uint32_t num_words, const uint32_t *words) override;
	inline void message(MessageType, const char *) override {}
	inline void eof() override { is_eof = true; }
	inline void set_context_index(unsigned index) override;
	inline void signal_complete() override;

	unsigned draw_calls_for_context[2] = {};
	unsigned frame_count_for_context[2] = {};
	unsigned syncs_for_context[2] = {};
	unsigned current_context = 0;

	std::vector<RGBA> scanout_result[2];
	unsigned widths[2] = {};
	unsigned heights[2] = {};

	bool is_eof = false;

	struct
	{
		uint32_t addr = 0;
		uint32_t size = 0;
		uint32_t width = 0;
		uint32_t depth_addr = 0;
	} fb;
};

inline void Interface::update_screen(const void *data, unsigned width, unsigned height, unsigned pitch)
{
	scanout_result[current_context].resize(width * height);
	for (unsigned y = 0; y < height; y++)
	{
		memcpy(scanout_result[current_context].data() + y * width,
		       static_cast<const uint32_t *>(data) + y * pitch,
		       width * sizeof(RGBA));
	}
	widths[current_context] = width;
	heights[current_context] = height;
	frame_count_for_context[current_context]++;
	draw_calls_for_context[current_context] = 0;
	syncs_for_context[current_context] = 0;
}

inline void Interface::set_context_index(unsigned index)
{
	current_context = index;
}

inline void Interface::notify_command(Op cmd_id, uint32_t, const uint32_t *words)
{
	if (command_is_draw_call(cmd_id))
	{
		draw_calls_for_context[current_context]++;
	}
	else if (cmd_id == Op::SetColorImage)
	{
		fb.size = (words[0] >> 19) & 3;
		fb.addr = words[1] & 0xffffff;
		fb.width = (words[0] & 1023) + 1;
	}
	else if (cmd_id == Op::SetMaskImage)
	{
		fb.depth_addr = words[1] & 0xffffff;
	}
}

inline void Interface::signal_complete()
{
	syncs_for_context[current_context]++;
}

// Vulkan-free stand-in for the bits of Vulkan::Device the suites poke at.
// Every method is a no-op; renderdoc capture is unavailable on the host build.
struct NullDevice
{
	void begin_renderdoc_capture() {}
	void end_renderdoc_capture() {}
	void next_frame_context() {}
	static bool init_renderdoc_capture() { return false; }
};

// Factory injected by A.0: builds a concrete ReplayerDriver (oracle / our
// renderer) bound to the supplied command source + event sink. Returning null
// signals construction failure.
using DriverFactory =
    std::function<std::unique_ptr<ReplayerDriver>(CommandInterface &player, ReplayerEventInterface &iface)>;

struct ReplayerState
{
	// Factories must be set before init(); typically wired by the A.0 executable.
	DriverFactory make_reference;   // e.g. angrylion oracle (A.3)
	DriverFactory make_gpu;         // our renderer adapter (A.0)
	DriverFactory make_gpu_scaled;  // our renderer, upscaled (optional; VI suites)

	NullDevice null_device;
	NullDevice *device = &null_device;

	std::unique_ptr<ReplayerDriver> reference, gpu, gpu_scaled;
	std::unique_ptr<ReplayerDriver> combined;
	CommandBuilder builder;
	Interface iface;

	inline bool init();
};

inline bool ReplayerState::init()
{
	if (!make_reference || !make_gpu)
	{
		LOGE("ReplayerState::init: reference/gpu driver factories not set.\n");
		return false;
	}

	reference = make_reference(builder, iface);
	gpu = make_gpu(builder, iface);
	if (make_gpu_scaled)
		gpu_scaled = make_gpu_scaled(builder, iface);

	if (!reference || !gpu)
	{
		LOGE("ReplayerState::init: driver factory returned null.\n");
		return false;
	}

	combined = create_side_by_side_driver(reference.get(), gpu.get(), iface);
	builder.set_command_interface(combined.get());
	return true;
}

static inline void randomize_rdram(RNG &rng, ReplayerDriver &reference, ReplayerDriver &gpu)
{
	gpu.invalidate_caches();

	auto *rdram_reference = reinterpret_cast<uint32_t *>(reference.get_rdram());
	auto *rdram_gpu = reinterpret_cast<uint32_t *>(gpu.get_rdram());
	size_t size = reference.get_rdram_size() >> 2;

	for (size_t i = 0; i < size; i++)
	{
		auto v = uint32_t(rng.rnd());
		rdram_reference[i] = v;
		rdram_gpu[i] = v;
	}

	rdram_reference = reinterpret_cast<uint32_t *>(reference.get_hidden_rdram());
	rdram_gpu = reinterpret_cast<uint32_t *>(gpu.get_hidden_rdram());
	size = reference.get_hidden_rdram_size() >> 2;

	for (size_t i = 0; i < size; i++)
	{
		auto v = uint32_t(rng.rnd());
		v &= 0x03030303u;
		rdram_reference[i] = v;
		rdram_gpu[i] = v;
	}

	gpu.flush_caches();
}

static inline void clear_rdram(ReplayerDriver &driver)
{
	driver.invalidate_caches();
	memset(driver.get_rdram(), 0, driver.get_rdram_size());
	memset(driver.get_hidden_rdram(), 0, driver.get_hidden_rdram_size());
	driver.flush_caches();
}
}
