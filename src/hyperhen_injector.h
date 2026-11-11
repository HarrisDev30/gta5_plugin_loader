#pragma once
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int inject_plugin_hyperhen(pid_t pid, const char *plugin_path, uint32_t *out_handle);

#ifdef __cplusplus
}
#endif

