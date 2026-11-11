#define _GNU_SOURCE
#include "nullfs_mount.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/mount.h>

#define MAX_TRACKED_MOUNTS 16
static char g_mountpoints[MAX_TRACKED_MOUNTS][512];
static size_t g_mountpoint_count = 0;

static void record_mountpoint(const char *mp) {
    if (!mp || !*mp) return;
    for (size_t i = 0; i < g_mountpoint_count; i++) {
        if (strcmp(g_mountpoints[i], mp) == 0) return;
    }
    if (g_mountpoint_count < MAX_TRACKED_MOUNTS) {
        snprintf(g_mountpoints[g_mountpoint_count++], sizeof(g_mountpoints[0]), "%s", mp);
    }
}

static void mkdir_recursive(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

// Find the active sandbox directory for a given Title ID (e.g. /mnt/sandbox/CUSA00419_003)
static int find_sandbox_directory(const char *title_id, char *out_sandbox, size_t max_len) {
    DIR *dir = opendir("/mnt/sandbox");
    if (!dir) {
        snprintf(out_sandbox, max_len, "/mnt/sandbox/%s_000", title_id ? title_id : "CUSA00419");
        return 0;
    }

    char prefix[64];
    if (title_id && title_id[0] != '\0') {
        snprintf(prefix, sizeof(prefix), "%s", title_id);
    } else {
        snprintf(prefix, sizeof(prefix), "CUSA");
    }

    struct dirent *entry;
    char best_match[256] = {0};
    int highest_idx = -1;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, "pfsmnt") == 0) continue;

        if (strncasecmp(entry->d_name, prefix, strlen(prefix)) == 0) {
            char app0_path[512];
            snprintf(app0_path, sizeof(app0_path), "/mnt/sandbox/%s/app0", entry->d_name);
            struct stat st_app0;
            int has_app0 = (stat(app0_path, &st_app0) == 0);

            const char *underscore = strrchr(entry->d_name, '_');
            int idx = underscore ? atoi(underscore + 1) : 0;

            log_msg("[SANDBOX] Discovered sandbox candidate: %s (app0: %s, idx: %d)\n",
                    entry->d_name, has_app0 ? "ACTIVE" : "inactive", idx);

            if (has_app0 || idx > highest_idx || best_match[0] == '\0') {
                snprintf(best_match, sizeof(best_match), "/mnt/sandbox/%s", entry->d_name);
                highest_idx = idx;
                if (has_app0) {
                    // Active app0 mount is the definitive running game sandbox
                    break;
                }
            }
        }
    }
    closedir(dir);

    if (best_match[0] != '\0') {
        snprintf(out_sandbox, max_len, "%s", best_match);
    } else {
        snprintf(out_sandbox, max_len, "/mnt/sandbox/%s_000", title_id ? title_id : "CUSA00419");
    }
    return 0;
}

// Helper: copy a file directly and set executable permissions
static int copy_single_file(const char *src_path, const char *dst_path) {
    struct stat st_src, st_dst;
    if (stat(src_path, &st_src) != 0) return -1;

    // Guard against loopback self-truncation
    if (stat(dst_path, &st_dst) == 0) {
        if (st_src.st_dev == st_dst.st_dev && st_src.st_ino == st_dst.st_ino) {
            log_msg("[SANDBOX] '%s' is loopback identical to source, skipping copy.\n", dst_path);
            return 0;
        }
    }

    FILE *src = fopen(src_path, "rb");
    if (!src) return -1;

    FILE *dst = fopen(dst_path, "wb");
    if (!dst) {
        fclose(src);
        return -1;
    }

    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }

    fflush(dst);
    fclose(src);
    fclose(dst);

    chmod(dst_path, 0777);
    return 0;
}

// Stage host directory into a specific sandbox subdirectory (e.g. "plugins" or "data/orbx")
static int stage_into_sandbox(const char *real_src, const char *sandbox_dir,
                              const char *sandbox_rel_dir, const char *probe_filename) {
    char target_dir[512];
    const char *rel = (sandbox_rel_dir && sandbox_rel_dir[0] != '\0') ? sandbox_rel_dir : "plugins";
    while (*rel == '/') rel++;
    snprintf(target_dir, sizeof(target_dir), "%s/%s", sandbox_dir, rel);
    mkdir_recursive(target_dir);

    // 1. Try FreeBSD nullfs loopback mount
    struct statfs fs;
    int mounted = 0;
    if (statfs(target_dir, &fs) == 0 && strcmp(fs.f_fstypename, "nullfs") == 0) {
        log_msg("[NULLFS] Target '%s' is already mounted via nullfs\n", target_dir);
        mounted = 1;
    } else {
        char fstype_key[] = "fstype";
        char nullfs_val[] = "nullfs";
        char fspath_key[] = "fspath";
        char from_key[]   = "from";
        char target_key[] = "target";

        struct iovec iov[8];
        iov[0].iov_base = fstype_key;
        iov[0].iov_len  = sizeof(fstype_key);
        iov[1].iov_base = nullfs_val;
        iov[1].iov_len  = sizeof(nullfs_val);
        iov[2].iov_base = fspath_key;
        iov[2].iov_len  = sizeof(fspath_key);
        iov[3].iov_base = (void *)target_dir;
        iov[3].iov_len  = strlen(target_dir) + 1;
        iov[4].iov_base = from_key;
        iov[4].iov_len  = sizeof(from_key);
        iov[5].iov_base = (void *)real_src;
        iov[5].iov_len  = strlen(real_src) + 1;
        iov[6].iov_base = target_key;
        iov[6].iov_len  = sizeof(target_key);
        iov[7].iov_base = (void *)real_src;
        iov[7].iov_len  = strlen(real_src) + 1;

        log_msg("[NULLFS] Attempting nmount: '%s' -> '%s'...\n", real_src, target_dir);
        int res = nmount(iov, 8, 0);
        if (res == 0) {
            log_msg("[NULLFS] nmount syscall succeeded!\n");
            mounted = 1;
        } else {
            log_msg("[NULLFS] nmount returned errno %d (%s)\n", errno, strerror(errno));
        }
    }

    if (mounted) {
        record_mountpoint(target_dir);
    }

    // 2. Optional Verification / Direct Fallback Copy when a probe file is requested
    int ok = mounted;
    if (probe_filename && probe_filename[0] != '\0') {
        char probe_dst[512], probe_src[512];
        snprintf(probe_dst, sizeof(probe_dst), "%s/%s", target_dir, probe_filename);
        snprintf(probe_src, sizeof(probe_src), "%s/%s", real_src, probe_filename);

        struct stat st_dst, st_src;
        int src_valid = (stat(probe_src, &st_src) == 0 && st_src.st_size > 0);
        int visible   = (stat(probe_dst, &st_dst) == 0 && st_dst.st_size > 0);

        if (visible) {
            log_msg("[SANDBOX] Verified visible in '%s': %s (%lld bytes)\n",
                    target_dir, probe_filename, (long long)st_dst.st_size);
            ok = 1;
        } else if (src_valid) {
            log_msg("[SANDBOX] Probe '%s' not visible after mount. Copying file directly...\n", probe_filename);
            copy_single_file(probe_src, probe_dst);
            if (stat(probe_dst, &st_dst) == 0 && st_dst.st_size > 0) {
                log_msg("[SANDBOX] Direct copy succeeded: %s (%lld bytes)\n",
                        probe_filename, (long long)st_dst.st_size);
                ok = 1;
            } else {
                log_msg("[SANDBOX] Error: Failed to copy %s into '%s'!\n", probe_filename, target_dir);
                ok = 0;
            }
        }
    }

    return ok ? 0 : -1;
}

int mount_sandbox_subpath(const char *title_id, const char *host_source_dir,
                          const char *sandbox_rel_dir, const char *probe_filename,
                          char *out_sandbox_path, size_t max_len) {
    if (!host_source_dir) return -1;

    const char *rel = (sandbox_rel_dir && sandbox_rel_dir[0] != '\0') ? sandbox_rel_dir : "plugins";
    while (*rel == '/') rel++;

    // 1. Resolve real path of host directory (resolves /data symlinks)
    char real_src[512];
    if (!realpath(host_source_dir, real_src)) {
        snprintf(real_src, sizeof(real_src), "%s", host_source_dir);
    }

    // 2. Resolve primary active sandbox root for this game
    char sandbox_root[256];
    find_sandbox_directory(title_id, sandbox_root, sizeof(sandbox_root));
    log_msg("[SANDBOX] Selected primary sandbox: %s (target: /%s)\n", sandbox_root, rel);

    // 3. Stage into primary sandbox
    int res = stage_into_sandbox(real_src, sandbox_root, rel, probe_filename);

    // 4. Mirror/stage into any other matching sandboxes for this title_id
    DIR *dir = opendir("/mnt/sandbox");
    if (dir) {
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "%s", title_id ? title_id : "CUSA00419");
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            if (strcmp(entry->d_name, "pfsmnt") == 0) continue;
            if (strncasecmp(entry->d_name, prefix, strlen(prefix)) == 0) {
                char candidate[512];
                snprintf(candidate, sizeof(candidate), "/mnt/sandbox/%s", entry->d_name);
                if (strcmp(candidate, sandbox_root) != 0) {
                    stage_into_sandbox(real_src, candidate, rel, probe_filename);
                }
            }
        }
        closedir(dir);
    }

    if (out_sandbox_path) {
        snprintf(out_sandbox_path, max_len, "/%s", rel);
    }

    log_msg("[SANDBOX] Staging Complete! Game sees '%s' at '/%s'\n", host_source_dir, rel);
    return res;
}

int mount_sandbox_nullfs(const char *title_id, const char *host_source_dir, char *out_sandbox_path, size_t max_len) {
    return mount_sandbox_subpath(title_id, host_source_dir, "plugins", NULL, out_sandbox_path, max_len);
}

int unmount_sandbox_nullfs(const char *title_id) {
    (void)title_id;
    for (size_t i = g_mountpoint_count; i > 0; i--) {
        const char *mp = g_mountpoints[i - 1];
        if (mp[0] != '\0') {
            struct statfs fs;
            // Only unmount if it is actually still mounted as nullfs
            if (statfs(mp, &fs) == 0 && strcmp(fs.f_fstypename, "nullfs") == 0) {
                // Try clean unmount without MNT_FORCE first to prevent kernel vnode deadlocks
                if (unmount(mp, 0) != 0) {
                    usleep(30000); // 30ms brief backoff
                    unmount(mp, MNT_FORCE);
                }
            }
            g_mountpoints[i - 1][0] = '\0';
        }
    }
    g_mountpoint_count = 0;
    return 0;
}
