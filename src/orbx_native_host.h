#pragma once

#include <stdint.h>
#include <sys/types.h>
#include "orbx_native_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Install ModHEN Orbx Native hook engine into GTA V PID and run read-only v1.00 RAGE diagnostics.
// Returns 0 on success, -1 on failure.
int install_orbx_native(pid_t pid);

// Poll live telemetry counters and ring-buffer entries via kernel_proc_copyout.
void poll_orbx_native_telemetry(pid_t pid);

// Reset tracked state when game exits.
void reset_orbx_native_state(void);

// Snapshot stats for Port 8050 Web Dashboard (/api/status).
void get_orbx_native_stats(uint32_t *out_mod_count, uint32_t *out_calls, uint32_t *out_redirects, int *out_active);

// Live Mod Menu state & control API for Port 8050 Web Dashboard (/api/menu).
int get_orbx_menu_state(OrbxMenuState *out_menu);
int set_orbx_menu_toggle(const char *name, uint32_t val);
int trigger_orbx_menu_action(uint32_t action, int32_t arg0, uint32_t spawn_hash);
int spawn_orbx_vehicle_by_name(const char *model_name, int save_to_list);
int reload_orbx_addon_cars(void);

// GNM Graphics Pipeline & Crash Telemetry Probes (Phase 0 Read-Only)
int get_orbx_gnm_probe(GnmCrashProbe *out_probe, uint64_t *out_flips);

// Stage 1 Real-Time PSSL .sb Post-Processing Shader Loader & Probe
int get_orbx_shader_probe(GnmShaderProbe *out_probe);
int set_orbx_shader_enabled(uint32_t enabled);
int set_orbx_shader_tunables(float bloom_thresh, float bloom_int, float exposure, float contrast, float sat, float vig, float aa, float sharp);
int reload_orbx_shaders(void);

/* Stage 2 Depth Buffer & Screen-Space SSAO/SSR Host Interface */
int get_orbx_depth_probe(GnmDepthProbe *out_probe);
int set_orbx_depth_tunables(float ssao_inten, float ssao_cp, float ssr_mult, float ssr_dist);

#ifdef __cplusplus
}
#endif

