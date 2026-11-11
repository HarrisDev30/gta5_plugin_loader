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

#ifdef __cplusplus
}
#endif

