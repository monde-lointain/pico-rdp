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

OursReplayer::OursReplayer(CommandInterface &player_, ReplayerEventInterface &iface_)
	: player(player_), iface(iface_)
{
	rdram.resize(player.get_rdram_size());
	for (unsigned i = 0; i < VI_NUM_REG; i++)
		p_vi_regs[i] = &vi_regs[i];
	for (unsigned i = 0; i < DP_NUM_REG; i++)
		p_dp_regs[i] = &dp_regs[i];

	struct n64video_config config = {};
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
	// M0 stub: rdpx_video_process_list() is a no-op. Stream B fills this in.
	rdpx_video_process_list();
	iface.notify_command(command_id, num_words, words);
}

void OursReplayer::end_frame()
{
	rdpx_video_update_screen(nullptr);
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
}
