#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mount a host directory into a specific subpath inside the target game's sandbox
// (e.g. sandbox_rel_dir="plugins" or "data/orbx"). probe_filename may be NULL.
int mount_sandbox_subpath(const char *title_id, const char *host_source_dir,
                          const char *sandbox_rel_dir, const char *probe_filename,
                          char *out_sandbox_path, size_t max_len);

// Mount a host directory (e.g. /data/modHEN/plugins) into the target game's sandbox (/plugins)
// using FreeBSD nullfs loopback mounting (nmount).
int mount_sandbox_nullfs(const char *title_id, const char *host_source_dir, char *out_sandbox_path, size_t max_len);

// Unmount a previously mounted sandbox path
int unmount_sandbox_nullfs(const char *title_id);

#ifdef __cplusplus
}
#endif

