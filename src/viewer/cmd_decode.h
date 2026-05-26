#pragma once

// Stream C.2 — RDP command decoder.
//
// Turns raw RDP command words (the SAME 64-bit words the demo emitter produces
// and the renderer consumes via rdpx_rdp_cmd) into (a) a decoded human-readable
// record for the command log and (b), via shadow_state.h, a maintained shadow
// of RDP state for the inspector. One decoder feeds both panels — no rendering,
// SDL or ImGui dependency.
//
// Command IDs are the 6-bit field (word0 >> 24) & 0x3f, matching CMD_ID() in
// src/rdp/n64video.c. Word counts come from the renderer's rdp_commands[]
// length table (bytes/4). Unknown / unhandled opcodes fall back to a raw-hex
// record and a 1-word-pair (2-word) length guess — the decoder NEVER crashes on
// bad input.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 6-bit command IDs (subset relevant to the demo + inspector; values match
// src/rdp/n64video.c CMD_ID_*). Listed here so the decoder/test do not depend
// on rdp internal headers.
enum {
  RDPCMD_NO_OP = 0x00,
  RDPCMD_FILL_TRIANGLE = 0x08,
  RDPCMD_FILL_ZBUFFER_TRIANGLE = 0x09,
  RDPCMD_TEXTURE_TRIANGLE = 0x0a,
  RDPCMD_TEXTURE_ZBUFFER_TRIANGLE = 0x0b,
  RDPCMD_SHADE_TRIANGLE = 0x0c,
  RDPCMD_SHADE_ZBUFFER_TRIANGLE = 0x0d,
  RDPCMD_SHADE_TEXTURE_TRIANGLE = 0x0e,
  RDPCMD_SHADE_TEXTURE_Z_TRIANGLE = 0x0f,
  RDPCMD_TEXTURE_RECTANGLE = 0x24,
  RDPCMD_TEXTURE_RECTANGLE_FLIP = 0x25,
  RDPCMD_SYNC_LOAD = 0x26,
  RDPCMD_SYNC_PIPE = 0x27,
  RDPCMD_SYNC_TILE = 0x28,
  RDPCMD_SYNC_FULL = 0x29,
  RDPCMD_SET_KEY_GB = 0x2a,
  RDPCMD_SET_KEY_R = 0x2b,
  RDPCMD_SET_CONVERT = 0x2c,
  RDPCMD_SET_SCISSOR = 0x2d,
  RDPCMD_SET_PRIM_DEPTH = 0x2e,
  RDPCMD_SET_OTHER_MODES = 0x2f,
  RDPCMD_LOAD_TLUT = 0x30,
  RDPCMD_SET_TILE_SIZE = 0x32,
  RDPCMD_LOAD_BLOCK = 0x33,
  RDPCMD_LOAD_TILE = 0x34,
  RDPCMD_SET_TILE = 0x35,
  RDPCMD_FILL_RECTANGLE = 0x36,
  RDPCMD_SET_FILL_COLOR = 0x37,
  RDPCMD_SET_FOG_COLOR = 0x38,
  RDPCMD_SET_BLEND_COLOR = 0x39,
  RDPCMD_SET_PRIM_COLOR = 0x3a,
  RDPCMD_SET_ENV_COLOR = 0x3b,
  RDPCMD_SET_COMBINE = 0x3c,
  RDPCMD_SET_TEXTURE_IMAGE = 0x3d,
  RDPCMD_SET_MASK_IMAGE = 0x3e,
  RDPCMD_SET_COLOR_IMAGE = 0x3f
};

// A decoded record for one command. The decoder fills `name`, `id`,
// `word_count`, `known`, and a one-line `summary` of the decoded fields. The
// log panel prints `summary`; the inspector reads the shadow (shadow_state.h)
// instead.
//
// `summary` is a fixed buffer (no heap, Orthodox); long fields are formatted to
// fit. `known` is 0 for unhandled/invalid opcodes (raw-hex fallback in
// summary).
struct CmdRecord {
  uint8_t id;           // 6-bit opcode
  uint8_t known;        // 1 if a named handler exists (else raw-hex fallback)
  uint8_t is_triangle;  // 1 for the eight 0x08..0x0f triangle opcodes
  uint32_t word_count;  // words consumed (>=2); length-table driven
  const char *name;     // static string, e.g. "SET_COLOR_IMAGE"
  char summary[160];    // human-readable decoded fields
};

// Number of 32-bit words a command of opcode `id` consumes (from the renderer's
// rdp_commands[] length table, bytes/4). Unknown opcodes return 2 (one word
// pair) so a malformed stream still advances. Always >= 2.
uint32_t rdp_cmd_word_count(uint8_t id);

// Static command name for `id` ("UNKNOWN" if none). Never null.
const char *rdp_cmd_name(uint8_t id);

// Decode the command at `words` (which has at least `avail` words available)
// into `out`. Reads at most rdp_cmd_word_count(id) words but never more than
// `avail` (a truncated tail is reported in the summary rather than over-read).
// Returns the word count the decoder *would* consume (>= 2), so a caller can
// iterate a packed stream: advance by min(returned, remaining). Never crashes.
uint32_t cmd_decode(const uint32_t *words, uint32_t avail,
                    struct CmdRecord *out);

#ifdef __cplusplus
}  // extern "C"
#endif
