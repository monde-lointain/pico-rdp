/* rdp_version.cc — build-glue translation unit (Stream A.1).
 *
 * Sole purpose: give the rdp_core static library one compilable source so it
 * links before the Angrylion-derived single TU (n64video.c) is added by Stream
 * A.0/B. Compiled as C++; exposes a C ABI symbol.
 */
#include "rdp_version.h"

#define RDPX_VERSION_STRING "rdp 0.1.0"

extern "C" const char *rdpx_version(void) { return RDPX_VERSION_STRING; }
