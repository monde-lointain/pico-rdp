/* rdp_version.h — minimal build-glue header for the renderer core library.
 *
 * Stream A.1 glue only. The real public ABI (n64video.h / n64video_common.h)
 * is added by Stream A.5. Kept as a pure C ABI (rdpx_* prefix) so it links
 * identically on host and device and never collides with the Angrylion oracle.
 */
#ifndef RDPX_VERSION_H
#define RDPX_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the renderer-core version string (e.g. "rdp 0.1.0"). */
const char *rdpx_version(void);

#ifdef __cplusplus
}
#endif

#endif /* RDPX_VERSION_H */
