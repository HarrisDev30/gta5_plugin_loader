#include <stdint.h>
#include <stddef.h>

typedef struct __attribute__((packed)) {
    int32_t pid;
    char name[40];
    char path[64];
    char title[16];
    char content[64];
    char version[6];
    uint64_t base;
} HhpRaiInfo;

typedef struct {
    uint32_t pid;
    uint64_t eboot_base;
    uint64_t native_syscall_gate;
    char title_id[16];
} OrbxBridgeData;

__attribute__((section(".text.sys_sdk")))
int orbx_sys_sdk_bridge(int cmd, void *arg, OrbxBridgeData *data) {
    if (cmd == 4) {
        if (!arg) return -1;
        HhpRaiInfo *info = (HhpRaiInfo*)arg;
        uint8_t *p = (uint8_t*)arg;
        for (unsigned i = 0; i < sizeof(HhpRaiInfo); i++) p[i] = 0;
        info->pid = data ? data->pid : 0;
        info->base = data ? data->eboot_base : 0x400000;
        const char *name = "eboot.bin";
        for (int i = 0; name[i]; i++) info->name[i] = name[i];
        const char *path = "/app0/eboot.bin";
        for (int i = 0; path[i]; i++) info->path[i] = path[i];
        const char *title = (data && data->title_id[0]) ? data->title_id : "CUSA00419";
        for (int i = 0; title[i]; i++) info->title[i] = title[i];
        const char *content = "UP1004-CUSA00419_00-GTAV000000000001";
        for (int i = 0; content[i]; i++) info->content[i] = content[i];
        info->version[0] = '0'; info->version[1] = '1'; info->version[2] = '.'; info->version[3] = '0'; info->version[4] = '0';
        return 0;
    }
    return 0;
}

