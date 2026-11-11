#define _GNU_SOURCE
#include "orbx_compat.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <ps5/kernel.h>

// Signature prefixes for verifying Orbx PRX functions
static const uint8_t g_expected_wrapper_prefix[16] = {
    0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xec, 0x10,
    0x48, 0x89, 0x7d, 0xf8, 0x48, 0x89, 0x75, 0xf0
};

static const uint8_t g_expected_syscall_prefix[12] = {
    0x48, 0xc7, 0xc0, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x89, 0xca, 0x0f, 0x05
};

// Find an authorized 0f 05 c3 (syscall; ret) gate inside libkernel.sprx
uint64_t find_native_syscall_gate(pid_t pid) {
    static const char *names[] = {"libkernel.sprx", "libkernel_sys.sprx"};
    static const char nid_spin_trylock[] = "rCTGkBIHfPY"; // pthread_spin_trylock
    static const char nid_getpid[]       = "HoLVWNanBBc"; // getpid

    static const uint8_t anchor_pattern[70] = {
        0x64,0x48,0x8b,0x0c,0x25,0x10,0x00,0x00,0x00,0xb8,0x16,0x00,0x00,0x00,
        0x48,0x85,0xff,0x74,0x17,0x48,0x8b,0x3f,0x48,0x85,0xff,0x74,0x0f,0x8b,
        0x09,0x31,0xc0,0xf0,0x0f,0xb1,0x0f,0xb8,0x00,0x00,0x00,0x00,0x75,0x01,
        0xc3,0xf6,0x47,0x04,0x08,0xb8,0x10,0x00,0x00,0x00,0x74,0xf4,0x55,0x48,
        0x89,0xe5,0xbe,0xff,0xff,0xff,0xff,0xe8,0x00,0x00,0x00,0x00,0x5d,0xc3
    };
    static const uint8_t helper_pattern[16] = {
        0x41,0x89,0xf1,0xbe,0x04,0x00,0x00,0x00,0x31,0xd2,0x31,0xc9,0x45,0x31,0xc0,0xe9
    };
    static const uint8_t stub_pattern[13] = {
        0x48,0xc7,0xc0,0xc6,0x01,0x00,0x00,0x49,0x89,0xca,0x0f,0x05,0xc3
    };
    static const uint8_t getpid_pattern[13] = {
        0x48,0xc7,0xc0,0x14,0x00,0x00,0x00,0x49,0x89,0xca,0x0f,0x05,0xc3
    };

    // Method 1: HyperHEN / TacHen call-chain displacement resolver (100% verified)
    // Resolves pthread_spin_trylock -> private helper -> _umtx_op stub (+10 = 0f 05 c3)
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        uint32_t handle = 0;
        if (kernel_dynlib_handle(pid, names[i], &handle) != 0 || handle == 0) continue;

        intptr_t libk_base = kernel_dynlib_mapbase_addr(pid, handle);
        if (!libk_base) continue;

        uint64_t anchor = (uint64_t)kernel_dynlib_resolve(pid, handle, nid_spin_trylock);
        if (!anchor) {
            log_msg("[ORBX] Method 1: %s %s unresolved\n", names[i], nid_spin_trylock);
            continue;
        }

        log_msg("[ORBX] Method 1: %s anchor resolved @ 0x%llx (offset +0x%llx)\n",
                names[i], (unsigned long long)anchor, (unsigned long long)(anchor - (uint64_t)libk_base));

        uint8_t anchor_code[70] = {0};
        if (kernel_proc_copyout(pid, (intptr_t)anchor, anchor_code, sizeof(anchor_code)) != 0) {
            log_msg("[ORBX] Method 1: Failed to read anchor code @ 0x%llx\n", (unsigned long long)anchor);
            continue;
        }

        // Verify full 64-byte anchor prefix and trailing pop rbp; ret (5d c3)
        if (memcmp(anchor_code, anchor_pattern, 64) != 0 ||
            memcmp(anchor_code + 68, anchor_pattern + 68, 2) != 0) {
            log_msg("[ORBX] Method 1: %s anchor code mismatch at 0x%llx (bytes 0..7: %02x %02x %02x %02x %02x %02x %02x %02x, byte 63: 0x%02x, 68..69: 0x%02x 0x%02x)\n",
                    names[i], (unsigned long long)anchor,
                    anchor_code[0], anchor_code[1], anchor_code[2], anchor_code[3],
                    anchor_code[4], anchor_code[5], anchor_code[6], anchor_code[7],
                    anchor_code[63], anchor_code[68], anchor_code[69]);
            continue;
        }

        int32_t disp1 = 0;
        memcpy(&disp1, anchor_code + 64, sizeof(disp1));
        uint64_t helper = (uint64_t)((int64_t)anchor + 68 + (int64_t)disp1);

        uint8_t helper_code[20] = {0};
        if (kernel_proc_copyout(pid, (intptr_t)helper, helper_code, sizeof(helper_code)) != 0) {
            log_msg("[ORBX] Method 1: Failed to read helper code @ 0x%llx\n", (unsigned long long)helper);
            continue;
        }

        // Verify full 16-byte helper prefix
        if (memcmp(helper_code, helper_pattern, sizeof(helper_pattern)) != 0) {
            log_msg("[ORBX] Method 1: %s helper code mismatch at 0x%llx (byte 15: 0x%02x)\n",
                    names[i], (unsigned long long)helper, helper_code[15]);
            continue;
        }

        int32_t disp2 = 0;
        memcpy(&disp2, helper_code + 16, sizeof(disp2));
        uint64_t stub = (uint64_t)((int64_t)helper + 20 + (int64_t)disp2);

        uint8_t stub_code[13] = {0};
        if (kernel_proc_copyout(pid, (intptr_t)stub, stub_code, sizeof(stub_code)) != 0) {
            log_msg("[ORBX] Method 1: Failed to read stub code @ 0x%llx\n", (unsigned long long)stub);
            continue;
        }

        // Verify full 13-byte _umtx_op syscall stub: 48 c7 c0 c6 01 00 00 49 89 ca 0f 05 c3
        if (memcmp(stub_code, stub_pattern, sizeof(stub_pattern)) != 0) {
            log_msg("[ORBX] Method 1: %s stub code mismatch at 0x%llx (bytes 10..12: %02x %02x %02x)\n",
                    names[i], (unsigned long long)stub, stub_code[10], stub_code[11], stub_code[12]);
            continue;
        }

        uint64_t gate = stub + 10;
        int prot = kernel_get_vmem_protection(pid, (intptr_t)gate, 3);
        if (prot > 0 && !(prot & PROT_EXEC)) {
            log_msg("[ORBX] Method 1: gate 0x%llx lacks PROT_EXEC (prot=%d)\n",
                    (unsigned long long)gate, prot);
            continue;
        }

        log_msg("[ORBX] Found Authorized Native Syscall Gate via %s:rCTGkBIHfPY @ 0x%llx (prot=%d)\n",
                names[i], (unsigned long long)gate, prot);
        return gate;
    }

    // Method 2: Direct check of getpid (HoLVWNanBBc)
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        uint32_t handle = 0;
        if (kernel_dynlib_handle(pid, names[i], &handle) != 0 || handle == 0) continue;

        intptr_t libk_base = kernel_dynlib_mapbase_addr(pid, handle);
        if (!libk_base) continue;

        uint64_t getpid_fn = (uint64_t)kernel_dynlib_resolve(pid, handle, nid_getpid);
        if (!getpid_fn) continue;

        uint8_t code[16] = {0};
        if (kernel_proc_copyout(pid, (intptr_t)getpid_fn, code, sizeof(code)) == 0) {
            if (memcmp(code, getpid_pattern, sizeof(getpid_pattern)) == 0) {
                uint64_t gate = getpid_fn + 10;
                int prot = kernel_get_vmem_protection(pid, (intptr_t)gate, 3);
                if (prot > 0 && !(prot & PROT_EXEC)) continue;

                log_msg("[ORBX] Found Authorized Native Syscall Gate via %s:getpid @ 0x%llx (prot=%d)\n",
                        names[i], (unsigned long long)gate, prot);
                return gate;
            }
        }
    }

    // Method 3: Read-only scan of executable code pages for the 13-byte _umtx_op stub
    // ONLY checks pages already mapped as PROT_EXEC; NEVER touches VM protection
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        uint32_t handle = 0;
        if (kernel_dynlib_handle(pid, names[i], &handle) != 0 || handle == 0) continue;

        intptr_t libk_base = kernel_dynlib_mapbase_addr(pid, handle);
        if (!libk_base) continue;

        static uint8_t chunk[4096];
        for (uint64_t cur = (uint64_t)libk_base; cur < (uint64_t)libk_base + 0x40000; cur += 4096) {
            int prot = kernel_get_vmem_protection(pid, (intptr_t)cur, 4096);
            if (prot <= 0 || !(prot & PROT_EXEC)) continue; // Skip non-executable / data pages

            if (kernel_proc_copyout(pid, (intptr_t)cur, chunk, sizeof(chunk)) != 0) continue;
            for (size_t k = 0; k <= sizeof(chunk) - sizeof(stub_pattern); k++) {
                if (memcmp(chunk + k, stub_pattern, sizeof(stub_pattern)) == 0) {
                    uint64_t gate = cur + k + 10;
                    log_msg("[ORBX] Found Authorized Native Syscall Gate via %s .text scan @ 0x%llx (prot=%d)\n",
                            names[i], (unsigned long long)gate, prot);
                    return gate;
                }
            }
        }
    }

    log_msg("[ORBX] Error: Could not locate native syscall gate in any libkernel export or segment!\n");
    return 0;
}

int build_orbx_sys_sdk_bridge(uint8_t *out, size_t capacity, uint64_t info_addr) {
    if (!out || capacity < ORBX_SYS_SDK_BRIDGE_BYTES || info_addr < 0x10000) return -1;

    static const uint8_t template[45] = {
        0x83, 0xff, 0x04,                               // cmpl $4, %edi
        0x75, 0x1f,                                     // jne .done_success (+0x1f -> offset 0x24)
        0x48, 0x85, 0xf6,                               // testq %rsi, %rsi
        0x74, 0x1d,                                     // je .done_err (+0x1d -> offset 0x27)
        0x57,                                           // pushq %rdi
        0x56,                                           // pushq %rsi
        0x51,                                           // pushq %rcx
        0x48, 0x89, 0xf7,                               // movq %rsi, %rdi
        0x48, 0xbe, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // movabsq $info_addr, %rsi
        0xb9, 0xca, 0x00, 0x00, 0x00,                   // movl $202, %ecx (sizeof HhpRaiInfo)
        0xf3, 0xa4,                                     // rep movsb
        0x59,                                           // popq %rcx
        0x5e,                                           // popq %rsi
        0x5f,                                           // popq %rdi
        // offset 0x24: .done_success
        0x31, 0xc0,                                     // xorl %eax, %eax
        0xc3,                                           // retq
        // offset 0x27: .done_err
        0xb8, 0xff, 0xff, 0xff, 0xff,                   // movl $-1, %eax
        0xc3                                            // retq
    };

    memcpy(out, template, sizeof(template));
    memcpy(out + 18, &info_addr, sizeof(info_addr));
    return 0;
}

int build_orbx_syscall_bridge(uint8_t *out, size_t capacity, uint64_t native_gate) {
    if (!out || capacity < ORBX_SYSCALL_BRIDGE_BYTES || native_gate < 0x10000) return -1;

    // SysV AMD64 ABI Syscall Bridge:
    // When orbx.prx calls this bridge with 'call rax', the stack layout is:
    //   [rsp + 0x00] = return address back to orbx.prx (offset 0xfa80 + 12)
    //   [rsp + 0x08] = return address back to PRX caller of syscall
    //   [rsp + 0x10] = arg6 (6th syscall argument)
    // Tail jump to native_gate executes '0f 05 c3'. Native ret pops [rsp + 0x00]
    // and returns directly to orbx.prx with carry flag (CF) preserved!
    static const uint8_t template[36] = {
        0x48, 0x89, 0xf8,                               // movq %rdi, %rax (sysno)
        0x48, 0x89, 0xf7,                               // movq %rsi, %rdi (arg1)
        0x48, 0x89, 0xd6,                               // movq %rdx, %rsi (arg2)
        0x48, 0x89, 0xca,                               // movq %rcx, %rdx (arg3)
        0x4d, 0x89, 0xc2,                               // movq %r8,  %r10 (arg4)
        0x4d, 0x89, 0xc8,                               // movq %r9,  %r8  (arg5)
        0x4c, 0x8b, 0x4c, 0x24, 0x10,                   // movq 16(%rsp), %r9 (arg6: [rsp + 16])
        0x49, 0xbb, 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // movabsq $native_gate, %r11
        0x41, 0xff, 0xe3                                // jmpq *%r11
    };

    memcpy(out, template, sizeof(template));
    memcpy(out + 25, &native_gate, sizeof(native_gate));
    return 0;
}

int patch_orbx_buffer(uint8_t *bytes, size_t size, uint64_t sys_sdk_bridge_addr, uint64_t syscall_bridge_addr) {
    if (!bytes || size < ORBX_SYSCALL_FILE_OFFSET + 43) return -1;

    // 1. Locate Wrapper function (GoldHEN sys_sdk_cmd)
    size_t wrapper_off = ORBX_WRAPPER_FILE_OFFSET;
    if (memcmp(bytes + wrapper_off, g_expected_wrapper_prefix, sizeof(g_expected_wrapper_prefix)) != 0) {
        // Fallback scan
        wrapper_off = 0;
        for (size_t i = 0x4000; i < size - 42; i++) {
            if (memcmp(bytes + i, g_expected_wrapper_prefix, sizeof(g_expected_wrapper_prefix)) == 0) {
                wrapper_off = i;
                break;
            }
        }
        if (!wrapper_off) {
            log_msg("[ORBX] Error: Could not locate GoldHEN sys_sdk_cmd wrapper in PRX!\n");
            return -1;
        }
    }

    // 2. Locate Raw Syscall function
    size_t syscall_off = ORBX_SYSCALL_FILE_OFFSET;
    if (memcmp(bytes + syscall_off, g_expected_syscall_prefix, sizeof(g_expected_syscall_prefix)) != 0) {
        // Fallback scan
        syscall_off = 0;
        for (size_t i = 0x4000; i < size - 43; i++) {
            if (memcmp(bytes + i, g_expected_syscall_prefix, sizeof(g_expected_syscall_prefix)) == 0) {
                syscall_off = i;
                break;
            }
        }
        if (!syscall_off) {
            log_msg("[ORBX] Error: Could not locate raw syscall stub in PRX!\n");
            return -1;
        }
    }

    log_msg("[ORBX] Found Orbx entry points: Wrapper @ 0x%zx, Syscall @ 0x%zx\n",
            wrapper_off, syscall_off);

    // Patch 42-byte Wrapper with direct jump to sys_sdk_bridge_addr
    memset(bytes + wrapper_off, 0x90, 42);
    bytes[wrapper_off + 0] = 0x48;
    bytes[wrapper_off + 1] = 0xb8;
    memcpy(bytes + wrapper_off + 2, &sys_sdk_bridge_addr, 8);
    bytes[wrapper_off + 10] = 0xff;
    bytes[wrapper_off + 11] = 0xe0; // jmp rax

    // Patch Raw Syscall function:
    // Replace ONLY the first 12 bytes with 'mov rax, syscall_bridge_addr; call rax'
    // Preserves bytes 12..42: '0f 82 01 00 00 00' (jb .error), 'c3' (ret), and __error handler!
    bytes[syscall_off + 0] = 0x48;
    bytes[syscall_off + 1] = 0xb8;
    memcpy(bytes + syscall_off + 2, &syscall_bridge_addr, 8);
    bytes[syscall_off + 10] = 0xff;
    bytes[syscall_off + 11] = 0xd0; // call rax

    log_msg("[ORBX] Successfully patched Orbx PRX image (SDK Bridge: 0x%llx, Syscall Bridge: 0x%llx)\n",
            (unsigned long long)sys_sdk_bridge_addr, (unsigned long long)syscall_bridge_addr);
    return 0;
}

int deploy_patched_orbx(const uint8_t *src_bytes, size_t src_size, const char *dst_path,
                        uint64_t sys_sdk_bridge_addr, uint64_t syscall_bridge_addr) {
    if (!src_bytes || src_size < 1000000 || !dst_path) return -1;

    uint8_t *buf = malloc(src_size);
    if (!buf) return -1;

    memcpy(buf, src_bytes, src_size);

    if (patch_orbx_buffer(buf, src_size, sys_sdk_bridge_addr, syscall_bridge_addr) != 0) {
        log_msg("[ORBX] Error: patch_orbx_buffer failed, deployment aborted!\n");
        free(buf);
        return -1;
    }

    unlink(dst_path);
    FILE *f = fopen(dst_path, "wb");
    if (!f) {
        log_msg("[ORBX] Error opening %s for writing: errno=%d (%s)\n",
                dst_path, errno, strerror(errno));
        free(buf);
        return -1;
    }

    size_t written = fwrite(buf, 1, src_size, f);
    fclose(f);
    free(buf);

    chmod(dst_path, 0777);
    log_msg("[ORBX] Successfully deployed patched orbx.prx to %s (%zu bytes)\n",
            dst_path, written);
    return (written == src_size) ? 0 : -1;
}
