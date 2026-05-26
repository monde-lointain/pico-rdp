/* capture.cc — Stream C.5: write the viewer's live RDP command stream to a
 * `.rdp` file byte-compatible with parallel-rdp's DumpPlayer reader
 * (tests/conformance/vendor/rdp_dump.cpp). See capture.h for the layout.
 *
 * Orthodoxy-EXEMPT (host viewer TU; mirrors main.cc / renderer_host).
 *
 * The writer mirrors DumpPlayer::iterate's expected record grammar exactly: a
 * uint32 Command tag, then the payload that tag's reader branch consumes. The
 * tag values come straight from rdp_dump.cpp's `enum class Command`.
 */

#include "capture.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "panels.h"  // struct Playback / PlaybackCmd
#include "renderer_host.h"

// DumpPlayer Command tags (rdp_dump.cpp `enum class Command`). Mirrored here so
// the writer is self-contained; the reader's switch keys off these exact ids.
#define DUMP_CMD_UPDATE_DRAM 1u
#define DUMP_CMD_RDP_COMMAND 2u
#define DUMP_CMD_SET_VI_REGISTER 3u
#define DUMP_CMD_END_FRAME 4u
#define DUMP_CMD_SIGNAL_COMPLETE 5u
#define DUMP_CMD_END_OF_FILE 6u
#define DUMP_CMD_UPDATE_DRAM_FLUSH 7u
#define DUMP_CMD_UPDATE_HIDDEN_DRAM 8u
#define DUMP_CMD_UPDATE_HIDDEN_DRAM_FLUSH 9u

// DumpPlayer::load_dump only accepts these sizes; the header must declare one.
#define DUMP_RDRAM_4MB (4u * 1024u * 1024u)
#define DUMP_RDRAM_8MB (8u * 1024u * 1024u)
#define DUMP_HIDDEN_4MB (4u * 1024u * 1024u)

static int write_word(FILE* f, uint32_t w) {
  // DumpPlayer::read_word does fread(&value, 4, 1) — raw native-endian uint32.
  return fwrite(&w, sizeof(w), 1, f) == 1 ? 0 : 1;
}

int capture_frame_to_rdp(const char* path, const struct Playback* pb) {
  if (!path || !pb) return 1;

  uint32_t rdram_size = 0;
  const uint8_t* rdram = renderer_host_rdram(&rdram_size);
  if (!rdram) {
    fprintf(stderr, "capture: renderer not initialized\n");
    return 2;
  }

  // DumpPlayer requires rdram_size in {4 MiB, 8 MiB}. The renderer arena is
  // smaller (2 MiB); declare the smallest legal dump size that contains it and
  // zero-pad the remainder so the reader's bounds checks pass and the replayed
  // renderer (re-init at this size) sees an identical addressable arena.
  uint32_t dump_rdram_size = DUMP_RDRAM_4MB;
  if (rdram_size > DUMP_RDRAM_4MB) dump_rdram_size = DUMP_RDRAM_8MB;
  if (rdram_size > dump_rdram_size) {
    fprintf(stderr, "capture: RDRAM %u too large for an 8 MiB dump\n",
            rdram_size);
    return 3;
  }

  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "capture: cannot open %s\n", path);
    return 4;
  }

  int rc = 0;

  // Header: 8-byte magic, then rdram_size + hidden_dram_size words.
  if (fwrite("RDPDUMP2", 1, 8, f) != 8) rc = 5;
  if (!rc) rc = write_word(f, dump_rdram_size);
  if (!rc) rc = write_word(f, DUMP_HIDDEN_4MB);

  // SetVIRegister records for every VI register the renderer has programmed, so
  // the replay reconstructs the exact scanout geometry standalone. Index order
  // matches DumpPlayer's VIRegister enum == n64video.h VI_* enum.
  uint32_t vi_count = 0;
  const uint32_t* vi = renderer_host_vi_regs(&vi_count);
  if (vi) {
    for (uint32_t i = 0; i < vi_count && !rc; ++i) {
      rc = write_word(f, DUMP_CMD_SET_VI_REGISTER);
      if (!rc) rc = write_word(f, i);
      if (!rc) rc = write_word(f, vi[i]);
    }
  }

  // Seed the (pre-render) RDRAM as one UpdateDram region covering the whole
  // declared arena, then UpdateDramFlush so the reader pushes it into the
  // replay renderer via update_rdram(). We write the renderer's actual bytes
  // then zero-pad up to dump_rdram_size.
  if (!rc) rc = write_word(f, DUMP_CMD_UPDATE_DRAM);
  if (!rc) rc = write_word(f, 0u);               // offset
  if (!rc) rc = write_word(f, dump_rdram_size);  // size
  if (!rc && fwrite(rdram, 1, rdram_size, f) != rdram_size) rc = 6;
  if (!rc && dump_rdram_size > rdram_size) {
    static const uint8_t zeros[4096] = {0};
    uint32_t pad = dump_rdram_size - rdram_size;
    while (pad && !rc) {
      uint32_t chunk = pad < sizeof(zeros) ? pad : (uint32_t)sizeof(zeros);
      if (fwrite(zeros, 1, chunk, f) != chunk) rc = 6;
      pad -= chunk;
    }
  }
  if (!rc) rc = write_word(f, DUMP_CMD_UPDATE_DRAM_FLUSH);

  // RDPCommand records: one per buffered command. cmd_id is the RDP opcode in
  // the command's first word [29:24] — what DumpPlayer hands back as Op so the
  // replay's command() can dispatch (our replay just feeds the raw words, so
  // cmd_id is informational, but we record the true opcode for fidelity).
  for (uint32_t i = 0; i < pb->cmd_total && !rc; ++i) {
    const struct PlaybackCmd* c = &pb->cmds[i];
    const uint32_t* w = &pb->words[c->word_off];
    uint32_t cmd_id = (c->word_count > 0) ? ((w[0] >> 24) & 0x3fu) : 0u;
    rc = write_word(f, DUMP_CMD_RDP_COMMAND);
    if (!rc) rc = write_word(f, cmd_id);
    if (!rc) rc = write_word(f, c->word_count);
    if (!rc && c->word_count) {
      if (fwrite(w, sizeof(uint32_t), c->word_count, f) != c->word_count)
        rc = 7;
    }
  }

  // EndFrame (run the VI) then EndOfFile.
  if (!rc) rc = write_word(f, DUMP_CMD_END_FRAME);
  if (!rc) rc = write_word(f, DUMP_CMD_END_OF_FILE);

  if (fclose(f) != 0 && !rc) rc = 8;
  if (rc) {
    fprintf(stderr, "capture: write error (%d) to %s\n", rc, path);
    return rc;
  }

  printf("capture: wrote %s (rdram=%u dump=%u cmds=%u)\n", path, rdram_size,
         dump_rdram_size, pb->cmd_total);
  return 0;
}
