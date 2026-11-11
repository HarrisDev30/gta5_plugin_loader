#ifndef ORBX_PIC_COMMON_H
#define ORBX_PIC_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include "orbx_native_abi.h"

OrbxNativeContext *orbx_pic_get_ctx(void);
void orbx_pic_log_raw(OrbxNativeContext *ctx, const char *msg);
size_t orbx_pic_fmt_str(char *dst, size_t pos, size_t cap, const char *s);
size_t orbx_pic_fmt_u64(char *dst, size_t pos, size_t cap, uint64_t v);
size_t orbx_pic_fmt_hex32(char *dst, size_t pos, size_t cap, uint32_t v);
uint32_t orbx_pic_joaat(const char *s);

#endif /* ORBX_PIC_COMMON_H */

