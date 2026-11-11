#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBX_WRAPPER_FILE_OFFSET   0xf870u
#define ORBX_SYSCALL_FILE_OFFSET   0xfa80u
#define ORBX_SYS_SDK_BRIDGE_BYTES  45u
#define ORBX_SYSCALL_BRIDGE_BYTES  36u
#define ORBX_SYSCALL_PREFIX_BYTES  12u

typedef struct __attribute__((packed)) {
    int32_t pid;
    char name[40];
    char path[64];
    char title[16];
    char content[64];
    char version[6];
    uint64_t base;
} HhpRaiInfo;

// Find native authorized syscall gate (0f 05 c3) inside libkernel.sprx
uint64_t find_native_syscall_gate(pid_t pid);

// Build the 45-byte position-independent sys_sdk bridge
int build_orbx_sys_sdk_bridge(uint8_t *out, size_t capacity, uint64_t info_addr);

// Build the 36-byte native syscall bridge
int build_orbx_syscall_bridge(uint8_t *out, size_t capacity, uint64_t native_gate);

// Patch in-memory buffer of orbx.prx with bridge jumps
int patch_orbx_buffer(uint8_t *bytes, size_t size, uint64_t sys_sdk_bridge_addr, uint64_t syscall_bridge_addr);

// Deploy patched orbx.prx to destination file
int deploy_patched_orbx(const uint8_t *src_bytes, size_t src_size, const char *dst_path,
                        uint64_t sys_sdk_bridge_addr, uint64_t syscall_bridge_addr);

#ifdef __cplusplus
}
#endif
