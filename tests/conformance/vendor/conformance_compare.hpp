/* Standalone byte-compare helpers for the parallel-rdp conformance harness.
 *
 * Extracted verbatim (A.2) from upstream conformance_utils.hpp, which coupled
 * these pure comparison routines to Vulkan (context.hpp / device.hpp) and
 * Granite (global_managers, thread_group, os_filesystem, path_utils). None of
 * the compare logic needs any of that, so it lives here free of Vulkan/Granite
 * and compiles on its own (only <vector>, <string>, logging shim, C stdlib).
 *
 * The comparison bodies -- compare_memory / compare_rdram / compare_image /
 * crop_image / randomize_rdram / clear_rdram / suite_compare(_glob) -- are
 * UNCHANGED from upstream so RDRAM / scanout compares stay bit-exact.
 *
 * The one structural change: upstream's compare_image()/crop_image() operate on
 * std::vector<Interface::RGBA>, where Interface lived in the Vulkan-coupled
 * conformance_utils.hpp. That RGBA POD is hoisted here as ScanoutRGBA (same
 * layout: 4x uint8_t) so this header is self-contained; the event sink in
 * replayer_state.hpp aliases Interface::RGBA = ScanoutRGBA.
 *
 * NOT orthodox-enforced (vendored host-only test infrastructure).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <vector>
#include <utility>

#include "logging.hpp"
#include "replayer_driver.hpp"

namespace RDP
{
struct ScanoutRGBA
{
	uint8_t r, g, b, a;
};

static inline bool compare_memory(const char *tag, const uint8_t *reference_, const uint8_t *gpu_, size_t size,
                                  uint32_t *fault_addr)
{
	if (!memcmp(reference_, gpu_, size))
	{
		auto *reference = reinterpret_cast<const uint32_t *>(reference_);
		size /= sizeof(uint32_t);

		bool nonzero = false;
		for (size_t i = 0; i < size; i++)
		{
			if (reference[i] != 0)
			{
				nonzero = true;
				break;
			}
		}
		if (!nonzero)
			LOGW("RDRAM is completely zero, might not be a valuable test.\n");
		return true;
	}
	else
	{
		auto *reference = reference_;
		auto *reference16 = reinterpret_cast<const uint16_t *>(reference_);
		auto *reference32 = reinterpret_cast<const uint32_t *>(reference_);
		auto *gpu = gpu_;
		auto *gpu16 = reinterpret_cast<const uint16_t *>(gpu_);
		auto *gpu32 = reinterpret_cast<const uint32_t *>(gpu_);

		bool nonzero = false;
		for (size_t i = 0; i < size; i++)
		{
			if (reference[i ^ 3] != gpu[i ^ 3])
			{
				LOGE("  8-bit coord: (%d, %d)\n", int(i % 320), int(i / 320));
				LOGE("Memory delta found at byte %zu for %s, (ref) 0x%02x != (gpu) 0x%02x!\n", i, tag, reference[i ^ 3],
				     gpu[i ^ 3]);

				LOGE("  16-bit coord: (%d, %d)\n", int((i >> 1) % 320), int((i >> 1) / 320));
				LOGE("Memory delta found at word %zu for %s, (ref) 0x%02x != (gpu) 0x%02x!\n", i >> 1, tag, reference16[(i >> 1) ^ 1],
				     gpu16[(i >> 1) ^ 1]);

				LOGE("  32-bit coord: (%d, %d)\n", int((i >> 2) % 320), int((i >> 2) / 320));
				LOGE("Memory delta found at dword %zu for %s, (ref) 0x%02x != (gpu) 0x%02x!\n", i >> 2, tag, reference32[i >> 2],
				     gpu32[i >> 2]);

				if (fault_addr)
					*fault_addr = i;
				return false;
			}

			if (reference[i ^ 3] != 0)
				nonzero = true;
		}

		if (!nonzero)
			LOGW("RDRAM is completely zero, might not be a valuable test.\n");
	}

	return true;
}

static inline bool compare_rdram(ReplayerDriver &reference, ReplayerDriver &gpu,
                                 uint32_t *fault_addr = nullptr, bool *fault_hidden = nullptr)
{
	auto *rdram_reference = reference.get_rdram();
	auto *rdram_gpu = gpu.get_rdram();
	if (!compare_memory("RDRAM", rdram_reference, rdram_gpu, gpu.get_rdram_size(), fault_addr))
	{
		if (fault_hidden)
			*fault_hidden = false;
		return false;
	}

	auto *hidden_reference = reference.get_hidden_rdram();
	auto *hidden_gpu = gpu.get_hidden_rdram();
	if (!compare_memory("Hidden RDRAM", hidden_reference, hidden_gpu, gpu.get_hidden_rdram_size(), fault_addr))
	{
		if (fault_hidden)
			*fault_hidden = true;
		return false;
	}

	return true;
}

static inline bool compare_image(const std::vector<ScanoutRGBA> &reference,
                                 unsigned reference_width, unsigned reference_height,
                                 const std::vector<ScanoutRGBA> &gpu, unsigned gpu_width, unsigned gpu_height)
{
	if (reference_width != gpu_width || reference_height != gpu_height)
	{
		LOGE("Reference scanout result resolution does not match GPU. Ref: %u x %u, GPU: %u x %u.\n",
		     reference_width, reference_height,
		     gpu_width, gpu_height);
		return false;
	}

	for (unsigned y = 0; y < reference_height; y++)
	{
		for (unsigned x = 0; x < reference_width; x++)
		{
			auto &a = reference[y * reference_width + x];
			auto &b = gpu[y * reference_width + x];
			if (a.r != b.r || a.g != b.g || a.b != b.b)
			{
				LOGE("Pixel mismatch at %u x %u, [%u, %u, %u] vs [%u, %u, %u]\n",
				     x, y, a.r, a.g, a.b, b.r, b.g, b.b);
				return false;
			}
		}
	}

	return true;
}

static inline void crop_image(std::vector<ScanoutRGBA> &reference,
                              int &width, int &height,
                              int left, int right, int top, int bottom)
{
	int new_width = width - left - right;
	int new_height = height - top - bottom;
	assert(new_width > 0 && new_height > 0);
	std::vector<ScanoutRGBA> new_reference(new_width * new_height);

	for (int y = 0; y < new_height; y++)
	{
		memcpy(new_reference.data() + y * new_width, reference.data() + (y + top) * width + left,
		       new_width * sizeof(ScanoutRGBA));
	}

	reference = std::move(new_reference);
	width = new_width;
	height = new_height;
}

static inline bool suite_compare_glob(const std::string &suite, const std::string &cmp)
{
	if (cmp.empty())
		return true;
	return suite.find(cmp) != std::string::npos;
}

static inline bool suite_compare(const std::string &suite, const std::string &cmp)
{
	return suite == cmp;
}
}
