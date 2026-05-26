/* Stream A.0 — ReplayerDriver adapter for OUR renderer (rdp_core).
 *
 * Near-mirror of tests/conformance/oracle/replayer_driver_angrylion_oracle.cpp,
 * but it drives our rdpx_* C ABI (n64video.h) instead of Angrylion's original C
 * names. Our renderer keeps RDRAM caller-owned (config.gfx.rdram -> this driver's
 * `rdram` vector); hidden RDRAM and TMEM are file-static inside rdp_core and
 * reached through the RDPX_TESTING accessor block.
 *
 * Host-only test infrastructure (NOT orthodox-enforced).
 */

#include "replayer_driver_ours.hpp"

#include "logging.hpp"
#include "rdp_common.hpp"

#include <stdint.h>
#include <string.h>
#include <vector>

extern "C" {
#include "n64video.h"

// rdpx_* C ABI from src/rdp/n64video.c (entry points declared in n64video.h).
// The RDPX_TESTING accessors are NOT in the public header (test-only), so we
// declare them here. They are compiled into rdp_core when BUILD_TESTS is on.
uint8_t *rdpx_get_hidden_rdram(void);
uint32_t rdpx_get_hidden_rdram_size(void);
uint8_t *rdpx_get_tmem(void);
uint32_t rdpx_get_tmem_size(void);

// Real per-command dispatch entry (not in the public header — it is the harness
// hook). Mirrors the oracle adapter's rdp_cmd(0, words).
void rdpx_rdp_cmd(uint32_t wid, const uint32_t *args);

// Scanout callback installer. The VI emits the finished frame (RGBA8888) through
// this callback, mirroring the oracle's vdac_write -> update_screen path.
void rdpx_vdac_set_scanout_cb(void (*cb)(const void *, uint32_t, uint32_t, uint32_t));
}

namespace RDP
{
// Our renderer's `config` is a file-static singleton (like the oracle's), so the
// driver is a singleton too.
class OursReplayer : public ReplayerDriver
{
public:
	OursReplayer(CommandInterface &player_, ReplayerEventInterface &iface_);
	~OursReplayer() override;

	uint8_t *get_rdram() override { return rdram.data(); }
	size_t get_rdram_size() override { return rdram.size(); }
	uint8_t *get_hidden_rdram() override { return rdpx_get_hidden_rdram(); }
	size_t get_hidden_rdram_size() override { return rdpx_get_hidden_rdram_size(); }
	uint8_t *get_tmem() override { return rdpx_get_tmem(); }

	void idle() override {}
	void flush_caches() override {}
	void invalidate_caches() override {}

	// Forwards a VI-produced frame to the event interface (RGBA8888), mirroring
	// the oracle's AngrylionReplayer::update_screen. Called via the scanout
	// callback installed on rdp_core.
	void update_screen(const void *data, unsigned width, unsigned height, unsigned row_length);

private:
	CommandInterface &player;
	ReplayerEventInterface &iface;
	std::vector<uint8_t> rdram;

	uint32_t vi_regs[VI_NUM_REG] = {};
	uint32_t dp_regs[DP_NUM_REG] = {};
	uint32_t irq_reg = 0;
	uint32_t *p_vi_regs[VI_NUM_REG] = {};
	uint32_t *p_dp_regs[DP_NUM_REG] = {};

	void eof() override;
	void signal_complete() override;
	void update_rdram(const void *data, size_t size, size_t offset) override;
	void update_hidden_rdram(const void *data, size_t size, size_t offset) override;
	void command(Op command_id, uint32_t word_count, const uint32_t *words) override;
	void end_frame() override;
	void set_vi_register(VIRegister index, uint32_t value) override;

	void begin_vi_register_per_scanline() override {}
	void set_vi_register_for_scanline(unsigned vi_line, uint32_t h_start, uint32_t x_scale) override;
	void end_vi_register_per_scanline() override {}
	void set_crop_rect(unsigned left, unsigned right, unsigned top, unsigned bottom) override;
};

static OursReplayer *global_replayer;

static void mi_intr_noop(void) {}

// Free-function scanout callback installed on rdp_core. The VI calls it with the
// finished frame; we route to the singleton's event interface, exactly like the
// oracle's free-function vdac_write -> global_replayer->update_screen.
static void ours_scanout_cb(const void *data, uint32_t width, uint32_t height, uint32_t pitch)
{
	if (global_replayer)
		global_replayer->update_screen(data, width, height, pitch);
}

OursReplayer::OursReplayer(CommandInterface &player_, ReplayerEventInterface &iface_)
	: player(player_), iface(iface_)
{
	rdram.resize(player.get_rdram_size());
	for (unsigned i = 0; i < VI_NUM_REG; i++)
		p_vi_regs[i] = &vi_regs[i];
	for (unsigned i = 0; i < DP_NUM_REG; i++)
		p_dp_regs[i] = &dp_regs[i];

	struct N64videoConfig config = {};
	config.gfx.rdram = rdram.data();
	config.gfx.rdram_size = uint32_t(rdram.size());
	config.gfx.vi_reg = p_vi_regs;
	config.gfx.dp_reg = p_dp_regs;
	config.gfx.mi_intr_reg = &irq_reg;
	config.gfx.mi_intr_cb = mi_intr_noop;
	config.vi.mode = VI_MODE_NORMAL;
	config.vi.interp = VI_INTERP_LINEAR;
	config.dp.compat = DP_COMPAT_HIGH;
	rdpx_video_init(&config);

	// Route VI scanout to this driver's event interface (RGBA8888), as the
	// oracle wires vdac_write -> update_screen.
	rdpx_vdac_set_scanout_cb(ours_scanout_cb);
}

OursReplayer::~OursReplayer()
{
	rdpx_video_close();
	if (global_replayer == this)
		global_replayer = nullptr;
}

void OursReplayer::update_rdram(const void *data, size_t size, size_t offset)
{
	memcpy(rdram.data() + offset, data, size);
}

void OursReplayer::update_hidden_rdram(const void *data, size_t size, size_t offset)
{
	memcpy(rdpx_get_hidden_rdram() + offset, data, size);
}

void OursReplayer::command(Op command_id, uint32_t num_words, const uint32_t *words)
{
	// Feed the RDP command words straight into our real dispatch, exactly like
	// the oracle adapter's rdp_cmd(0, words). The harness has already assembled
	// a complete command; we run it on worker 0 (single host worker).
	rdpx_rdp_cmd(0, words);
	iface.notify_command(command_id, num_words, words);
}

void OursReplayer::end_frame()
{
	// Runs the VI; on a valid frame the scanout callback fires and forwards the
	// frame to update_screen (-> iface.update_screen). The struct argument is
	// unused (the harness reads scanout via the event interface).
	rdpx_video_update_screen(nullptr);
}

void OursReplayer::update_screen(const void *data, unsigned width, unsigned height, unsigned row_length)
{
	iface.update_screen(data, width, height, row_length);
}

void OursReplayer::set_vi_register(VIRegister index, uint32_t value)
{
	vi_regs[unsigned(index)] = value;
}

void OursReplayer::set_vi_register_for_scanline(unsigned, uint32_t, uint32_t) {}

void OursReplayer::set_crop_rect(unsigned, unsigned, unsigned, unsigned) {}

void OursReplayer::eof()
{
	iface.eof();
}

void OursReplayer::signal_complete()
{
	iface.signal_complete();
}

std::unique_ptr<ReplayerDriver> create_replayer_driver_ours(CommandInterface &player,
                                                            ReplayerEventInterface &iface)
{
	if (global_replayer)
	{
		LOGE("Our renderer is a singleton (file-static config).\n");
		return nullptr;
	}

	auto ret = std::make_unique<OursReplayer>(player, iface);
	global_replayer = ret.get();
	return ret;
}

// -------------------------------------------------------------------------
// ReplayerState::gpu_scaled stub.
//
// rdp_core is native-resolution ONLY (Angrylion-derived); it has no upscaling
// renderer. Upstream parallel-rdp wires gpu_scaled to a SECOND instance of the
// Vulkan GPU renderer (upscale=true). We have no such thing, and our engine is a
// file-static singleton — a second real instance would corrupt the first's
// shared VI/RDP state.
//
// But the vendored VI host (vi_conformance_host.cpp) dereferences
// state.gpu_scaled unconditionally in set_default_vi_registers() for EVERY VI
// suite, even though its scanout is never compared for the native aa-* suites
// (those compare combined = reference vs gpu). So gpu_scaled must be a valid,
// non-null object that simply absorbs set_vi_register / end_frame without
// touching the real engine.
//
// This stub does exactly that: it owns its own RDRAM/hidden-RDRAM/TMEM buffers
// (so randomize_rdram and the accessors are safe) and no-ops every driver call.
// The per-scanline-*upscale* suites (the only ones that would compare its
// scanout) are intentionally NOT registered — they are a parallel-rdp GPU-VI
// feature with no native-renderer equivalent (see vi_suite_runner_main.cpp).
class OursScaledStub : public ReplayerDriver
{
public:
	OursScaledStub(CommandInterface &player, ReplayerEventInterface &iface_) : iface(iface_)
	{
		rdram.resize(player.get_rdram_size());
		hidden_rdram.resize(player.get_rdram_size() / 2);
		tmem.resize(0x1000);
	}

	uint8_t *get_rdram() override { return rdram.data(); }
	size_t get_rdram_size() override { return rdram.size(); }
	uint8_t *get_hidden_rdram() override { return hidden_rdram.data(); }
	size_t get_hidden_rdram_size() override { return hidden_rdram.size(); }
	uint8_t *get_tmem() override { return tmem.data(); }

	void idle() override {}
	void flush_caches() override {}
	void invalidate_caches() override {}

private:
	ReplayerEventInterface &iface;
	std::vector<uint8_t> rdram;
	std::vector<uint8_t> hidden_rdram;
	std::vector<uint8_t> tmem;

	void eof() override { iface.eof(); }
	void signal_complete() override { iface.signal_complete(); }
	void update_rdram(const void *, size_t, size_t) override {}
	void update_hidden_rdram(const void *, size_t, size_t) override {}
	void command(Op, uint32_t, const uint32_t *) override {}
	void end_frame() override {}
	void set_vi_register(VIRegister, uint32_t) override {}
	void begin_vi_register_per_scanline() override {}
	void set_vi_register_for_scanline(unsigned, uint32_t, uint32_t) override {}
	void end_vi_register_per_scanline() override {}
	void set_crop_rect(unsigned, unsigned, unsigned, unsigned) override {}
};

std::unique_ptr<ReplayerDriver> create_replayer_driver_ours_scaled(CommandInterface &player,
                                                                   ReplayerEventInterface &iface)
{
	return std::make_unique<OursScaledStub>(player, iface);
}
}
