/*
 * ModHEN GTA V v1.00 (CUSA00419 / b323) Freestanding PS5-Native Mod Menu
 * Compiled into orbx_native.bin under strict XOM-safe PIC rules:
 *   - Zero .data, .data.rel.ro, .bss, .got
 *   - Zero stored char* pointers in static const tables (fixed char[] arrays only)
 *   - Runs inside GTA V main_persistent script thread via WAIT handler hook
 */

#include <stdint.h>
#include <stddef.h>
#include "orbx_native_abi.h"
#include "orbx_pic_common.h"
#include "orbx_menu_tables.h"

#define SCR_HASH_MAIN_PERSISTENT 0x5700179CU
#define SCR_HASH_FREEMODE        0xC875557DU

/* PS4/PS5 GTA V control IDs */
#define CTRL_PLAYER   0
#define CTRL_FRONTEND 2

#define IN_LOOK_LR         1
#define IN_LOOK_UD         2
#define IN_CHAR_WHEEL      19
#define IN_SPRINT          21
#define IN_JUMP            22
#define IN_ATTACK          24
#define IN_AIM             25
#define IN_PHONE           27
#define IN_MOVE_LR         30
#define IN_MOVE_UD         31
#define IN_COVER           44
#define IN_RELOAD          45
#define IN_VEH_CIN_CAM     80
#define IN_VEH_HORN        86
#define IN_VEH_NEXT_WPN    99
#define IN_VEH_PREV_WPN    100
#define IN_FE_DOWN         187
#define IN_FE_UP           188
#define IN_FE_LEFT         189
#define IN_FE_RIGHT        190
#define IN_FE_ACCEPT       201
#define IN_FE_CANCEL       202
#define IN_FE_LB           205
#define IN_FE_RB           206
#define IN_SPAD_UP         232
#define IN_SPAD_DOWN       233
#define IN_SPAD_LEFT       234
#define IN_SPAD_RIGHT      235
#define IN_CELL_UP         172
#define IN_CELL_DOWN       173
#define IN_CELL_LEFT       174
#define IN_CELL_RIGHT      175
#define IN_CELL_SELECT     176
#define IN_CELL_CANCEL     177

static int sanitize_live_keybinds(OrbxNativeContext *oc);

/* Sony Orbis / PS5 ScePadData::buttons hardware bitmasks */
#define SCE_PAD_BTN_L3        0x00000002u
#define SCE_PAD_BTN_R3        0x00000004u
#define SCE_PAD_BTN_OPTIONS   0x00000008u
#define SCE_PAD_BTN_UP        0x00000010u
#define SCE_PAD_BTN_RIGHT     0x00000020u
#define SCE_PAD_BTN_DOWN      0x00000040u
#define SCE_PAD_BTN_LEFT      0x00000080u
#define SCE_PAD_BTN_L2        0x00000100u
#define SCE_PAD_BTN_R2        0x00000200u
#define SCE_PAD_BTN_L1        0x00000400u
#define SCE_PAD_BTN_R1        0x00000800u
#define SCE_PAD_BTN_TRIANGLE  0x00001000u
#define SCE_PAD_BTN_CIRCLE    0x00002000u
#define SCE_PAD_BTN_CROSS     0x00004000u
#define SCE_PAD_BTN_SQUARE    0x00008000u
#define SCE_PAD_BTN_TOUCH_PAD 0x00100000u

/* Debounced menu action bitmask */
#define BTN_RB      (1u << 0)
#define BTN_LB      (1u << 1)
#define BTN_UP      (1u << 2)
#define BTN_DOWN    (1u << 3)
#define BTN_LEFT    (1u << 4)
#define BTN_RIGHT   (1u << 5)
#define BTN_ACCEPT  (1u << 6)
#define BTN_CANCEL  (1u << 7)

/* Applied transition flags so reverting a toggle off runs cleanly once */
#define APP_GOD          (1u << 0)
#define APP_WANTED       (1u << 1)
#define APP_FASTRUN      (1u << 2)
#define APP_NORAGDOLL    (1u << 3)
#define APP_INF_AMMO     (1u << 4)
#define APP_VEH_GOD      (1u << 5)
#define APP_INVISIBLE    (1u << 6)
#define APP_GFX_MOD      (1u << 7)
#define APP_GFX_GRAIN    (1u << 8)
#define APP_CLOUD_ALPHA  (1u << 10)
#define APP_NOCLIP       (1u << 11)
#define APP_SEATBELT     (1u << 13)
#define APP_DRIFT        (1u << 14)
#define APP_TORQUE       (1u << 15)
#define APP_LOWGRAV      (1u << 16)
#define APP_TIMESCALE    (1u << 17)
#define APP_CLOCK_PAUSE  (1u << 18)
#define APP_MOBILE_RADIO (1u << 19)
#define APP_FP_LOCK      (1u << 20)
#define APP_CINEMATIC    (1u << 21)
#define APP_BULLETPROOF  (1u << 22)
#define APP_DOORS_LOCK   (1u << 23)
#define APP_FASTRUN_TOG  (1u << 24)
#define APP_DENSITY      (1u << 25)
#define APP_GFX_EXTRA_MOD (1u << 26)
#define APP_GFX_SHADOWS   (1u << 27)

#define MAX_VISIBLE_ROWS 11
#define MAX_MENU_ROWS    64

typedef struct __attribute__((aligned(16))) {
    uint64_t *ret_val;
    uint32_t  arg_count;
    uint32_t  pad0;
    uint64_t *arg_values;
    uint32_t  data_count;
    uint32_t  pad1;
    uint64_t *orig[4];
    float     vecs[4][4];
} GtaNativeCtx;

typedef struct {
    float x, y, z;
} Vec3;

/* 24-byte aligned vector buffer for native pointer out-params (e.g. GET_PED_LAST_WEAPON_IMPACT_COORD) */
typedef struct {
    float    x;
    uint32_t _p0;
    float    y;
    uint32_t _p1;
    float    z;
    uint32_t _p2;
} ScrVec3;

typedef void (*GtaNativeFn)(GtaNativeCtx *ctx);

enum RowKind {
    ROW_SUB     = 0,
    ROW_TOGGLE  = 1,
    ROW_ACTION  = 2,
    ROW_CYCLE   = 3,
    ROW_DIVIDER = 4
};

enum KeybindSlotId {
    KB_SLOT_NONE         = 0,
    KB_SLOT_OPEN         = 1,
    KB_SLOT_ACCEPT       = 2,
    KB_SLOT_CANCEL       = 3,
    KB_SLOT_TAB_PREV     = 4,
    KB_SLOT_TAB_NEXT     = 5,
    KB_SLOT_HORN_BOOST   = 6,
    KB_SLOT_NOCLIP       = 7,
    KB_SLOT_QUICK_REPAIR = 8,
    KB_SLOT_QUICK_HEAL   = 9,
    KB_SLOT_SUPER_BRAKE  = 10
};

enum MenuItemId {
    ITEM_NONE = 0,
    /* Submenu targets */
    ITEM_SUB_PLAYER,
    ITEM_SUB_WEAPONS,
    ITEM_SUB_VEHICLE,
    ITEM_SUB_SPAWN,
    ITEM_SUB_TELEPORT,
    ITEM_SUB_WORLD,
    ITEM_SUB_ADDON_SPAWN,
    ITEM_SUB_VEH_CUSTOM,
    ITEM_SUB_VEH_COLORS,
    ITEM_SUB_VEH_LIGHTS,
    ITEM_SUB_VEH_DOORS,
    ITEM_SUB_CAMERA,
    ITEM_SUB_CLOUDS,
    ITEM_SUB_SETTINGS,
    ITEM_SUB_GRAPHICS,
    ITEM_SUB_KEYBINDS,
    ITEM_SUB_SPAWN_CAT,
    ITEM_SUB_IPL_INTERIORS,

    /* Player / Self options */
    ITEM_TOG_GODMODE,
    ITEM_TOG_NEVER_WANTED,
    ITEM_CYC_WANTED_CAP,
    ITEM_TOG_SUPER_JUMP,
    ITEM_TOG_FAST_RUN,
    ITEM_CYC_SPRINT_MULT,
    ITEM_CYC_SWIM_MULT,
    ITEM_TOG_NO_RAGDOLL,
    ITEM_TOG_INVISIBLE,
    ITEM_TOG_NOCLIP,
    ITEM_CYC_NOCLIP_SPEED,
    ITEM_TOG_SPEC_INF,
    ITEM_ACT_HEAL_ARMOR,
    ITEM_ACT_CLEAR_WANTED,
    ITEM_ACT_FORCE_VISIBLE,

    /* Weapon options */
    ITEM_ACT_GIVE_WEAPONS,
    ITEM_ACT_REMOVE_WEAPONS,
    ITEM_TOG_INF_AMMO,
    ITEM_TOG_EXP_AMMO,
    ITEM_TOG_FIRE_AMMO,
    ITEM_TOG_EXP_MELEE,

    /* Vehicle options */
    ITEM_TOG_VEH_GOD,
    ITEM_TOG_SEATBELT,
    ITEM_TOG_HORN_BOOST,
    ITEM_TOG_VEH_DRIFT,
    ITEM_CYC_VEH_TORQUE,
    ITEM_TOG_BULLETPROOF_TIRES,
    ITEM_CYC_SPEEDO_HUD,
    ITEM_ACT_WARP_NEAREST_VEH,
    ITEM_ACT_FIX_VEH,
    ITEM_ACT_WASH_VEH,
    ITEM_ACT_FLIP_VEH,
    ITEM_ACT_BOOST_VEH,
    ITEM_ACT_PLATE_MODHEN,
    ITEM_ACT_PLATE_OXAGON,
    ITEM_ACT_PLATE_KEYBOARD,
    ITEM_ACT_DOORS_OPEN_ALL,
    ITEM_ACT_DOORS_SHUT_ALL,
    ITEM_TOG_DOORS_LOCK,
    ITEM_ACT_DOOR_TOGGLE,
    ITEM_ACT_EXTRA_TOGGLE,
    ITEM_TOG_REALISTIC_FUEL,
    ITEM_ACT_REFUEL_VEH,
    ITEM_TOG_GTA6_HUD,
    ITEM_TOG_GTA6_WHEEL,

    /* Vehicle Colors & Paint */
    ITEM_CYC_COLOR_TARGET,
    ITEM_CYC_COLOR_CAT,
    ITEM_ACT_COLOR_PRESET,
    ITEM_CYC_WINDOW_TINT,

    /* Vehicle Performance & LSC Body Mods */
    ITEM_ACT_MAX_PERF,
    ITEM_CYC_MOD_ENGINE,
    ITEM_CYC_MOD_BRAKES,
    ITEM_CYC_MOD_TRANS,
    ITEM_CYC_MOD_SUSP,
    ITEM_CYC_MOD_ARMOR,
    ITEM_TOG_TURBO,
    ITEM_TOG_XENON,
    ITEM_CYC_LSC_PART,
    ITEM_CYC_LSC_SLOT,
    ITEM_CYC_LSC_VAL,

    /* Vehicle Neons & Lights */
    ITEM_TOG_NEON_ALL,
    ITEM_TOG_NEON_FRONT,
    ITEM_TOG_NEON_BACK,
    ITEM_TOG_NEON_LEFT,
    ITEM_TOG_NEON_RIGHT,
    ITEM_CYC_NEON_COLOR,
    ITEM_CYC_SMOKE_COLOR,

    /* Spawner & Teleport entries */
    ITEM_SPAWN_KEYBOARD,
    ITEM_ADDON_IDX,
    ITEM_SPAWN_CAT_ITEM,
    ITEM_TP_WP,
    ITEM_TP_IDX,
    ITEM_TP_IPL,

    /* World / Sky / Clouds entries */
    ITEM_CYC_WEATHER,
    ITEM_TOG_WEATHER_PERSIST,
    ITEM_CYC_TIME,
    ITEM_TOG_CLOCK_PAUSE,
    ITEM_CYC_TIME_SCALE,
    ITEM_ACT_RESET_TIME,
    ITEM_CYC_DENSITY,
    ITEM_TOG_LOW_GRAV,
    ITEM_TOG_MOBILE_RADIO,
    ITEM_ACT_CLEAR_PEDS,
    ITEM_ACT_CLEAR_VEHS,
    ITEM_ACT_APPLY_CLOUD_HAT,
    ITEM_ACT_CLEAR_CLOUDS,
    ITEM_CYC_CLOUD_HAT_CHOICE,
    ITEM_CYC_CLOUD_KEYFRAME,
    ITEM_CYC_CLOUD_ALPHA,
    ITEM_TOG_CALMER_POLICE,

    /* Camera & First Person POV entries */
    ITEM_CYC_POV_FOV,
    ITEM_TOG_FP_LOCK,
    ITEM_TOG_CLIP_FIX,
    ITEM_CYC_PED_VIEW,
    ITEM_CYC_VEH_VIEW,
    ITEM_TOG_CINEMATIC_CAM,
    ITEM_TOG_CAM_ANTI_SHAKE,
    ITEM_ACT_CAM_RESET_BEHIND,

    /* Alternate First Person & Camera Modes (PC ASI Mod Port) */
    ITEM_CYC_AFP_MODE,
    ITEM_CYC_AFP_FOV,
    ITEM_CYC_AFP_HEADBOB,
    ITEM_CYC_AFP_DRIFT,
    ITEM_CYC_AFP_RECOIL,
    ITEM_TOG_AFP_INERTIA,
    ITEM_TOG_AFP_CROUCH,
    ITEM_CYC_AFP_ADS_ZOOM,
    ITEM_CYC_AFP_CROSSHAIR,
    ITEM_TOG_AFP_AUTOTURN,
    ITEM_TOG_AFP_CLIP_FIX,
    ITEM_ACT_AFP_RESET,

    /* Settings, Keybinds & Themes */
    ITEM_CYC_MENU_POS,
    ITEM_CYC_MENU_SCALE,
    ITEM_CYC_THEME_ACCENT,
    ITEM_TOG_SOUND_FX,
    ITEM_TOG_KEEP_STEER,
    ITEM_TOG_NAV_STICK,
    ITEM_ACT_KEYBIND_SLOT,
    ITEM_ACT_KEYBIND_CLEAR,
    ITEM_ACT_KEYBIND_RESET,
    ITEM_ACT_SAVE_INI,

    /* ENB / ReShade Live Graphics Enhancer */
    ITEM_CYC_GFX_PRESET,
    ITEM_CYC_GFX_STRENGTH,
    ITEM_CYC_GFX_GRAIN,
    ITEM_CYC_GFX_POSTFX,
    ITEM_ACT_GFX_RESET,
    ITEM_ACT_GFX_QUICK,
    /* Stage 1 & 2 PC ENB Controls in Mod Menu */
    ITEM_CYC_ENB_EXTRA,
    ITEM_CYC_ENB_SHADOW,
    ITEM_CYC_ENB_SHADOW_SCALE,
    ITEM_CYC_ENB_SSAO,
    ITEM_CYC_ENB_BLOOM,
    ITEM_CYC_ENB_REFL,
    ITEM_CYC_ENB_DOF,
    ITEM_CYC_ENB_MOTIONBLUR
};

typedef struct {
    char     label[32];
    char     badge[24];
    char     desc[64];
    uint8_t  kind;
    uint8_t  id;
    uint8_t  arg;
    uint8_t  on;
} MenuRow;

static inline uint64_t f2u(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;
    return (uint64_t)v.u;
}

static inline float u2f(uint64_t u) {
    union { float f; uint32_t u; } v;
    v.u = (uint32_t)u;
    return v.f;
}

static inline uint64_t ptr2u(const void *p) {
    return (uint64_t)(uintptr_t)p;
}

static void zero_bytes(void *p, size_t n) {
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++) {
        b[i] = 0;
    }
    __asm__ volatile("" ::: "memory");
}

static void copy_cstr(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < cap) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

__attribute__((noinline))
static uint64_t nat_invoke(OrbxNativeContext *oc, uint32_t nat_id,
                           uint64_t *args, uint32_t argc, Vec3 *out_vec) {
    if (!oc || nat_id >= ORBX_NAT_COUNT) return 0;
    uint64_t addr = oc->menu.natives[nat_id];
    if (!addr) return 0;

    uint64_t ret[4];
    ret[0] = ret[1] = ret[2] = ret[3] = 0;

    GtaNativeCtx nctx;
    zero_bytes(&nctx, sizeof(nctx));
    nctx.ret_val    = ret;
    nctx.arg_count  = argc;
    nctx.arg_values = args;

    ((GtaNativeFn)(uintptr_t)addr)(&nctx);

    if (nctx.data_count > 0 && nctx.data_count <= 4) {
        for (uint32_t i = 0; i < nctx.data_count; i++) {
            uint64_t *dst = nctx.orig[i];
            if (dst) {
                *(float *)((uint8_t *)dst + 0)  = nctx.vecs[i][0];
                *(float *)((uint8_t *)dst + 8)  = nctx.vecs[i][1];
                *(float *)((uint8_t *)dst + 16) = nctx.vecs[i][2];
            }
        }
    }

    if (out_vec) {
        out_vec->x = u2f(ret[0]);
        out_vec->y = u2f(ret[1]);
        out_vec->z = u2f(ret[2]);
    }
    return ret[0];
}

static inline uint64_t nat0(OrbxNativeContext *oc, uint32_t id) {
    return nat_invoke(oc, id, NULL, 0, NULL);
}

static inline uint64_t nat1(OrbxNativeContext *oc, uint32_t id, uint64_t a1) {
    uint64_t a[1] = { a1 };
    return nat_invoke(oc, id, a, 1, NULL);
}

static inline uint64_t nat2(OrbxNativeContext *oc, uint32_t id, uint64_t a1, uint64_t a2) {
    uint64_t a[2] = { a1, a2 };
    return nat_invoke(oc, id, a, 2, NULL);
}

static inline uint64_t nat3(OrbxNativeContext *oc, uint32_t id,
                            uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t a[3] = { a1, a2, a3 };
    return nat_invoke(oc, id, a, 3, NULL);
}

static inline uint64_t nat4(OrbxNativeContext *oc, uint32_t id,
                            uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    uint64_t a[4] = { a1, a2, a3, a4 };
    return nat_invoke(oc, id, a, 4, NULL);
}

static inline uint64_t nat9(OrbxNativeContext *oc, uint32_t id,
                            uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                            uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9) {
    uint64_t a[9] = { a1, a2, a3, a4, a5, a6, a7, a8, a9 };
    return nat_invoke(oc, id, a, 9, NULL);
}

static inline void set_toast(OrbxNativeContext *oc, const char *msg) {
    copy_cstr(oc->menu.toast_msg, sizeof(oc->menu.toast_msg), msg);
    oc->menu.toast_ticks = 180u; /* ~3s at 60 FPS */
}

static inline void play_ui_sound(OrbxNativeContext *oc, const char *name) {
    if (!oc || !oc->menu.sound_enabled) return;
    nat4(oc, ORBX_NAT_PLAY_SOUND_FRONTEND, (uint64_t)(int64_t)-1,
         ptr2u(name), ptr2u("HUD_FRONTEND_DEFAULT_SOUNDSET"), 0);
}

__attribute__((noinline))
static void ui_draw_rect(OrbxNativeContext *oc, float x, float y, float w, float h,
                         int r, int g, int b, int a) {
    uint64_t args[9] = {
        f2u(x), f2u(y), f2u(w), f2u(h),
        (uint64_t)r, (uint64_t)g, (uint64_t)b, (uint64_t)a, 0
    };
    nat_invoke(oc, ORBX_NAT_DRAW_RECT, args, 9, NULL);
}

static inline void request_txd(OrbxNativeContext *oc, const char *txd) {
    if (!oc || !oc->menu.natives[ORBX_NAT_REQUEST_STREAMED_TEXTURE_DICT]) return;
    nat2(oc, ORBX_NAT_REQUEST_STREAMED_TEXTURE_DICT, ptr2u(txd), 0);
}

static inline int has_txd_loaded(OrbxNativeContext *oc, const char *txd) {
    if (!oc || !oc->menu.natives[ORBX_NAT_HAS_STREAMED_TEXTURE_DICT_LOADED]) return 0;
    return nat1(oc, ORBX_NAT_HAS_STREAMED_TEXTURE_DICT_LOADED, ptr2u(txd)) ? 1 : 0;
}

__attribute__((noinline))
static void ui_draw_sprite(OrbxNativeContext *oc, const char *txd, const char *txn,
                           float x, float y, float w, float h, float heading,
                           int r, int g, int b, int a) {
    if (!oc || !oc->menu.natives[ORBX_NAT_DRAW_SPRITE]) return;
    uint64_t args[12] = {
        ptr2u(txd), ptr2u(txn),
        f2u(x), f2u(y), f2u(w), f2u(h), f2u(heading),
        (uint64_t)r, (uint64_t)g, (uint64_t)b, (uint64_t)a, 0
    };
    nat_invoke(oc, ORBX_NAT_DRAW_SPRITE, args, 12, NULL);
}

__attribute__((noinline))
static void ui_draw_text(OrbxNativeContext *oc, const char *txt,
                         float x, float y, float scale, int font,
                         int align, float wrap_end,
                         int r, int g, int b, int a) {
    nat1(oc, ORBX_NAT_SET_TEXT_FONT, (uint64_t)font);
    nat2(oc, ORBX_NAT_SET_TEXT_SCALE, f2u(scale), f2u(scale));
    nat4(oc, ORBX_NAT_SET_TEXT_COLOUR, (uint64_t)r, (uint64_t)g, (uint64_t)b, (uint64_t)a);
    nat1(oc, ORBX_NAT_SET_TEXT_CENTRE, (align == 1) ? 1u : 0u);
    nat1(oc, ORBX_NAT_SET_TEXT_RIGHT_JUSTIFY, (align == 2) ? 1u : 0u);
    if (align == 2) {
        nat2(oc, ORBX_NAT_SET_TEXT_WRAP, f2u(0.0f), f2u(wrap_end));
    } else if (align == 0 && wrap_end > x && wrap_end < 0.999f) {
        nat2(oc, ORBX_NAT_SET_TEXT_WRAP, f2u(x), f2u(wrap_end));
    } else {
        nat2(oc, ORBX_NAT_SET_TEXT_WRAP, f2u(0.0f), f2u(1.0f));
    }
    nat1(oc, ORBX_NAT_BEGIN_TEXT_COMMAND_DISPLAY_TEXT, ptr2u("STRING"));
    nat1(oc, ORBX_NAT_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER, ptr2u(txt));
    nat3(oc, ORBX_NAT_END_TEXT_COMMAND_DISPLAY_TEXT, f2u(x), f2u(y), 0);
}

/* ============================================================================
 * Freestanding Math, Keybind Formatting & Navigation Stack Helpers
 * ============================================================================ */

static float pic_sinf(float x) {
    const float PI     = 3.14159265f;
    const float TWO_PI = 6.28318530f;
    while (x >  PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    if (x >  PI * 0.5f) x =  PI - x;
    if (x < -PI * 0.5f) x = -PI - x;
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}

static float pic_cosf(float x) {
    return pic_sinf(x + 1.57079632f);
}

static size_t fmt_single_btn(char *dst, size_t pos, size_t cap, uint32_t bit) {
    static const uint32_t kBtnBits[16] = {
        SCE_PAD_BTN_CROSS,
        SCE_PAD_BTN_CIRCLE,
        SCE_PAD_BTN_SQUARE,
        SCE_PAD_BTN_TRIANGLE,
        SCE_PAD_BTN_L1,
        SCE_PAD_BTN_R1,
        SCE_PAD_BTN_L2,
        SCE_PAD_BTN_R2,
        SCE_PAD_BTN_L3,
        SCE_PAD_BTN_R3,
        SCE_PAD_BTN_UP,
        SCE_PAD_BTN_DOWN,
        SCE_PAD_BTN_LEFT,
        SCE_PAD_BTN_RIGHT,
        SCE_PAD_BTN_TOUCH_PAD,
        SCE_PAD_BTN_OPTIONS
    };
    static const char kBtnNames[17][10] = {
        "Cross", "Circle", "Square", "Triangle",
        "L1", "R1", "L2", "R2", "L3", "R3",
        "D-Up", "D-Down", "D-Left", "D-Right",
        "Touchpad", "Options", "Btn"
    };
    int idx = 16;
    for (size_t i = 0; i < 16u; i++) {
        if (kBtnBits[i] == bit) {
            idx = (int)i;
            break;
        }
    }
    __asm__ volatile("" : "+r"(idx));
    return orbx_pic_fmt_str(dst, pos, cap, kBtnNames[idx]);
}

static void fmt_combo_btn(char *dst, size_t cap, uint32_t mod, uint32_t btn) {
    if (!cap) return;
    if (btn == 0u) {
        copy_cstr(dst, cap, "UNBOUND");
        return;
    }
    size_t p = 0;
    if (mod != 0u && mod != btn) {
        p = fmt_single_btn(dst, p, cap - 1, mod);
        p = orbx_pic_fmt_str(dst, p, cap - 1, "+");
    }
    p = fmt_single_btn(dst, p, cap - 1, btn);
    dst[p] = '\0';
}

static void recompute_suppress_mask(OrbxNativeContext *oc) {
    uint32_t m = SCE_PAD_BTN_UP | SCE_PAD_BTN_DOWN | SCE_PAD_BTN_LEFT | SCE_PAD_BTN_RIGHT;
    m |= oc->menu.bind_accept | oc->menu.bind_cancel;
    m |= oc->menu.bind_tab_prev | oc->menu.bind_tab_next;
    m |= oc->menu.bind_open_mod | oc->menu.bind_open_btn;
    if (oc->menu.keep_steering) {
        m &= ~(SCE_PAD_BTN_L2 | SCE_PAD_BTN_R2);
    }
    m &= 0x0010FFF6u;
    oc->menu.pad_suppress_mask = m;
}

static void reset_default_keybinds(OrbxNativeContext *oc) {
    oc->menu.bind_open_mod     = SCE_PAD_BTN_R1;
    oc->menu.bind_open_btn     = SCE_PAD_BTN_DOWN;
    oc->menu.bind_accept       = SCE_PAD_BTN_CROSS;
    oc->menu.bind_cancel       = SCE_PAD_BTN_CIRCLE;
    oc->menu.bind_tab_prev     = SCE_PAD_BTN_L1;
    oc->menu.bind_tab_next     = SCE_PAD_BTN_R1;
    oc->menu.bind_horn_boost   = 0u;
    oc->menu.bind_noclip       = 0u;
    oc->menu.bind_quick_repair = 0u;
    oc->menu.bind_quick_heal   = 0u;
    oc->menu.bind_super_brake  = 0u;
    recompute_suppress_mask(oc);
}

static void push_submenu(OrbxNativeContext *oc, uint32_t target_sub) {
    if (target_sub >= ORBX_SUB_MAX) return;
    if (oc->menu.submenu == target_sub) return;
    uint32_t d = oc->menu.sub_depth;
    if (d < 8u) {
        oc->menu.sub_stack[d] = oc->menu.submenu;
        oc->menu.cur_stack[d] = oc->menu.cursor[oc->menu.submenu];
        oc->menu.sub_depth    = d + 1u;
    }
    oc->menu.submenu = target_sub;
}

static void pop_submenu(OrbxNativeContext *oc) {
    if (oc->menu.sub_depth > 0u) {
        uint32_t d = oc->menu.sub_depth - 1u;
        oc->menu.sub_depth = d;
        uint32_t prev_sub  = oc->menu.sub_stack[d];
        if (prev_sub >= ORBX_SUB_MAX) prev_sub = ORBX_SUB_MAIN;
        oc->menu.submenu = prev_sub;
        oc->menu.cursor[prev_sub] = oc->menu.cur_stack[d];
        return;
    }
    if (oc->menu.submenu == ORBX_SUB_MAIN) {
        oc->menu.menu_open = 0u;
        return;
    }
    /* Direct top-tab fallback hierarchy */
    uint32_t cur_tab = oc->menu.submenu;
    int is_top = 0;
    for (size_t i = 0; i < 8u; i++) {
        if (kTopTabs[i].sub_id == cur_tab) { is_top = 1; break; }
    }
    if (is_top) {
        oc->menu.menu_open = 0u;
    } else if (cur_tab == ORBX_SUB_ADDON_SPAWN || cur_tab == ORBX_SUB_SPAWN_CAT) {
        oc->menu.submenu = ORBX_SUB_SPAWN;
    } else if (cur_tab == ORBX_SUB_VEH_COLORS || cur_tab == ORBX_SUB_VEH_CUSTOM ||
               cur_tab == ORBX_SUB_VEH_LIGHTS || cur_tab == ORBX_SUB_VEH_DOORS) {
        oc->menu.submenu = ORBX_SUB_VEHICLE;
    } else if (cur_tab == ORBX_SUB_CLOUDS || cur_tab == ORBX_SUB_TELEPORT || cur_tab == ORBX_SUB_IPL_INTERIORS) {
        oc->menu.submenu = ORBX_SUB_WORLD;
    } else if (cur_tab == ORBX_SUB_GRAPHICS) {
        oc->menu.submenu = ORBX_SUB_CAMERA;
    } else if (cur_tab == ORBX_SUB_KEYBINDS) {
        oc->menu.submenu = ORBX_SUB_SETTINGS;
    } else {
        oc->menu.submenu = ORBX_SUB_MAIN;
    }
}

static int find_active_tab_idx(uint32_t sub) {
    if (sub == ORBX_SUB_PLAYER) return 1;
    if (sub == ORBX_SUB_WEAPONS) return 2;
    if (sub == ORBX_SUB_VEHICLE || sub == ORBX_SUB_VEH_CUSTOM ||
        sub == ORBX_SUB_VEH_COLORS || sub == ORBX_SUB_VEH_LIGHTS || sub == ORBX_SUB_VEH_DOORS) return 3;
    if (sub == ORBX_SUB_SPAWN || sub == ORBX_SUB_ADDON_SPAWN || sub == ORBX_SUB_SPAWN_CAT) return 4;
    if (sub == ORBX_SUB_WORLD || sub == ORBX_SUB_CLOUDS ||
        sub == ORBX_SUB_TELEPORT || sub == ORBX_SUB_IPL_INTERIORS) return 5;
    if (sub == ORBX_SUB_CAMERA || sub == ORBX_SUB_GRAPHICS) return 6;
    if (sub == ORBX_SUB_SETTINGS || sub == ORBX_SUB_KEYBINDS) return 7;
    return 0;
}

static void jump_top_tab(OrbxNativeContext *oc, int dir) {
    int cur_t = find_active_tab_idx(oc->menu.submenu);
    int nxt_t = (cur_t + (dir < 0 ? 7 : 1)) % 8;
    oc->menu.sub_depth = 0u;
    oc->menu.submenu   = kTopTabs[nxt_t].sub_id;
}

/* ============================================================================
 * Feature Actions & Continuous Per-Frame Mods
 * ============================================================================ */

static void sanitize_menu_toggles(OrbxNativeContext *oc) {
    if (!oc) return;
    if (oc->menu.invisible > 1u)        oc->menu.invisible        = 0u;
    if (oc->menu.godmode > 1u)          oc->menu.godmode          = 0u;
    if (oc->menu.never_wanted > 1u)     oc->menu.never_wanted     = 0u;
    if (oc->menu.super_jump > 1u)       oc->menu.super_jump       = 0u;
    if (oc->menu.fast_run > 1u)         oc->menu.fast_run         = 0u;
    if (oc->menu.no_ragdoll > 1u)       oc->menu.no_ragdoll       = 0u;
    if (oc->menu.noclip_on > 1u)        oc->menu.noclip_on        = 0u;
    if (oc->menu.spec_ability_inf > 1u) oc->menu.spec_ability_inf = 0u;
    if (oc->menu.inf_ammo > 1u)         oc->menu.inf_ammo         = 0u;
    if (oc->menu.explosive_ammo > 1u)   oc->menu.explosive_ammo   = 0u;
    if (oc->menu.fire_ammo > 1u)        oc->menu.fire_ammo        = 0u;
    if (oc->menu.explosive_melee > 1u)  oc->menu.explosive_melee  = 0u;
    if (oc->menu.veh_god > 1u)          oc->menu.veh_god          = 0u;
    if (oc->menu.seatbelt_on > 1u)      oc->menu.seatbelt_on      = 0u;
    if (oc->menu.horn_boost > 1u)       oc->menu.horn_boost       = 0u;
    if (oc->menu.veh_drift_mode > 1u)   oc->menu.veh_drift_mode   = 0u;
    if (oc->menu.clock_paused > 1u)     oc->menu.clock_paused     = 0u;
    if (oc->menu.low_gravity > 1u)      oc->menu.low_gravity      = 0u;
    if (oc->menu.mobile_radio > 1u)     oc->menu.mobile_radio     = 0u;
    if (oc->menu.calmer_police_on > 1u) oc->menu.calmer_police_on = 0u;
    if (oc->menu.gta6_hud_on > 1u)      oc->menu.gta6_hud_on      = 0u;
    if (oc->menu.gta6_wheel_on > 1u)    oc->menu.gta6_wheel_on    = 0u;
    if (oc->menu.fp_lock > 1u)          oc->menu.fp_lock          = 0u;
    if (oc->menu.weather_persist > 1u)  oc->menu.weather_persist  = 0u;
    if (oc->menu.veh_doors_locked > 1u) oc->menu.veh_doors_locked = 0u;
    if (oc->menu.veh_turbo_on > 1u)     oc->menu.veh_turbo_on     = 0u;
    if (oc->menu.veh_xenon_on > 1u)     oc->menu.veh_xenon_on     = 0u;
    if (oc->menu.veh_neon_on > 1u)      oc->menu.veh_neon_on      = 0u;
}

static void ensure_menu_initialized(OrbxNativeContext *oc) {
    if (!oc->menu.initialized) {
        oc->menu.initialized = 1u;
        if (!oc->menu.density_pct) oc->menu.density_pct = 100u;
        oc->menu.last_density_pct = oc->menu.density_pct;
        oc->menu.last_want_cap_applied = 5u;
        oc->menu.lock_hour = -1;
        oc->menu.weather_idx = 0;
        oc->menu.cloud_hat_idx = -1;
        oc->menu.cloud_keyframe_idx = -1;
        if (!oc->menu.cloud_alpha_pct) oc->menu.cloud_alpha_pct = 100u;
        if (oc->menu.menu_scale > 2u) oc->menu.menu_scale = 1u;
        oc->menu.veh_neon_front = 1u;
        oc->menu.veh_neon_back = 1u;
        oc->menu.veh_neon_left = 1u;
        oc->menu.veh_neon_right = 1u;
        if (!oc->menu.gfx_strength_pct) oc->menu.gfx_strength_pct = 100u;
        copy_cstr(oc->menu.gfx_custom_name, sizeof(oc->menu.gfx_custom_name), "AmbientPUSH");
        copy_cstr(oc->menu.probe_last_arg, sizeof(oc->menu.probe_last_arg), "NONE");
        if (oc->menu.fuel_level <= 0.0f) oc->menu.fuel_level = 100.0f;
        oc->menu.gta6_hud_on   = 0u;
        oc->menu.gta6_wheel_on = 0u;
        oc->menu.fuel_system_on = 0u;

        /* Strictly ensure all cheats and continuous mods start OFF on game launch */
        oc->menu.godmode          = 0u;
        oc->menu.never_wanted     = 0u;
        oc->menu.explosive_ammo   = 0u;
        oc->menu.fire_ammo        = 0u;
        oc->menu.explosive_melee  = 0u;
        oc->menu.super_jump       = 0u;
        oc->menu.fast_run         = 0u;
        oc->menu.inf_ammo         = 0u;
        oc->menu.invisible        = 0u;
        oc->menu.no_ragdoll       = 0u;
        oc->menu.spec_ability_inf = 0u;
        oc->menu.veh_god          = 0u;
        oc->menu.seatbelt_on      = 0u;
        oc->menu.horn_boost       = 0u;
        oc->menu.veh_drift_mode   = 0u;
        oc->menu.veh_torque_idx   = 0u;
        oc->menu.clock_paused     = 0u;
        oc->menu.low_gravity      = 0u;
        oc->menu.mobile_radio     = 0u;
        oc->menu.calmer_police_on = 0u;
        oc->menu.noclip_on        = 0u;
        oc->menu.bind_horn_boost  = 0u;
        oc->menu.fp_lock          = 0u;
        oc->menu.afp_mode         = 0u;
        oc->menu.applied_flags    = 0u;

        /* Only seed default keybinds if host INI didn't already populate them */
        if (!oc->menu.bind_open_btn)  oc->menu.bind_open_mod = SCE_PAD_BTN_R1, oc->menu.bind_open_btn = SCE_PAD_BTN_DOWN;
        if (!oc->menu.bind_accept)    oc->menu.bind_accept   = SCE_PAD_BTN_CROSS;
        if (!oc->menu.bind_cancel)    oc->menu.bind_cancel   = SCE_PAD_BTN_CIRCLE;
        if (!oc->menu.bind_tab_prev)  oc->menu.bind_tab_prev = SCE_PAD_BTN_L1;
        if (!oc->menu.bind_tab_next)  oc->menu.bind_tab_next = SCE_PAD_BTN_R1;
        sanitize_live_keybinds(oc);
    }
}

/* ============================================================================
 * ENB & ReShade Live Graphics Enhancer (Stage 1 AA/LOD + Stage 2 Shadows/AO/FX)
 * ============================================================================ */

static void sync_enb_graphics_pipeline(OrbxNativeContext *oc) {
    uint32_t aa_lvl  = oc->menu.enb_extra_active; /* Stage 1: Anti-Aliasing (Smooth) 0..5 */
    uint32_t dof_lvl = oc->menu.enb_dof_lvl;      /* Stage 1: NVE Depth of Field 0..5 */
    uint32_t mb_lvl  = oc->menu.enb_motion_blur;  /* Stage 1: NVE Motion Blur 0..5 */
    uint32_t sh_lvl  = oc->menu.enb_shadow_soft;  /* Stage 2: Dynamic Shadow Cascades (Pure Native) 0..5 */
    uint32_t dk_lvl  = oc->menu.enb_ssao_boost;   /* Stage 2: NVE HBAO / Contact AO 0..5 */
    uint32_t rf_lvl  = oc->menu.enb_refl_lvl;     /* Stage 2: NVE Mirror Reflections 0..5 */
    uint32_t bl_lvl  = oc->menu.enb_bloom_lvl;    /* Stage 2: NVE HDR Bloom & Glow 0..5 */
    uint32_t base_i  = (oc->menu.gfx_preset_idx < 15u) ? oc->menu.gfx_preset_idx : 0u;
    uint32_t base_pct = oc->menu.gfx_strength_pct;
    if (base_pct < 10u || base_pct > 200u) base_pct = 100u;

    /* 1. Pure Native Cascade Shadow Sharpness & Softness (NEVER overwrites NVE timecycle shadows/lighting!) */
    if (oc->menu.main_ticks > 180u) {
        if (sh_lvl > 0u) {
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_SHADOW_SAMPLE_TYPE]) {
                const char *stype = "CSM_ST_BOX4x4";
                if (sh_lvl == 2u)      stype = "CSM_ST_HIGHRES_BOX4x4";
                else if (sh_lvl == 3u) stype = "CSM_ST_SOFT16";
                else if (sh_lvl >= 4u) stype = "CSM_ST_POISSON16_RPDB_GNORM";
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_SHADOW_SAMPLE_TYPE, ptr2u(stype));
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE]) {
                /* Tighter cascade bounds = higher shadow map texel density near player/vehicles */
                float bscale = 1.0f - (float)sh_lvl * 0.12f;
                if (bscale < 0.40f) bscale = 0.40f;
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE, f2u(bscale));
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER, 1u);
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE, 0u);
            }
            if (oc->menu.natives[ORBX_NAT_SET_BACKFACECULLING]) {
                nat1(oc, ORBX_NAT_SET_BACKFACECULLING, 1u);
            }
            oc->menu.applied_flags |= APP_GFX_SHADOWS;
        } else if (oc->menu.applied_flags & APP_GFX_SHADOWS) {
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE]) {
                nat0(oc, ORBX_NAT_CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE);
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE, f2u(1.0f));
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER, 1u);
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE, 0u);
            }
            if (oc->menu.natives[ORBX_NAT_SET_BACKFACECULLING]) {
                nat1(oc, ORBX_NAT_SET_BACKFACECULLING, 1u);
            }
            oc->menu.applied_flags &= ~APP_GFX_SHADOWS;
        }
    }

    /* 2. Stage 1 Anti-Aliasing (Film Grain / Dither Suppression) & Bokeh DOF */
    if (aa_lvl > 0u) {
        oc->menu.gfx_film_grain = 0u;
        if (oc->menu.natives[ORBX_NAT_SET_NOISINESSOVERIDE]) {
            nat1(oc, ORBX_NAT_SET_NOISINESSOVERIDE, f2u(0.0f));
        }
        if (oc->menu.natives[ORBX_NAT_SET_NOISEOVERIDE]) {
            nat1(oc, ORBX_NAT_SET_NOISEOVERIDE, 0u);
        }
        oc->menu.applied_flags &= ~APP_GFX_GRAIN;
        oc->shader_probe.anti_aliasing = (float)aa_lvl * 0.20f;
        oc->shader_probe.film_grain    = 0.0f;
    }
    if (dof_lvl == 0u && oc->menu.natives[ORBX_NAT_SET_HIDOF_OVERRIDE]) {
        uint64_t da[6] = { 0u, 0u, f2u(0.0f), f2u(0.0f), f2u(0.0f), f2u(0.0f) };
        nat_invoke(oc, ORBX_NAT_SET_HIDOF_OVERRIDE, da, 6, NULL);
    }

    /* 3. Non-Destructive Stackable FX Modifiers (Motion Blur, HBAO, Reflections, Bloom):
     * Each MODHEN_* modifier in timecycle_mods_1.xml contains ONLY its isolated effect tags
     * (zero light_dir*, light_natural*, postfx_tonemap_filmic_*, or dir_shadow_* tags)
     * so your NVE Classic shadows and weather lighting are NEVER overwritten! */
    const char *s2_mods[3];
    uint32_t s2_lvls[3];
    uint32_t s2_cnt = 0u;

    if (dk_lvl > 0u) { s2_mods[s2_cnt] = "MODHEN_ENB_DARK_AO"; s2_lvls[s2_cnt] = dk_lvl; s2_cnt++; }
    if (rf_lvl > 0u) { s2_mods[s2_cnt] = "MODHEN_MIRROR_REFL"; s2_lvls[s2_cnt] = rf_lvl; s2_cnt++; }
    if (bl_lvl > 0u) { s2_mods[s2_cnt] = "MODHEN_ENB_BLOOM";   s2_lvls[s2_cnt] = bl_lvl; s2_cnt++; }

    uint32_t total_cnt = s2_cnt + (mb_lvl > 0u ? 1u : 0u);
    if (total_cnt > 0u) {
        char prim_mod[32];
        char extra_mod[32];
        int has_extra = 0;
        uint32_t max_l = mb_lvl;

        for (uint32_t i = 0u; i < s2_cnt; i++) {
            if (s2_lvls[i] > max_l) max_l = s2_lvls[i];
        }

        if (base_i > 0u) {
            copy_cstr(extra_mod, sizeof(extra_mod), kGfxPresets[base_i].mod_name);
            has_extra = 1;
            if (s2_cnt == 0u) {
                copy_cstr(prim_mod, sizeof(prim_mod), "MODHEN_NVE_MOTIONBLUR");
            } else if (s2_cnt == 1u) {
                copy_cstr(prim_mod, sizeof(prim_mod), s2_mods[0]);
            } else {
                copy_cstr(prim_mod, sizeof(prim_mod), "MODHEN_ENB_ALL");
            }
        } else if (mb_lvl > 0u && s2_cnt == 0u) {
            copy_cstr(prim_mod, sizeof(prim_mod), "MODHEN_NVE_MOTIONBLUR");
        } else if (mb_lvl > 0u && s2_cnt == 1u) {
            copy_cstr(extra_mod, sizeof(extra_mod), "MODHEN_NVE_MOTIONBLUR");
            copy_cstr(prim_mod, sizeof(prim_mod), s2_mods[0]);
            has_extra = 1;
        } else if (mb_lvl > 0u && s2_cnt >= 2u) {
            copy_cstr(extra_mod, sizeof(extra_mod), "MODHEN_NVE_MOTIONBLUR");
            copy_cstr(prim_mod, sizeof(prim_mod), "MODHEN_ENB_ALL");
            has_extra = 1;
        } else if (s2_cnt == 1u) {
            copy_cstr(prim_mod, sizeof(prim_mod), s2_mods[0]);
        } else if (s2_cnt == 2u) {
            copy_cstr(extra_mod, sizeof(extra_mod), s2_mods[0]);
            copy_cstr(prim_mod, sizeof(prim_mod), s2_mods[1]);
            has_extra = 1;
        } else {
            copy_cstr(prim_mod, sizeof(prim_mod), "MODHEN_ENB_ALL");
        }

        if (has_extra) {
            if (oc->menu.natives[ORBX_NAT_SET_EXTRA_TIMECYCLE_MODIFIER]) {
                nat1(oc, ORBX_NAT_SET_EXTRA_TIMECYCLE_MODIFIER, ptr2u(extra_mod));
            }
            oc->menu.applied_flags |= APP_GFX_EXTRA_MOD;
        } else {
            if (oc->menu.natives[ORBX_NAT_CLEAR_EXTRA_TIMECYCLE_MODIFIER]) {
                nat0(oc, ORBX_NAT_CLEAR_EXTRA_TIMECYCLE_MODIFIER);
            }
            oc->menu.applied_flags &= ~APP_GFX_EXTRA_MOD;
        }

        float s2_str = 0.35f + (float)max_l * 0.13f;
        s2_str *= ((float)base_pct * 0.01f);
        if (s2_str < 0.35f) s2_str = 0.35f;
        if (s2_str > 1.00f) s2_str = 1.00f;

        if (oc->menu.natives[ORBX_NAT_SET_TIMECYCLE_MODIFIER]) {
            copy_cstr(oc->menu.probe_last_arg, sizeof(oc->menu.probe_last_arg), prim_mod);
            nat1(oc, ORBX_NAT_SET_TIMECYCLE_MODIFIER, ptr2u(prim_mod));
        }
        if (oc->menu.natives[ORBX_NAT_SET_TIMECYCLE_MODIFIER_STRENGTH]) {
            nat1(oc, ORBX_NAT_SET_TIMECYCLE_MODIFIER_STRENGTH, f2u(s2_str));
        }
        oc->menu.applied_flags |= APP_GFX_MOD;

        oc->depth_probe.ssao_intensity   = 4.0f + (float)dk_lvl * 1.2f;
        oc->depth_probe.ssao_cp_strength = 0.5f + (float)dk_lvl * 0.20f;
        oc->depth_probe.ssr_intensity    = 1.0f + (float)rf_lvl * 0.50f;
        oc->shader_probe.bloom_intensity = 0.35f + (float)bl_lvl * 0.20f;
    } else {
        if (oc->menu.natives[ORBX_NAT_CLEAR_EXTRA_TIMECYCLE_MODIFIER]) {
            nat0(oc, ORBX_NAT_CLEAR_EXTRA_TIMECYCLE_MODIFIER);
        }
        oc->menu.applied_flags &= ~APP_GFX_EXTRA_MOD;

        if (base_i > 0u) {
            char bmod[32];
            copy_cstr(bmod, sizeof(bmod), kGfxPresets[base_i].mod_name);
            if (oc->menu.natives[ORBX_NAT_SET_TIMECYCLE_MODIFIER]) {
                copy_cstr(oc->menu.probe_last_arg, sizeof(oc->menu.probe_last_arg), bmod);
                nat1(oc, ORBX_NAT_SET_TIMECYCLE_MODIFIER, ptr2u(bmod));
            }
            if (oc->menu.natives[ORBX_NAT_SET_TIMECYCLE_MODIFIER_STRENGTH]) {
                nat1(oc, ORBX_NAT_SET_TIMECYCLE_MODIFIER_STRENGTH, f2u((float)base_pct * 0.01f));
            }
            oc->menu.applied_flags |= APP_GFX_MOD;
        } else {
            if (oc->menu.natives[ORBX_NAT_CLEAR_TIMECYCLE_MODIFIER]) {
                nat0(oc, ORBX_NAT_CLEAR_TIMECYCLE_MODIFIER);
            }
            oc->menu.applied_flags &= ~APP_GFX_MOD;
        }
    }
}

static void apply_gfx_preset_with_strength(OrbxNativeContext *oc, uint32_t idx, uint32_t custom_pct) {
    if (idx >= 15u) idx = 0u;
    oc->menu.gfx_preset_idx = idx;

    if (idx == 0u) {
        sync_enb_graphics_pipeline(oc);
        set_toast(oc, (oc->menu.enb_shadow_soft || oc->menu.enb_ssao_boost || oc->menu.enb_bloom_lvl)
                      ? "Base Preset: Vanilla (+Stage 2 ENB)"
                      : "Original Graphics Restored");
        return;
    }

    uint32_t pct = custom_pct;
    if (pct < 10u || pct > 200u) {
        pct = (uint32_t)kGfxPresets[idx].default_pct;
        if (pct < 10u || pct > 200u) pct = 100u;
    }
    oc->menu.gfx_strength_pct = pct;
    sync_enb_graphics_pipeline(oc);

    char msg[52];
    size_t p = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, "Visuals: ");
    p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, kGfxPresets[idx].label);
    msg[p] = '\0';
    set_toast(oc, msg);
}

static void apply_gfx_preset(OrbxNativeContext *oc, uint32_t idx) {
    apply_gfx_preset_with_strength(oc, idx, 0);
}

static void apply_gfx_strength(OrbxNativeContext *oc, uint32_t pct) {
    if (pct < 10u)  pct = 10u;
    if (pct > 200u) pct = 200u;
    oc->menu.gfx_strength_pct = pct;
    sync_enb_graphics_pipeline(oc);
}

static void apply_gfx_grain(OrbxNativeContext *oc, uint32_t fg) {
    if (fg > 100u) fg = 0u;
    oc->menu.gfx_film_grain = fg;
    if (fg > 0u) {
        if (oc->menu.natives[ORBX_NAT_SET_NOISEOVERIDE]) {
            nat1(oc, ORBX_NAT_SET_NOISEOVERIDE, 1);
        }
        if (oc->menu.natives[ORBX_NAT_SET_NOISINESSOVERIDE]) {
            nat1(oc, ORBX_NAT_SET_NOISINESSOVERIDE, f2u((float)fg * 0.01f));
        }
        oc->menu.applied_flags |= APP_GFX_GRAIN;
    } else {
        if (oc->menu.natives[ORBX_NAT_SET_NOISINESSOVERIDE]) {
            nat1(oc, ORBX_NAT_SET_NOISINESSOVERIDE, f2u(0.0f));
        }
        if (oc->menu.natives[ORBX_NAT_SET_NOISEOVERIDE]) {
            nat1(oc, ORBX_NAT_SET_NOISEOVERIDE, 0);
        }
        oc->menu.applied_flags &= ~APP_GFX_GRAIN;
    }
}

static void apply_gfx_postfx(OrbxNativeContext *oc, uint32_t idx) {
    if (idx >= 7u) idx = 0u;
    oc->menu.gfx_postfx_idx = idx;

    if (oc->menu.natives[ORBX_NAT_ANIMPOSTFX_STOP_ALL]) {
        nat0(oc, ORBX_NAT_ANIMPOSTFX_STOP_ALL);
    }
    if (idx > 0u && oc->menu.natives[ORBX_NAT_ANIMPOSTFX_PLAY]) {
        char local_fx[32];
        copy_cstr(local_fx, sizeof(local_fx), kPostFxPresets[idx].fx_name);
        copy_cstr(oc->menu.probe_last_arg, sizeof(oc->menu.probe_last_arg), local_fx);
        nat3(oc, ORBX_NAT_ANIMPOSTFX_PLAY, ptr2u(local_fx), 0, 1);
    }
    set_toast(oc, kPostFxPresets[idx].label);
}

static void reset_all_gfx(OrbxNativeContext *oc) {
    oc->menu.gfx_preset_idx = 0u;
    oc->menu.gfx_strength_pct = 100u;
    oc->menu.gfx_film_grain = 0u;
    oc->menu.gfx_postfx_idx = 0u;
    oc->menu.enb_extra_active = 0u;
    oc->menu.enb_shadow_soft  = 0u;
    oc->menu.enb_shadow_scale = 0u;
    oc->menu.enb_ssao_boost   = 0u;
    oc->menu.enb_bloom_lvl    = 0u;
    oc->menu.enb_refl_lvl     = 0u;
    oc->menu.enb_dof_lvl      = 0u;
    oc->menu.enb_motion_blur  = 0u;
    copy_cstr(oc->menu.probe_last_arg, sizeof(oc->menu.probe_last_arg), "RESET_ALL_GFX");

    if (oc->menu.natives[ORBX_NAT_SET_BACKFACECULLING]) {
        nat1(oc, ORBX_NAT_SET_BACKFACECULLING, 1u);
    }
    if (oc->menu.natives[ORBX_NAT_SET_HIDOF_OVERRIDE]) {
        uint64_t da[6] = { 0u, 0u, f2u(0.0f), f2u(0.0f), f2u(0.0f), f2u(0.0f) };
        nat_invoke(oc, ORBX_NAT_SET_HIDOF_OVERRIDE, da, 6, NULL);
    }

    if (oc->menu.natives[ORBX_NAT_CLEAR_TIMECYCLE_MODIFIER]) {
        nat0(oc, ORBX_NAT_CLEAR_TIMECYCLE_MODIFIER);
    }
    if (oc->menu.natives[ORBX_NAT_CLEAR_EXTRA_TIMECYCLE_MODIFIER]) {
        nat0(oc, ORBX_NAT_CLEAR_EXTRA_TIMECYCLE_MODIFIER);
    }
    if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE]) {
        nat0(oc, ORBX_NAT_CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE);
    }
    if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE]) {
        nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE, f2u(1.0f));
    }
    if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE]) {
        nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE, 0u);
    }
    if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER]) {
        nat1(oc, ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER, 1u);
    }
    if (oc->menu.natives[ORBX_NAT_CLEAR_HD_AREA]) {
        nat0(oc, ORBX_NAT_CLEAR_HD_AREA);
    }
    if (oc->menu.natives[ORBX_NAT_ANIMPOSTFX_STOP_ALL]) {
        nat0(oc, ORBX_NAT_ANIMPOSTFX_STOP_ALL);
    }
    if (oc->menu.natives[ORBX_NAT_SET_NOISINESSOVERIDE]) {
        nat1(oc, ORBX_NAT_SET_NOISINESSOVERIDE, f2u(0.0f));
    }
    if (oc->menu.natives[ORBX_NAT_SET_NOISEOVERIDE]) {
        nat1(oc, ORBX_NAT_SET_NOISEOVERIDE, 0);
    }
    oc->menu.applied_flags &= ~(APP_GFX_MOD | APP_GFX_GRAIN | APP_GFX_EXTRA_MOD | APP_GFX_SHADOWS);
    set_toast(oc, "All Graphics Filters Reset to Default");
}

static void stop_afp_cam(OrbxNativeContext *oc) {
    if (oc->menu.afp_cam_active && oc->menu.natives[ORBX_NAT_RENDER_SCRIPT_CAMS]) {
        uint64_t rargs[5] = { 0u, 0u, 0u, 1u, 0u };
        nat_invoke(oc, ORBX_NAT_RENDER_SCRIPT_CAMS, rargs, 5, NULL);
    }
    if (oc->menu.afp_cam_handle && oc->menu.natives[ORBX_NAT_DOES_CAM_EXIST] &&
        oc->menu.natives[ORBX_NAT_DESTROY_CAM]) {
        if (nat1(oc, ORBX_NAT_DOES_CAM_EXIST, (uint64_t)oc->menu.afp_cam_handle)) {
            if (oc->menu.afp_attached_entity && oc->menu.natives[ORBX_NAT_DETACH_CAM]) {
                nat1(oc, ORBX_NAT_DETACH_CAM, (uint64_t)oc->menu.afp_cam_handle);
            }
            if (oc->menu.natives[ORBX_NAT_SET_CAM_ACTIVE]) {
                nat2(oc, ORBX_NAT_SET_CAM_ACTIVE, (uint64_t)oc->menu.afp_cam_handle, 0u);
            }
            nat2(oc, ORBX_NAT_DESTROY_CAM, (uint64_t)oc->menu.afp_cam_handle, 0u);
        }
    }
    oc->menu.afp_cam_handle      = 0;
    oc->menu.afp_cam_active      = 0u;
    oc->menu.afp_attached_entity = 0;
    oc->menu.afp_attached_mode   = 0u;
    oc->menu.afp_attached_bone   = -1;
    oc->menu.afp_smooth_z        = 0.0f;
    oc->menu.afp_veh_off_x       = 0.0f;
    oc->menu.afp_veh_off_y       = 0.0f;
    oc->menu.afp_veh_off_z       = 0.0f;
}

static void stop_fp_fov_cam(OrbxNativeContext *oc) {
    if (oc->menu.fp_cam_active && oc->menu.natives[ORBX_NAT_RENDER_SCRIPT_CAMS]) {
        uint64_t rargs[5] = { 0u, 0u, 0u, 1u, 0u };
        nat_invoke(oc, ORBX_NAT_RENDER_SCRIPT_CAMS, rargs, 5, NULL);
    }
    if (oc->menu.fp_cam_handle && oc->menu.natives[ORBX_NAT_DOES_CAM_EXIST] &&
        oc->menu.natives[ORBX_NAT_DESTROY_CAM]) {
        if (nat1(oc, ORBX_NAT_DOES_CAM_EXIST, (uint64_t)oc->menu.fp_cam_handle)) {
            if (oc->menu.natives[ORBX_NAT_SET_CAM_ACTIVE]) {
                nat2(oc, ORBX_NAT_SET_CAM_ACTIVE, (uint64_t)oc->menu.fp_cam_handle, 0u);
            }
            nat2(oc, ORBX_NAT_DESTROY_CAM, (uint64_t)oc->menu.fp_cam_handle, 0u);
        }
    }
    oc->menu.fp_cam_handle = 0;
    oc->menu.fp_cam_active = 0u;
    oc->menu.fp_prev_veh   = 0;
}

static void reset_all_mods(OrbxNativeContext *oc) {
    stop_afp_cam(oc);
    stop_fp_fov_cam(oc);
    reset_all_gfx(oc);

    oc->menu.godmode               = 0u;
    oc->menu.never_wanted          = 0u;
    oc->menu.super_jump            = 0u;
    oc->menu.fast_run              = 0u;
    oc->menu.no_ragdoll            = 0u;
    oc->menu.invisible             = 0u;
    oc->menu.noclip_on             = 0u;
    oc->menu.spec_ability_inf      = 0u;
    oc->menu.wanted_cap_idx        = 0u;
    oc->menu.sprint_mult_idx       = 0u;
    oc->menu.swim_mult_idx         = 0u;
    oc->menu.inf_ammo              = 0u;
    oc->menu.explosive_ammo        = 0u;
    oc->menu.fire_ammo             = 0u;
    oc->menu.explosive_melee       = 0u;
    oc->menu.veh_god               = 0u;
    oc->menu.seatbelt_on           = 0u;
    oc->menu.horn_boost            = 0u;
    oc->menu.veh_drift_mode        = 0u;
    oc->menu.veh_torque_idx        = 0u;
    oc->menu.veh_bulletproof_tyres = 0u;
    oc->menu.veh_doors_locked      = 0u;
    oc->menu.veh_neon_on           = 0u;
    oc->menu.speedo_mode           = 0u;
    oc->menu.density_pct           = 100u;
    oc->menu.lock_hour             = -1;
    oc->menu.weather_idx           = 0;
    oc->menu.weather_persist       = 0u;
    oc->menu.clock_paused          = 0u;
    oc->menu.time_scale_idx        = 0u;
    oc->menu.low_gravity           = 0u;
    oc->menu.mobile_radio          = 0u;
    oc->menu.cloud_hat_idx         = -1;
    oc->menu.cloud_keyframe_idx    = -1;
    oc->menu.cloud_alpha_pct       = 100u;
    oc->menu.fp_fov_idx            = 0u;
    oc->menu.fp_lock               = 0u;
    oc->menu.fp_clip_fix           = 0u;
    oc->menu.cinematic_cam         = 0u;
    oc->menu.cam_anti_shake        = 0u;
    oc->menu.keep_steering         = 0u;
    oc->menu.nav_stick             = 0u;

    /* Alternate First Person default states (OFF by default) */
    oc->menu.afp_mode              = 0u;
    oc->menu.afp_fov_idx           = 3u; /* 90.0 deg */
    oc->menu.afp_headbob           = 2u; /* Normal */
    oc->menu.afp_smooth_follow     = 2u; /* Bodycam 0.85 */
    oc->menu.afp_recoil            = 2u; /* Standard */
    oc->menu.afp_inertia           = 1u;
    oc->menu.afp_crouch_lower      = 1u;
    oc->menu.afp_ads_zoom          = 2u; /* 25 deg */
    oc->menu.afp_crosshair         = 0u;
    oc->menu.afp_autoturn          = 1u;
    oc->menu.afp_clip_fix          = 1u;

    if (oc->menu.natives[ORBX_NAT_CLEAR_OVERRIDE_WEATHER]) {
        nat0(oc, ORBX_NAT_CLEAR_OVERRIDE_WEATHER);
    }
    if (oc->menu.natives[ORBX_NAT_UNLOAD_ALL_CLOUD_HATS]) {
        nat0(oc, ORBX_NAT_UNLOAD_ALL_CLOUD_HATS);
    }
    if (oc->menu.natives[ORBX_NAT_SET_CLOUDS_ALPHA]) {
        nat1(oc, ORBX_NAT_SET_CLOUDS_ALPHA, f2u(1.0f));
    }
    if (oc->menu.natives[ORBX_NAT_PAUSE_CLOCK]) {
        nat1(oc, ORBX_NAT_PAUSE_CLOCK, 0u);
    }
    if (oc->menu.natives[ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE]) {
        nat0(oc, ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE);
    }
    if (oc->menu.natives[ORBX_NAT_SET_TIME_SCALE]) {
        nat1(oc, ORBX_NAT_SET_TIME_SCALE, f2u(1.0f));
    }
    if (oc->menu.natives[ORBX_NAT_SET_GRAVITY_LEVEL]) {
        nat1(oc, ORBX_NAT_SET_GRAVITY_LEVEL, 0u);
    }
    int32_t r_ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    if (r_ped) {
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_VISIBLE]) {
            nat3(oc, ORBX_NAT_SET_ENTITY_VISIBLE, (uint64_t)r_ped, 1, 0);
        }
        if (oc->menu.natives[ORBX_NAT_RESET_ENTITY_ALPHA]) {
            nat1(oc, ORBX_NAT_RESET_ENTITY_ALPHA, (uint64_t)r_ped);
        }
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE]) {
            nat1(oc, ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE, (uint64_t)r_ped);
        }
    }
    oc->menu.invisible = 0u;
    oc->menu.applied_flags &= ~APP_INVISIBLE;
    recompute_suppress_mask(oc);
    set_toast(oc, "All Active Mods Reset to OFF");
}

static void act_heal_armor(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t max_hp = (int32_t)nat1(oc, ORBX_NAT_GET_ENTITY_MAX_HEALTH, (uint64_t)ped);
    if (max_hp < 200) max_hp = 200;
    nat3(oc, ORBX_NAT_SET_ENTITY_HEALTH, (uint64_t)ped, (uint64_t)max_hp, 0);
    nat2(oc, ORBX_NAT_SET_PED_ARMOUR, (uint64_t)ped, 100);
    if (ped) {
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_VISIBLE]) {
            nat3(oc, ORBX_NAT_SET_ENTITY_VISIBLE, (uint64_t)ped, 1, 0);
        }
        if (oc->menu.natives[ORBX_NAT_RESET_ENTITY_ALPHA]) {
            nat1(oc, ORBX_NAT_RESET_ENTITY_ALPHA, (uint64_t)ped);
        }
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE]) {
            nat1(oc, ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE, (uint64_t)ped);
        }
    }
    oc->menu.invisible = 0u;
    oc->menu.applied_flags &= ~APP_INVISIBLE;
    set_toast(oc, "Health Restored & Player Visible");
}

static void act_force_visible(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    if (ped) {
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_VISIBLE]) {
            nat3(oc, ORBX_NAT_SET_ENTITY_VISIBLE, (uint64_t)ped, 1, 0);
        }
        if (oc->menu.natives[ORBX_NAT_RESET_ENTITY_ALPHA]) {
            nat1(oc, ORBX_NAT_RESET_ENTITY_ALPHA, (uint64_t)ped);
        }
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE]) {
            nat1(oc, ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE, (uint64_t)ped);
        }
    }
    oc->menu.invisible = 0u;
    oc->menu.applied_flags &= ~APP_INVISIBLE;
    set_toast(oc, "Player Forced Visible");
}

static void act_clear_wanted(OrbxNativeContext *oc) {
    int32_t pl = (int32_t)nat0(oc, ORBX_NAT_PLAYER_ID);
    nat1(oc, ORBX_NAT_CLEAR_PLAYER_WANTED_LEVEL, (uint64_t)pl);
    set_toast(oc, "Wanted Level Cleared");
}

static void act_give_weapons(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    for (size_t i = 0; i < (sizeof(kWeaponHashes) / sizeof(kWeaponHashes[0])); i++) {
        uint64_t a[5] = { (uint64_t)ped, (uint64_t)kWeaponHashes[i], 9999u, 0, 0 };
        nat_invoke(oc, ORBX_NAT_GIVE_WEAPON_TO_PED, a, 5, NULL);
    }
    set_toast(oc, "All Weapons & Parachute Granted");
}

static int32_t get_current_veh(OrbxNativeContext *oc, int32_t ped) {
    if (!nat2(oc, ORBX_NAT_IS_PED_IN_ANY_VEHICLE, (uint64_t)ped, 0)) return 0;
    int32_t veh = (int32_t)nat2(oc, ORBX_NAT_GET_VEHICLE_PED_IS_IN, (uint64_t)ped, 0);
    if (!veh || !nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)veh)) return 0;
    return veh;
}

static void act_fix_vehicle(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    nat1(oc, ORBX_NAT_SET_VEHICLE_FIXED, (uint64_t)veh);
    nat2(oc, ORBX_NAT_SET_VEHICLE_DIRT_LEVEL, (uint64_t)veh, f2u(0.0f));
    nat2(oc, ORBX_NAT_SET_VEHICLE_ON_GROUND_PROPERLY, (uint64_t)veh, 0);
    set_toast(oc, "Vehicle Repaired & Washed");
}

static void act_flip_vehicle(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    nat2(oc, ORBX_NAT_SET_VEHICLE_ON_GROUND_PROPERLY, (uint64_t)veh, 0);
    set_toast(oc, "Vehicle Flipped Upright");
}

static void act_boost_vehicle(OrbxNativeContext *oc, float speed) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    nat2(oc, ORBX_NAT_SET_VEHICLE_FORWARD_SPEED, (uint64_t)veh, f2u(speed));
    set_toast(oc, "Super Speed Boost!");
}

static void act_plate_vehicle(OrbxNativeContext *oc, const char *plate_text) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    nat2(oc, ORBX_NAT_SET_VEHICLE_NUMBER_PLATE_TEXT, (uint64_t)veh, ptr2u(plate_text));
    char msg[32];
    size_t p = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, "Plate Set: ");
    p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, plate_text);
    msg[p] = '\0';
    set_toast(oc, msg);
}

/* Specific Vehicle Colors: Preset Palette */
static void apply_color_preset(OrbxNativeContext *oc, int preset_idx) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    if (preset_idx < 0 || (size_t)preset_idx >= (sizeof(kColorPresets) / sizeof(kColorPresets[0]))) return;

    const ColorPreset *cp = &kColorPresets[preset_idx];
    uint32_t target = oc->menu.veh_color_target;

    int cur_prim = 0, cur_sec = 0;
    int cur_pearl = 0, cur_wheel = 0;

    if (oc->menu.natives[ORBX_NAT_GET_VEHICLE_COLOURS]) {
        uint64_t a[3] = { (uint64_t)veh, ptr2u(&cur_prim), ptr2u(&cur_sec) };
        nat_invoke(oc, ORBX_NAT_GET_VEHICLE_COLOURS, a, 3, NULL);
    }
    if (oc->menu.natives[ORBX_NAT_GET_VEHICLE_EXTRA_COLOURS]) {
        uint64_t a[3] = { (uint64_t)veh, ptr2u(&cur_pearl), ptr2u(&cur_wheel) };
        nat_invoke(oc, ORBX_NAT_GET_VEHICLE_EXTRA_COLOURS, a, 3, NULL);
    }

    switch (target) {
    case 0: /* Both Primary & Secondary */
        nat3(oc, ORBX_NAT_SET_VEHICLE_COLOURS, (uint64_t)veh, (uint64_t)cp->id, (uint64_t)cp->id);
        break;
    case 1: /* Primary only */
        nat3(oc, ORBX_NAT_SET_VEHICLE_COLOURS, (uint64_t)veh, (uint64_t)cp->id, (uint64_t)cur_sec);
        break;
    case 2: /* Secondary only */
        nat3(oc, ORBX_NAT_SET_VEHICLE_COLOURS, (uint64_t)veh, (uint64_t)cur_prim, (uint64_t)cp->id);
        break;
    case 3: /* Pearlescent */
        if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_EXTRA_COLOURS]) {
            nat3(oc, ORBX_NAT_SET_VEHICLE_EXTRA_COLOURS, (uint64_t)veh, (uint64_t)cp->id, (uint64_t)cur_wheel);
        }
        break;
    case 4: /* Wheels */
        if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_EXTRA_COLOURS]) {
            nat3(oc, ORBX_NAT_SET_VEHICLE_EXTRA_COLOURS, (uint64_t)veh, (uint64_t)cur_pearl, (uint64_t)cp->id);
        }
        break;
    default:
        break;
    }

    char msg[48];
    size_t p = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, cp->name);
    p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, " Applied");
    msg[p] = '\0';
    set_toast(oc, msg);
}

/* Neon Lights Helper */
static void apply_neon_lights(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh || !oc->menu.natives[ORBX_NAT_SET_VEHICLE_NEON_ENABLED]) return;

    uint32_t on = oc->menu.veh_neon_on;
    nat3(oc, ORBX_NAT_SET_VEHICLE_NEON_ENABLED, (uint64_t)veh, 0, (on && oc->menu.veh_neon_left) ? 1 : 0);
    nat3(oc, ORBX_NAT_SET_VEHICLE_NEON_ENABLED, (uint64_t)veh, 1, (on && oc->menu.veh_neon_right) ? 1 : 0);
    nat3(oc, ORBX_NAT_SET_VEHICLE_NEON_ENABLED, (uint64_t)veh, 2, (on && oc->menu.veh_neon_front) ? 1 : 0);
    nat3(oc, ORBX_NAT_SET_VEHICLE_NEON_ENABLED, (uint64_t)veh, 3, (on && oc->menu.veh_neon_back) ? 1 : 0);

    if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_NEON_COLOUR]) {
        uint32_t ni = oc->menu.veh_neon_color_idx;
        if (ni >= 9) ni = 0;
        nat4(oc, ORBX_NAT_SET_VEHICLE_NEON_COLOUR, (uint64_t)veh,
             (uint64_t)kNeonColors[ni].r, (uint64_t)kNeonColors[ni].g, (uint64_t)kNeonColors[ni].b);
    }
}

/* Vehicle Mod Item Installation Helper */
static void apply_vehicle_mod_item(OrbxNativeContext *oc, int mod_type, int mod_level) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD_KIT]) {
        nat2(oc, ORBX_NAT_SET_VEHICLE_MOD_KIT, (uint64_t)veh, 0);
    }
    if (mod_level < 0) {
        if (oc->menu.natives[ORBX_NAT_REMOVE_VEHICLE_MOD]) {
            nat2(oc, ORBX_NAT_REMOVE_VEHICLE_MOD, (uint64_t)veh, (uint64_t)mod_type);
        } else if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD]) {
            nat4(oc, ORBX_NAT_SET_VEHICLE_MOD, (uint64_t)veh, (uint64_t)mod_type, (uint64_t)(uint32_t)-1, 0);
        }
    } else {
        if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD]) {
            nat4(oc, ORBX_NAT_SET_VEHICLE_MOD, (uint64_t)veh, (uint64_t)mod_type, (uint64_t)(uint32_t)mod_level, 0);
        }
    }
}

/* Vehicle Performance Tuning */
static void act_max_vehicle_mods(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }

    if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD_KIT]) {
        nat2(oc, ORBX_NAT_SET_VEHICLE_MOD_KIT, (uint64_t)veh, 0);
    }
    if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD]) {
        nat4(oc, ORBX_NAT_SET_VEHICLE_MOD, (uint64_t)veh, 11, 3, 0); /* Engine Level 4 */
        nat4(oc, ORBX_NAT_SET_VEHICLE_MOD, (uint64_t)veh, 12, 2, 0); /* Race Brakes */
        nat4(oc, ORBX_NAT_SET_VEHICLE_MOD, (uint64_t)veh, 13, 2, 0); /* Race Transmission */
        nat4(oc, ORBX_NAT_SET_VEHICLE_MOD, (uint64_t)veh, 15, 3, 0); /* Competition Suspension */
        nat4(oc, ORBX_NAT_SET_VEHICLE_MOD, (uint64_t)veh, 16, 4, 0); /* 100% Armor */
    }
    if (oc->menu.natives[ORBX_NAT_TOGGLE_VEHICLE_MOD]) {
        nat3(oc, ORBX_NAT_TOGGLE_VEHICLE_MOD, (uint64_t)veh, 18, 1); /* Turbo */
        nat3(oc, ORBX_NAT_TOGGLE_VEHICLE_MOD, (uint64_t)veh, 22, 1); /* Xenon */
    }
    if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_WINDOW_TINT]) {
        nat2(oc, ORBX_NAT_SET_VEHICLE_WINDOW_TINT, (uint64_t)veh, 1); /* Pure Black */
    }
    if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
        nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)veh, 0);
        oc->menu.veh_bulletproof_tyres = 1u;
    }
    nat1(oc, ORBX_NAT_SET_VEHICLE_FIXED, (uint64_t)veh);
    nat2(oc, ORBX_NAT_SET_VEHICLE_DIRT_LEVEL, (uint64_t)veh, f2u(0.0f));
    oc->menu.veh_engine_lvl = 4u;
    oc->menu.veh_brake_lvl  = 3u;
    oc->menu.veh_trans_lvl  = 3u;
    oc->menu.veh_susp_lvl   = 4u;
    oc->menu.veh_armor_lvl  = 5u;
    oc->menu.veh_turbo_on   = 1u;
    oc->menu.veh_xenon_on   = 1u;
    set_toast(oc, "Max Performance Tuning Installed");
}

/* Clouds & NVE Skyframe Actions */
static void act_apply_cloud_hat(OrbxNativeContext *oc, int hat_idx) {
    if (hat_idx < 0 || hat_idx >= 6) return;
    char hat_name[24];
    copy_cstr(hat_name, sizeof(hat_name), kCloudHats[hat_idx].name);
    if (oc->menu.natives[ORBX_NAT_LOAD_CLOUD_HAT]) {
        if (oc->menu.natives[ORBX_NAT_UNLOAD_ALL_CLOUD_HATS]) {
            nat0(oc, ORBX_NAT_UNLOAD_ALL_CLOUD_HATS);
        }
        nat2(oc, ORBX_NAT_LOAD_CLOUD_HAT, ptr2u(hat_name), f2u(0.5f));
        oc->menu.cloud_hat_idx = hat_idx;
        char msg[48];
        size_t p = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, "Cloud Hat: ");
        p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, kCloudHats[hat_idx].label);
        msg[p] = '\0';
        set_toast(oc, msg);
    } else {
        set_toast(oc, "Cloud hat native not found");
    }
}

static void act_apply_cloud_keyframe(OrbxNativeContext *oc, int kf_idx) {
    if (!oc->menu.natives[ORBX_NAT_SET_CLOUD_SETTINGS_OVERRIDE]) {
        set_toast(oc, "Cloud override native unavailable");
        return;
    }
    if (kf_idx < 0 || kf_idx >= 14) {
        oc->menu.cloud_keyframe_idx = -1;
        char empty[4] = "";
        nat1(oc, ORBX_NAT_SET_CLOUD_SETTINGS_OVERRIDE, ptr2u(empty));
        set_toast(oc, "Cloud Keyframe: Weather Default");
        return;
    }
    oc->menu.cloud_keyframe_idx = kf_idx;
    char kf_name[24];
    copy_cstr(kf_name, sizeof(kf_name), kCloudKeyframes[kf_idx].name);
    nat1(oc, ORBX_NAT_SET_CLOUD_SETTINGS_OVERRIDE, ptr2u(kf_name));
    char msg[48];
    size_t p = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, "Clouds NVE: ");
    p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, kCloudKeyframes[kf_idx].label);
    msg[p] = '\0';
    set_toast(oc, msg);
}

static void act_clear_cloud_hats(OrbxNativeContext *oc) {
    if (oc->menu.natives[ORBX_NAT_UNLOAD_ALL_CLOUD_HATS]) {
        nat0(oc, ORBX_NAT_UNLOAD_ALL_CLOUD_HATS);
    }
    oc->menu.cloud_hat_idx = -1;
    if (oc->menu.natives[ORBX_NAT_SET_CLOUD_SETTINGS_OVERRIDE]) {
        char empty[4] = "";
        nat1(oc, ORBX_NAT_SET_CLOUD_SETTINGS_OVERRIDE, ptr2u(empty));
    }
    oc->menu.cloud_keyframe_idx = -1;
    set_toast(oc, "Cloud Hats & Keyframes Cleared");
}

static void act_apply_weather_idx(OrbxNativeContext *oc, int wi) {
    if (wi < 0 || wi >= 15) return;
    oc->menu.weather_idx = wi;
    oc->menu.last_weather_apply_tick = oc->menu.main_ticks;
    char wname[18];
    copy_cstr(wname, sizeof(wname), kWeathers[wi].name);
    if (oc->menu.natives[ORBX_NAT_SET_WEATHER_TYPE_NOW_PERSIST]) {
        nat1(oc, ORBX_NAT_SET_WEATHER_TYPE_NOW_PERSIST, ptr2u(wname));
    }
    if (oc->menu.weather_persist && oc->menu.natives[ORBX_NAT_SET_OVERRIDE_WEATHER]) {
        nat1(oc, ORBX_NAT_SET_OVERRIDE_WEATHER, ptr2u(wname));
    }
    set_toast(oc, kWeathers[wi].label);
}

/* Camera & First-Person POV Helpers */
static uint32_t cam_mode_to_opt_idx(uint32_t mode) {
    for (uint32_t i = 0; i < 4u; i++) {
        if ((uint32_t)kCamViewOptions[i].mode_val == mode) return i;
    }
    return 0u;
}

static void set_cam_view_mode_all(OrbxNativeContext *oc, int is_veh, uint32_t mode) {
    if (oc->menu.natives[ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE] && mode != 3u) {
        if (oc->menu.cinematic_cam) {
            oc->menu.cinematic_cam = 0u;
            nat1(oc, ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE, 0u);
        }
    }
    if (!is_veh) {
        if (oc->menu.natives[ORBX_NAT_SET_FOLLOW_PED_CAM_VIEW_MODE]) {
            nat1(oc, ORBX_NAT_SET_FOLLOW_PED_CAM_VIEW_MODE, (uint64_t)mode);
        }
        if (oc->menu.natives[ORBX_NAT_SET_CAM_VIEW_MODE_FOR_CONTEXT]) {
            /* Context 0 is on-foot; do not overwrite active vehicle context */
            nat2(oc, ORBX_NAT_SET_CAM_VIEW_MODE_FOR_CONTEXT, 0u, (uint64_t)mode);
        }
    } else {
        if (oc->menu.natives[ORBX_NAT_SET_FOLLOW_VEHICLE_CAM_VIEW_MODE]) {
            nat1(oc, ORBX_NAT_SET_FOLLOW_VEHICLE_CAM_VIEW_MODE, (uint64_t)mode);
        }
        if (oc->menu.natives[ORBX_NAT_SET_CAM_VIEW_MODE_FOR_CONTEXT]) {
            /* Update all vehicle contexts (car, bike, boat, aircraft, sub, heli) without touching context 0 */
            for (uint64_t c = 1u; c <= 6u; c++) {
                nat2(oc, ORBX_NAT_SET_CAM_VIEW_MODE_FOR_CONTEXT, c, (uint64_t)mode);
            }
        }
    }
}

static void act_reset_cam_behind(OrbxNativeContext *oc) {
    if (oc->menu.natives[ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_HEADING]) {
        nat1(oc, ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_HEADING, f2u(0.0f));
    }
    if (oc->menu.natives[ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_PITCH]) {
        nat2(oc, ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_PITCH, f2u(0.0f), f2u(1.0f));
    }
    set_toast(oc, "Camera Reset Behind Player");
}

static void act_open_keyboard(OrbxNativeContext *oc, uint32_t kb_mode) {
    if (oc->menu.natives[ORBX_NAT_DISPLAY_ONSCREEN_KEYBOARD] &&
        oc->menu.natives[ORBX_NAT_UPDATE_ONSCREEN_KEYBOARD] &&
        oc->menu.natives[ORBX_NAT_GET_ONSCREEN_KEYBOARD_RESULT]) {
        oc->menu.kb_mode = kb_mode;
        char tip[24];
        copy_cstr(tip, sizeof(tip), "FMMC_KEY_TIP8");
        char empty[4] = "";
        uint64_t max_len = (kb_mode == 1u) ? 8u : 32u;
        uint64_t ka[8] = {
            0, ptr2u(tip), ptr2u(empty), ptr2u(empty),
            ptr2u(empty), ptr2u(empty), ptr2u(empty), max_len
        };
        nat_invoke(oc, ORBX_NAT_DISPLAY_ONSCREEN_KEYBOARD, ka, 8, NULL);
        oc->menu.kb_active = 1u;
    } else {
        set_toast(oc, "Onscreen keyboard native unavailable");
    }
}

static void act_warp_nearest_veh(OrbxNativeContext *oc) {
    if (!oc->menu.natives[ORBX_NAT_GET_CLOSEST_VEHICLE]) {
        set_toast(oc, "Closest vehicle native unavailable");
        return;
    }
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    Vec3 pos;
    zero_bytes(&pos, sizeof(pos));
    uint64_t gargs[2] = { (uint64_t)ped, 1u };
    nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, gargs, 2, &pos);
    uint64_t cargs[5] = { f2u(pos.x), f2u(pos.y), f2u(pos.z), f2u(80.0f), 0u };
    /* 6-7 params on GET_CLOSEST_VEHICLE(x,y,z,radius,modelHash,flags) */
    uint64_t cargs6[6] = { f2u(pos.x), f2u(pos.y), f2u(pos.z), f2u(80.0f), 0u, 70u };
    int32_t veh = (int32_t)nat_invoke(oc, ORBX_NAT_GET_CLOSEST_VEHICLE, cargs6, 6, NULL);
    (void)cargs;
    if (veh && nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)veh)) {
        nat3(oc, ORBX_NAT_SET_PED_INTO_VEHICLE, (uint64_t)ped, (uint64_t)veh, (uint64_t)(int64_t)-1);
        set_toast(oc, "Warped Into Nearest Vehicle");
    } else {
        set_toast(oc, "No Vehicle Nearby");
    }
}

static void act_door_op(OrbxNativeContext *oc, int op, int door_idx) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    if (op == 0 && oc->menu.natives[ORBX_NAT_SET_VEHICLE_DOOR_OPEN]) {
        for (uint64_t d = 0; d < 6; d++) {
            nat4(oc, ORBX_NAT_SET_VEHICLE_DOOR_OPEN, (uint64_t)veh, d, 0, 0);
        }
        set_toast(oc, "All Doors Opened");
    } else if (op == 1 && oc->menu.natives[ORBX_NAT_SET_VEHICLE_DOORS_SHUT]) {
        nat2(oc, ORBX_NAT_SET_VEHICLE_DOORS_SHUT, (uint64_t)veh, 0);
        set_toast(oc, "All Doors Closed");
    } else if (op == 2 && oc->menu.natives[ORBX_NAT_SET_VEHICLE_DOOR_OPEN]) {
        nat4(oc, ORBX_NAT_SET_VEHICLE_DOOR_OPEN, (uint64_t)veh, (uint64_t)door_idx, 0, 0);
        set_toast(oc, "Door Opened");
    }
}

static void teleport_player_entity(OrbxNativeContext *oc, float x, float y, float z) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    int32_t ent = veh ? veh : ped;
    uint64_t a[7] = { (uint64_t)ent, f2u(x), f2u(y), f2u(z), 0, 0, 0 };
    nat_invoke(oc, ORBX_NAT_SET_ENTITY_COORDS_NO_OFFSET, a, 7, NULL);
}

static void act_tp_ipl(OrbxNativeContext *oc, uint8_t idx) {
    if (idx >= (uint8_t)(sizeof(kIplInteriors) / sizeof(kIplInteriors[0]))) return;
    const IplEntry *e = &kIplInteriors[idx];
    if (oc->menu.natives[ORBX_NAT_REQUEST_IPL]) {
        if (e->is_ny) {
            static const char kNyIpls[11][14] = {
                "prologue01", "prologue01c", "prologue01d", "prologue01e",
                "prologue01f", "prologue01g", "prologue01h", "prologue01i",
                "prologue01j", "prologue01k", "prologue01z"
            };
            char buf[24];
            for (size_t i = 0; i < 11; i++) {
                copy_cstr(buf, sizeof(buf), kNyIpls[i]);
                nat1(oc, ORBX_NAT_REQUEST_IPL, ptr2u(buf));
            }
        } else if (e->ipl[0]) {
            char buf[28];
            copy_cstr(buf, sizeof(buf), e->ipl);
            nat1(oc, ORBX_NAT_REQUEST_IPL, ptr2u(buf));
        }
    }
    teleport_player_entity(oc, e->x, e->y, e->z);
    set_toast(oc, e->label);
}

static int probe_ground_z(OrbxNativeContext *oc, float x, float y, float *out_z) {
    static const float kHeights[8] = { 1000.0f, 800.0f, 600.0f, 400.0f, 250.0f, 150.0f, 75.0f, 30.0f };
    for (size_t i = 0; i < 8; i++) {
        float gz = 0.0f;
        uint64_t a[6] = { f2u(x), f2u(y), f2u(kHeights[i]), ptr2u(&gz), 0, 0 };
        if (nat_invoke(oc, ORBX_NAT_GET_GROUND_Z_FOR_3D_COORD, a, 6, NULL)) {
            *out_z = gz + 1.5f;
            return 1;
        }
    }
    return 0;
}

static void act_tp_waypoint(OrbxNativeContext *oc) {
    if (!nat0(oc, ORBX_NAT_IS_WAYPOINT_ACTIVE)) {
        set_toast(oc, "No Map Waypoint Active");
        return;
    }
    uint32_t blip = (uint32_t)nat1(oc, ORBX_NAT_GET_FIRST_BLIP_INFO_ID, 8u);
    if (!blip || !nat1(oc, ORBX_NAT_DOES_BLIP_EXIST, (uint64_t)blip)) {
        set_toast(oc, "Waypoint Marker Not Found");
        return;
    }

    Vec3 wp;
    zero_bytes(&wp, sizeof(wp));
    uint64_t bargs[1] = { (uint64_t)blip };
    nat_invoke(oc, ORBX_NAT_GET_BLIP_COORDS, bargs, 1, &wp);

    float gz = 0.0f;
    if (probe_ground_z(oc, wp.x, wp.y, &gz)) {
        teleport_player_entity(oc, wp.x, wp.y, gz);
        set_toast(oc, "Teleported to Waypoint");
        oc->menu.pending_tp_wp_ticks = 0;
    } else {
        teleport_player_entity(oc, wp.x, wp.y, 800.0f);
        set_toast(oc, "Loading Collision (Hold on)...");
        oc->menu.pending_tp_x = wp.x;
        oc->menu.pending_tp_y = wp.y;
        oc->menu.pending_tp_wp_ticks = 30u;
    }
}

static void start_vehicle_spawn(OrbxNativeContext *oc, uint32_t model_hash) {
    if (!model_hash) {
        set_toast(oc, "Invalid model hash");
        return;
    }

    nat1(oc, ORBX_NAT_REQUEST_MODEL, (uint64_t)model_hash);

    if (oc->menu.natives[ORBX_NAT_IS_MODEL_IN_CDIMAGE] &&
        !nat1(oc, ORBX_NAT_IS_MODEL_IN_CDIMAGE, (uint64_t)model_hash)) {
        set_toast(oc, "Model Not Found in Game Image");
        return;
    }

    if (oc->menu.natives[ORBX_NAT_IS_MODEL_VALID] &&
        !nat1(oc, ORBX_NAT_IS_MODEL_VALID, (uint64_t)model_hash)) {
        set_toast(oc, "Model Hash Invalid");
        return;
    }

    set_toast(oc, "Streaming Model...");
    oc->menu.pending_spawn_hash  = model_hash;
    oc->menu.pending_spawn_ticks = 900u;
}

static void tick_async_requests(OrbxNativeContext *oc) {
    if (oc->menu.pending_tp_wp_ticks > 0) {
        oc->menu.pending_tp_wp_ticks--;
        float gz = 0.0f;
        if (probe_ground_z(oc, oc->menu.pending_tp_x, oc->menu.pending_tp_y, &gz)) {
            teleport_player_entity(oc, oc->menu.pending_tp_x, oc->menu.pending_tp_y, gz);
            oc->menu.pending_tp_wp_ticks = 0;
        }
    }

    uint32_t mhash = oc->menu.pending_spawn_hash;
    if (!mhash) return;

    if (nat1(oc, ORBX_NAT_HAS_MODEL_LOADED, (uint64_t)mhash)) {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        Vec3 forward;
        zero_bytes(&forward, sizeof(forward));
        uint64_t off_car[4] = { (uint64_t)ped, f2u(0.0f), f2u(5.5f), f2u(0.5f) };
        nat_invoke(oc, ORBX_NAT_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, off_car, 4, &forward);
        float heading = u2f(nat1(oc, ORBX_NAT_GET_ENTITY_HEADING, (uint64_t)ped));
        /* 7 args required by CREATE_VEHICLE (isNetwork=0, netMissionEntity=0 for SP stability) */
        uint64_t cargs[7] = {
            (uint64_t)mhash,
            f2u(forward.x), f2u(forward.y), f2u(forward.z),
            f2u(heading), 0u, 0u
        };
        int32_t new_veh = (int32_t)nat_invoke(oc, ORBX_NAT_CREATE_VEHICLE, cargs, 7, NULL);
        if (new_veh) {
            nat3(oc, ORBX_NAT_SET_PED_INTO_VEHICLE, (uint64_t)ped, (uint64_t)new_veh, (uint64_t)(int64_t)-1);
            nat2(oc, ORBX_NAT_SET_VEHICLE_ON_GROUND_PROPERLY, (uint64_t)new_veh, 0);
            set_toast(oc, "Vehicle Spawned & Seated!");
        } else {
            set_toast(oc, "Vehicle Creation Failed");
        }
        nat1(oc, ORBX_NAT_SET_MODEL_AS_NO_LONGER_NEEDED, (uint64_t)mhash);
        oc->menu.pending_spawn_hash  = 0;
        oc->menu.pending_spawn_ticks = 0;
    } else if (oc->menu.pending_spawn_ticks > 0) {
        oc->menu.pending_spawn_ticks--;
        nat1(oc, ORBX_NAT_REQUEST_MODEL, (uint64_t)mhash);
        if (oc->menu.pending_spawn_ticks == 0) {
            set_toast(oc, "Model Streaming Timed Out");
            oc->menu.pending_spawn_hash = 0;
        }
    }
}

static void process_web_action(OrbxNativeContext *oc) {
    uint32_t act = oc->menu.web_action;
    if (!act) return;
    int32_t arg0 = oc->menu.web_arg0;
    uint32_t shash = oc->menu.web_spawn_hash;
    oc->menu.web_action = ORBX_ACT_NONE;

    switch (act) {
    case ORBX_ACT_HEAL_ARMOR:    act_heal_armor(oc); break;
    case ORBX_ACT_CLEAR_WANTED:  act_clear_wanted(oc); break;
    case ORBX_ACT_GIVE_WEAPONS:  act_give_weapons(oc); break;
    case ORBX_ACT_FIX_VEHICLE:   act_fix_vehicle(oc); break;
    case ORBX_ACT_FLIP_VEHICLE:  act_flip_vehicle(oc); break;
    case ORBX_ACT_BOOST_VEHICLE: act_boost_vehicle(oc, 85.0f); break;
    case ORBX_ACT_COLOR_VEHICLE: {
        uint32_t preset = (uint32_t)(arg0 & 0xFFFF);
        uint32_t target = (uint32_t)(((uint32_t)arg0 >> 16) & 0xFF);
        if (target < 5) oc->menu.veh_color_target = target;
        apply_color_preset(oc, (int)preset);
        break;
    }
    case ORBX_ACT_SET_VEH_MOD: {
        int slot = (int)(arg0 & 0xFF);
        int lvl  = (int)((int16_t)(((uint32_t)arg0 >> 16) & 0xFFFF));
        apply_vehicle_mod_item(oc, slot, lvl);
        break;
    }
    case ORBX_ACT_SET_WINDOW_TINT: {
        int tint = (int)(arg0 & 0xFF);
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_WINDOW_TINT]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_WINDOW_TINT, (uint64_t)veh, (uint64_t)tint);
            oc->menu.veh_tint_idx = (uint32_t)tint;
        }
        break;
    }
    case ORBX_ACT_TP_WAYPOINT:   act_tp_waypoint(oc); break;
    case ORBX_ACT_SET_TIME:
        if (arg0 >= 0 && arg0 <= 23) {
            oc->menu.lock_hour = arg0;
            oc->menu.last_lock_hour_tick = oc->menu.main_ticks;
            nat3(oc, ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME, (uint64_t)arg0, 0, 0);
            set_toast(oc, "Clock Time Updated");
        } else if (arg0 < 0) {
            oc->menu.lock_hour = -1;
            set_toast(oc, "Clock Unlocked");
        }
        break;
    case ORBX_ACT_SET_WEATHER:
        act_apply_weather_idx(oc, arg0);
        break;
    case ORBX_ACT_SPAWN_VEH:
        start_vehicle_spawn(oc, shash);
        break;
    case ORBX_ACT_SET_GFX_PRESET:
        if (arg0 < 0) {
            reset_all_gfx(oc);
        } else {
            uint32_t preset   = (uint32_t)(arg0 & 0xFFFF);
            uint32_t strength = (uint32_t)(((uint32_t)arg0 >> 16) & 0xFFFF);
            apply_gfx_preset_with_strength(oc, preset, strength);
        }
        break;
    case ORBX_ACT_SET_GFX_STRENGTH:
        apply_gfx_strength(oc, (uint32_t)arg0);
        break;
    case ORBX_ACT_SET_POSTFX:
        apply_gfx_postfx(oc, (uint32_t)arg0);
        break;
    case ORBX_ACT_APPLY_CLOUD_HAT:
        act_apply_cloud_hat(oc, arg0);
        break;
    case ORBX_ACT_APPLY_CLOUD_KEYFRAME:
        act_apply_cloud_keyframe(oc, arg0);
        break;
    case ORBX_ACT_CLEAR_CLOUD_HATS:
        act_clear_cloud_hats(oc);
        break;
    case ORBX_ACT_RESET_GFX:
        reset_all_gfx(oc);
        break;
    case ORBX_ACT_SET_ENB_EXTRA: {
        if (arg0 <= 0) {
            oc->menu.enb_bloom_lvl = 0u;
            sync_enb_graphics_pipeline(oc);
            set_toast(oc, "Stage 2 ENB FX: OFF");
        } else if (arg0 == 2) {
            if (oc->menu.enb_bloom_lvl == 0u) oc->menu.enb_bloom_lvl = 3u;
            sync_enb_graphics_pipeline(oc);
            set_toast(oc, "Stage 2: ENB Bloom & Glow Active");
        } else if (arg0 == 3) {
            if (oc->menu.enb_shadow_soft == 0u) oc->menu.enb_shadow_soft = 3u;
            if (oc->menu.enb_ssao_boost == 0u)  oc->menu.enb_ssao_boost  = 3u;
            if (oc->menu.enb_bloom_lvl == 0u)   oc->menu.enb_bloom_lvl   = 3u;
            sync_enb_graphics_pipeline(oc);
            set_toast(oc, "Stage 2: Full PC ENB Active");
        } else {
            if (oc->menu.enb_shadow_soft == 0u) oc->menu.enb_shadow_soft = 3u;
            if (oc->menu.enb_ssao_boost == 0u)  oc->menu.enb_ssao_boost  = 2u;
            sync_enb_graphics_pipeline(oc);
            set_toast(oc, "Stage 2: Dynamic Shadows & AO Active");
        }
        break;
    }
    case ORBX_ACT_SET_SHADOW_QUALITY: {
        if (arg0 <= 0) {
            oc->menu.enb_shadow_soft = 0u;
            oc->menu.enb_ssao_boost  = 0u;
            sync_enb_graphics_pipeline(oc);
            set_toast(oc, "Dynamic Shadows & AO: Vanilla");
        } else {
            uint32_t lvl = (shash >= 40u) ? 5u : ((shash >= 32u) ? 4u : ((shash >= 24u) ? 3u : ((shash >= 18u) ? 2u : 1u)));
            oc->menu.enb_shadow_soft = lvl;
            if (oc->menu.enb_ssao_boost == 0u) oc->menu.enb_ssao_boost = (lvl > 1u) ? (lvl - 1u) : 1u;
            sync_enb_graphics_pipeline(oc);
            set_toast(oc, "Stage 2 Dynamic Shadows & AO Updated");
        }
        break;
    }
    case ORBX_ACT_APPLY_CAM_VIEW: {
        uint32_t mode = kCamViewOptions[arg0 & 3].mode_val;
        int is_veh = (arg0 >> 8) & 1;
        if (mode != 4u && oc->menu.fp_lock) {
            oc->menu.fp_lock = 0u;
            oc->menu.applied_flags &= ~APP_FP_LOCK;
        }
        if (is_veh) oc->menu.cam_veh_view_mode = mode;
        else        oc->menu.cam_ped_view_mode = mode;
        set_cam_view_mode_all(oc, is_veh, mode);
        set_toast(oc, "Camera View Updated");
        break;
    }
    case ORBX_ACT_RESET_CAM_BEHIND:
        act_reset_cam_behind(oc);
        break;
    case ORBX_ACT_MAX_VEH:
        act_max_vehicle_mods(oc);
        break;
    case ORBX_ACT_WARP_NEAREST_VEH:
        act_warp_nearest_veh(oc);
        break;
    case ORBX_ACT_SET_TIMESCALE:
        if (arg0 >= 0 && arg0 < 4) {
            oc->menu.time_scale_idx = (uint32_t)arg0;
            if (oc->menu.natives[ORBX_NAT_SET_TIME_SCALE]) {
                nat1(oc, ORBX_NAT_SET_TIME_SCALE, f2u(kTimeScales[arg0]));
            }
            set_toast(oc, kTimeScaleNames[arg0]);
        }
        break;
    case ORBX_ACT_CLEAR_AREA: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        Vec3 pos;
        zero_bytes(&pos, sizeof(pos));
        uint64_t ga[2] = { (uint64_t)ped, 1u };
        nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ga, 2, &pos);
        if ((arg0 == 0 || arg0 == 2) && oc->menu.natives[ORBX_NAT_CLEAR_AREA_OF_PEDS]) {
            uint64_t pa[5] = { f2u(pos.x), f2u(pos.y), f2u(pos.z), f2u(150.0f), 1u };
            nat_invoke(oc, ORBX_NAT_CLEAR_AREA_OF_PEDS, pa, 5, NULL);
        }
        if ((arg0 == 1 || arg0 == 2) && oc->menu.natives[ORBX_NAT_CLEAR_AREA_OF_VEHICLES]) {
            uint64_t va[8] = { f2u(pos.x), f2u(pos.y), f2u(pos.z), f2u(150.0f), 0u, 0u, 0u, 0u };
            nat_invoke(oc, ORBX_NAT_CLEAR_AREA_OF_VEHICLES, va, 8, NULL);
        }
        set_toast(oc, "Area Cleared");
        break;
    }
    case ORBX_ACT_RESET_KEYBINDS:
        reset_default_keybinds(oc);
        oc->menu.cfg_save_req = 1u;
        set_toast(oc, "Keybinds Reset to Default");
        break;
    case ORBX_ACT_SAVE_CONFIG:
        oc->menu.cfg_save_req = 1u;
        set_toast(oc, "Settings Saved to config.ini");
        break;
    case ORBX_ACT_RESET_ALL_MODS:
        reset_all_mods(oc);
        break;
    case ORBX_ACT_TP_PRESET:
        if (arg0 >= 0 && arg0 < (int)(sizeof(kTeleports) / sizeof(kTeleports[0]))) {
            teleport_player_entity(oc, kTeleports[arg0].x, kTeleports[arg0].y, kTeleports[arg0].z);
            set_toast(oc, kTeleports[arg0].name);
        }
        break;
    case ORBX_ACT_TOGGLE_REALISTIC_FUEL:
        oc->menu.fuel_system_on ^= 1u;
        if (oc->menu.fuel_system_on && oc->menu.fuel_level <= 0.0f) {
            oc->menu.fuel_level = 100.0f;
        }
        set_toast(oc, oc->menu.fuel_system_on ? "Realistic Fuel ON" : "Realistic Fuel OFF");
        break;
    case ORBX_ACT_REFUEL_VEHICLE:
        oc->menu.fuel_level = 100.0f;
        play_ui_sound(oc, "SELECT");
        set_toast(oc, "Vehicle Fuel: 100% Full");
        break;
    case ORBX_ACT_TOGGLE_CALMER_POLICE:
        oc->menu.calmer_police_on ^= 1u;
        set_toast(oc, oc->menu.calmer_police_on ? "Calmer Police AI ON" : "Calmer Police AI OFF");
        break;
    case ORBX_ACT_TOGGLE_GTA6_HUD:
        oc->menu.gta6_hud_on ^= 1u;
        set_toast(oc, oc->menu.gta6_hud_on ? "GTA VI HUD ON" : "GTA VI HUD OFF");
        break;
    case ORBX_ACT_TOGGLE_GTA6_WHEEL:
        oc->menu.gta6_wheel_on ^= 1u;
        set_toast(oc, oc->menu.gta6_wheel_on ? "GTA VI Weapon Wheel ON" : "GTA VI Weapon Wheel OFF");
        break;
    default:
        break;
    }
}

static void tick_noclip(OrbxNativeContext *oc, int32_t ped, int32_t veh) {
    int32_t ent = veh ? veh : ped;
    if (oc->menu.noclip_on) {
        if (!oc->menu.noclip_pos_valid || (oc->menu.noclip_prev_ent != ent)) {
            Vec3 pos;
            zero_bytes(&pos, sizeof(pos));
            uint64_t ga[2] = { (uint64_t)ent, 1u };
            nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ga, 2, &pos);
            oc->menu.noclip_x = pos.x;
            oc->menu.noclip_y = pos.y;
            oc->menu.noclip_z = pos.z;
            oc->menu.noclip_pos_valid = 1u;
            if (oc->menu.noclip_prev_ent && oc->menu.noclip_prev_ent != ent &&
                (!oc->menu.natives[ORBX_NAT_DOES_ENTITY_EXIST] ||
                 nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)oc->menu.noclip_prev_ent))) {
                if (oc->menu.natives[ORBX_NAT_FREEZE_ENTITY_POSITION]) {
                    nat2(oc, ORBX_NAT_FREEZE_ENTITY_POSITION, (uint64_t)oc->menu.noclip_prev_ent, 0);
                }
                if (oc->menu.natives[ORBX_NAT_SET_ENTITY_COLLISION]) {
                    nat3(oc, ORBX_NAT_SET_ENTITY_COLLISION, (uint64_t)oc->menu.noclip_prev_ent, 1, 1);
                }
            }
            if (oc->menu.natives[ORBX_NAT_FREEZE_ENTITY_POSITION]) {
                nat2(oc, ORBX_NAT_FREEZE_ENTITY_POSITION, (uint64_t)ent, 1);
            }
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_COLLISION]) {
                nat3(oc, ORBX_NAT_SET_ENTITY_COLLISION, (uint64_t)ent, 0, 0);
            }
            oc->menu.noclip_prev_ent = ent;
        }
        Vec3 cam_rot;
        zero_bytes(&cam_rot, sizeof(cam_rot));
        if (oc->menu.natives[ORBX_NAT_GET_GAMEPLAY_CAM_ROT]) {
            uint64_t ra[1] = { 2u };
            nat_invoke(oc, ORBX_NAT_GET_GAMEPLAY_CAM_ROT, ra, 1, &cam_rot);
        }
        float yaw   = cam_rot.z * 0.01745329f;
        float pitch = cam_rot.x * 0.01745329f;
        float cp = pic_cosf(pitch), sp = pic_sinf(pitch);
        float sy = pic_sinf(yaw),   cy = pic_cosf(yaw);
        float fx = -sy * cp, fy = cy * cp, fz = sp;
        float rx =  cy,      ry = sy;

        float lr = 0.0f, ud = 0.0f;
        if (oc->menu.natives[ORBX_NAT_GET_DISABLED_CONTROL_NORMAL] && !oc->menu.menu_open) {
            lr = u2f(nat2(oc, ORBX_NAT_GET_DISABLED_CONTROL_NORMAL, 0, IN_MOVE_LR));
            ud = u2f(nat2(oc, ORBX_NAT_GET_DISABLED_CONTROL_NORMAL, 0, IN_MOVE_UD));
        }
        uint32_t spd_i = (oc->menu.noclip_speed_idx < 4u) ? oc->menu.noclip_speed_idx : 1u;
        float spd = kAirbreakSpeeds[spd_i];
        float fwd_in = -ud; /* Push up on left stick = negative Y axis */
        oc->menu.noclip_x += (fx * fwd_in + rx * lr) * spd;
        oc->menu.noclip_y += (fy * fwd_in + ry * lr) * spd;
        oc->menu.noclip_z += (fz * fwd_in) * spd;

        if (!oc->menu.menu_open) {
            if (oc->menu.pad_buttons_live & SCE_PAD_BTN_R2) oc->menu.noclip_z += spd;
            if (oc->menu.pad_buttons_live & SCE_PAD_BTN_L2) oc->menu.noclip_z -= spd;
        }
        uint64_t ca[7] = { (uint64_t)ent, f2u(oc->menu.noclip_x), f2u(oc->menu.noclip_y), f2u(oc->menu.noclip_z), 1u, 1u, 1u };
        nat_invoke(oc, ORBX_NAT_SET_ENTITY_COORDS_NO_OFFSET, ca, 7, NULL);
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_HEADING]) {
            nat2(oc, ORBX_NAT_SET_ENTITY_HEADING, (uint64_t)ent, f2u(cam_rot.z));
        }
        oc->menu.applied_flags |= APP_NOCLIP;
    } else if (oc->menu.applied_flags & APP_NOCLIP) {
        if (oc->menu.noclip_prev_ent && oc->menu.noclip_prev_ent != ent &&
            (!oc->menu.natives[ORBX_NAT_DOES_ENTITY_EXIST] ||
             nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)oc->menu.noclip_prev_ent))) {
            if (oc->menu.natives[ORBX_NAT_FREEZE_ENTITY_POSITION]) {
                nat2(oc, ORBX_NAT_FREEZE_ENTITY_POSITION, (uint64_t)oc->menu.noclip_prev_ent, 0);
            }
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_COLLISION]) {
                nat3(oc, ORBX_NAT_SET_ENTITY_COLLISION, (uint64_t)oc->menu.noclip_prev_ent, 1, 1);
            }
        }
        if (oc->menu.natives[ORBX_NAT_FREEZE_ENTITY_POSITION]) {
            nat2(oc, ORBX_NAT_FREEZE_ENTITY_POSITION, (uint64_t)ent, 0);
        }
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_COLLISION]) {
            nat3(oc, ORBX_NAT_SET_ENTITY_COLLISION, (uint64_t)ent, 1, 1);
        }
        oc->menu.noclip_pos_valid = 0u;
        oc->menu.noclip_prev_ent  = 0;
        oc->menu.applied_flags &= ~APP_NOCLIP;
    }
}

static float wrap_deg180(float d) {
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static void update_alternate_first_person(OrbxNativeContext *oc, int32_t pl, int32_t ped,
                                          int32_t veh, int veh_changed, uint32_t now_ms) {
    if (!oc || !ped) return;
    (void)veh_changed;

    /* Cinematic camera stock toggle */
    if (oc->menu.cinematic_cam && !(oc->menu.applied_flags & APP_CINEMATIC)) {
        if (oc->menu.natives[ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE]) nat1(oc, ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE, 1u);
        oc->menu.applied_flags |= APP_CINEMATIC;
    } else if (!oc->menu.cinematic_cam && (oc->menu.applied_flags & APP_CINEMATIC)) {
        if (oc->menu.natives[ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE]) nat1(oc, ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE, 0u);
        oc->menu.applied_flags &= ~APP_CINEMATIC;
    }

    /* Track stock view modes periodically */
    if ((oc->menu.main_ticks % 15u) == 0u) {
        if (oc->menu.natives[ORBX_NAT_GET_FOLLOW_PED_CAM_VIEW_MODE]) {
            oc->menu.cam_ped_view_mode = (uint32_t)nat0(oc, ORBX_NAT_GET_FOLLOW_PED_CAM_VIEW_MODE);
        }
        if (oc->menu.natives[ORBX_NAT_GET_FOLLOW_VEHICLE_CAM_VIEW_MODE]) {
            oc->menu.cam_veh_view_mode = (uint32_t)nat0(oc, ORBX_NAT_GET_FOLLOW_VEHICLE_CAM_VIEW_MODE);
        }
    }

    if (oc->menu.cam_anti_shake && oc->menu.natives[ORBX_NAT_STOP_GAMEPLAY_CAM_SHAKING]) {
        nat1(oc, ORBX_NAT_STOP_GAMEPLAY_CAM_SHAKING, 1u);
    }

    /* If Alternate First Person mode is OFF, inactive, or noclip is active: stop AFP cam cleanly */
    if (oc->menu.afp_mode == 0u || oc->menu.cinematic_cam || oc->menu.noclip_on) {
        stop_afp_cam(oc);
        return;
    }

    int can_script_cam = (oc->menu.natives[ORBX_NAT_CREATE_CAM] &&
                          oc->menu.natives[ORBX_NAT_DESTROY_CAM] &&
                          oc->menu.natives[ORBX_NAT_DOES_CAM_EXIST] &&
                          oc->menu.natives[ORBX_NAT_SET_CAM_ACTIVE] &&
                          oc->menu.natives[ORBX_NAT_SET_CAM_COORD] &&
                          oc->menu.natives[ORBX_NAT_SET_CAM_ROT] &&
                          oc->menu.natives[ORBX_NAT_SET_CAM_FOV] &&
                          oc->menu.natives[ORBX_NAT_RENDER_SCRIPT_CAMS] &&
                          oc->menu.natives[ORBX_NAT_GET_GAMEPLAY_CAM_ROT]) ? 1 : 0;
    if (!can_script_cam) {
        stop_afp_cam(oc);
        return;
    }

    /* Check pause menu */
    int paused = (oc->menu.natives[ORBX_NAT_IS_PAUSE_MENU_ACTIVE] &&
                  nat0(oc, ORBX_NAT_IS_PAUSE_MENU_ACTIVE)) ? 1 : 0;
    if (paused) {
        stop_afp_cam(oc);
        return;
    }

    /* Touchpad (INPUT_NEXT_CAMERA=0): Yield cleanly out of Alternate First Person */
    if (oc->menu.afp_cam_active && oc->menu.natives[ORBX_NAT_IS_DISABLED_CONTROL_JUST_PRESSED] &&
        nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_JUST_PRESSED, CTRL_PLAYER, 0u)) {
        stop_afp_cam(oc);
        oc->menu.afp_mode = 0u;
        set_toast(oc, "Camera: Stock GTA V");
        return;
    }

    /* Compute Delta Time (bounded 1ms to 50ms) */
    uint32_t dt_ms = (oc->menu.afp_prev_ms > 0u && now_ms > oc->menu.afp_prev_ms)
        ? (now_ms - oc->menu.afp_prev_ms) : 16u;
    if (dt_ms > 50u) dt_ms = 50u;
    if (dt_ms < 1u)  dt_ms = 1u;
    float dt = (float)dt_ms * 0.001f;
    oc->menu.afp_prev_ms = now_ms;

    /* Aim detection */
    int aiming = (oc->menu.natives[ORBX_NAT_IS_PLAYER_FREE_AIMING] &&
                  nat1(oc, ORBX_NAT_IS_PLAYER_FREE_AIMING, (uint64_t)pl)) ? 1 : 0;
    if (!aiming && !veh && oc->menu.natives[ORBX_NAT_IS_DISABLED_CONTROL_PRESSED]) {
        if (nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_PRESSED, CTRL_PLAYER, IN_AIM)) aiming = 1;
    }

    /* Create scripted camera if needed */
    int newly_activated = 0;
    if (!oc->menu.afp_cam_active || !oc->menu.afp_cam_handle ||
        !nat1(oc, ORBX_NAT_DOES_CAM_EXIST, (uint64_t)oc->menu.afp_cam_handle)) {
        Vec3 init_rot;
        zero_bytes(&init_rot, sizeof(init_rot));
        uint64_t ga[1] = { 2u };
        nat_invoke(oc, ORBX_NAT_GET_GAMEPLAY_CAM_ROT, ga, 1, &init_rot);

        oc->menu.afp_cur_yaw         = init_rot.z;
        oc->menu.afp_cur_pitch       = init_rot.x;
        oc->menu.afp_cur_fov         = kAfpFovValues[(oc->menu.afp_fov_idx < 8u) ? oc->menu.afp_fov_idx : 3u];
        oc->menu.afp_bob_phase       = 0.0f;
        oc->menu.afp_recoil_pitch    = 0.0f;
        oc->menu.afp_recoil_roll     = 0.0f;
        oc->menu.afp_inertia_pitch   = 0.0f;
        oc->menu.afp_prev_speed      = 0.0f;
        oc->menu.afp_attached_entity = 0;
        oc->menu.afp_attached_mode   = 0u;
        oc->menu.afp_attached_bone   = -1;
        oc->menu.afp_smooth_z        = 0.0f;
        oc->menu.afp_veh_off_x       = 0.0f;
        oc->menu.afp_veh_off_y       = 0.0f;
        oc->menu.afp_veh_off_z       = 0.0f;

        oc->menu.afp_cam_handle = (int32_t)nat2(oc, ORBX_NAT_CREATE_CAM, ptr2u("DEFAULT_SCRIPTED_CAMERA"), 1u);
        if (!oc->menu.afp_cam_handle) return;
        oc->menu.afp_cam_active = 1u;
        newly_activated = 1;
    }

    int32_t cam = oc->menu.afp_cam_handle;

    /* 1. Target Look Direction from Controller via GET_GAMEPLAY_CAM_ROT */
    Vec3 game_rot;
    zero_bytes(&game_rot, sizeof(game_rot));
    uint64_t ga[1] = { 2u };
    nat_invoke(oc, ORBX_NAT_GET_GAMEPLAY_CAM_ROT, ga, 1, &game_rot);
    float target_pitch = game_rot.x;
    float target_yaw   = game_rot.z;

    if (target_pitch > 75.0f)  target_pitch = 75.0f;
    if (target_pitch < -80.0f) target_pitch = -80.0f;

    /* 2. Look Direction & Smooth Follow (Bypass lag inside vehicle so steering turns remain responsive) */
    if (oc->menu.afp_smooth_follow == 0u || aiming || veh) {
        oc->menu.afp_cur_yaw   = target_yaw;
        oc->menu.afp_cur_pitch = target_pitch;
    } else {
        float lag_factor = 0.50f;
        if (oc->menu.afp_smooth_follow == 2u) lag_factor = 0.82f; /* Bodycam authentic lag */
        else if (oc->menu.afp_smooth_follow == 3u) lag_factor = 0.90f; /* Heavy cinematic lag */

        float follow_speed = (1.0f - lag_factor) * (dt / 0.016f);
        if (follow_speed > 1.0f) follow_speed = 1.0f;
        float dyaw = wrap_deg180(target_yaw - oc->menu.afp_cur_yaw);
        oc->menu.afp_cur_yaw = wrap_deg180(oc->menu.afp_cur_yaw + dyaw * follow_speed);

        /* Snappy vertical pitch follow prevents downward dip when stopping from a run */
        float pitch_speed = (oc->menu.afp_smooth_follow == 1u) ? 1.0f : (follow_speed * 2.5f);
        if (pitch_speed > 1.0f) pitch_speed = 1.0f;
        oc->menu.afp_cur_pitch += (target_pitch - oc->menu.afp_cur_pitch) * pitch_speed;
    }

    /* 3. Auto-Turn Body (align character heading when camera angle > 45 deg, on foot only) */
    if (!veh && oc->menu.afp_autoturn && oc->menu.natives[ORBX_NAT_GET_ENTITY_HEADING] &&
        oc->menu.natives[ORBX_NAT_SET_ENTITY_HEADING]) {
        float ped_hdg = u2f(nat1(oc, ORBX_NAT_GET_ENTITY_HEADING, (uint64_t)ped));
        float diff = wrap_deg180(oc->menu.afp_cur_yaw - ped_hdg);
        if (diff > 45.0f || diff < -45.0f || aiming) {
            float turn_speed = aiming ? 12.0f : 6.0f;
            float new_hdg = wrap_deg180(ped_hdg + diff * (turn_speed * dt));
            nat2(oc, ORBX_NAT_SET_ENTITY_HEADING, (uint64_t)ped, f2u(new_hdg));
        }
        if (oc->menu.natives[ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_HEADING]) {
            nat1(oc, ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_HEADING, f2u(0.0f));
        }
    }

    /* 4. Weapon Recoil Kick & Spring Return */
    if (oc->menu.afp_recoil > 0u && oc->menu.natives[ORBX_NAT_IS_PED_SHOOTING]) {
        if (nat1(oc, ORBX_NAT_IS_PED_SHOOTING, (uint64_t)ped)) {
            float kick_mult = (oc->menu.afp_recoil == 1u) ? 0.6f :
                              (oc->menu.afp_recoil == 2u) ? 1.2f : 2.4f;
            oc->menu.afp_recoil_pitch += 1.8f * kick_mult;
            float jolt = ((oc->menu.main_ticks & 1) ? 1.0f : -1.0f) * 0.75f * kick_mult;
            oc->menu.afp_recoil_roll += jolt;
        }
    }
    oc->menu.afp_recoil_pitch *= 0.82f;
    oc->menu.afp_recoil_roll  *= 0.82f;

    /* 5. Entity Speed & Acceleration */
    float speed = 0.0f;
    if (oc->menu.natives[ORBX_NAT_GET_ENTITY_SPEED]) {
        speed = u2f(nat1(oc, ORBX_NAT_GET_ENTITY_SPEED, (uint64_t)(veh ? veh : ped)));
    }
    float dspeed = speed - oc->menu.afp_prev_speed;
    float accel  = dspeed / (dt > 0.001f ? dt : 0.016f);

    /* Vehicle Inertia (pitch tilt on accel/braking) */
    if (veh && oc->menu.afp_inertia) {
        if (accel < -2.0f) {
            oc->menu.afp_inertia_pitch += accel * 0.06f;
            if (oc->menu.afp_inertia_pitch < -5.0f) oc->menu.afp_inertia_pitch = -5.0f;
        } else if (accel > 2.0f) {
            oc->menu.afp_inertia_pitch += accel * 0.03f;
            if (oc->menu.afp_inertia_pitch > 3.5f) oc->menu.afp_inertia_pitch = 3.5f;
        }
    }
    oc->menu.afp_inertia_pitch *= 0.85f;
    oc->menu.afp_prev_speed = speed;

    /* 6. Procedural Head Bobbing (Silky smooth, continuous, zero perspective drop) */
    float bob_x = 0.0f, bob_z = 0.0f;
    float bob_roll = 0.0f;
    float bob_pitch = 0.0f;
    if (oc->menu.afp_headbob > 0u && !veh) {
        /* Low = 0.25x, Normal = 0.50x, Cinematic = 0.85x */
        float bob_scale = (oc->menu.afp_headbob == 1u) ? 0.25f :
                          (oc->menu.afp_headbob == 2u) ? 0.50f : 0.85f;
        if (aiming) bob_scale *= 0.15f;

        /* Continuous walk weight smoothly ramping between 0.4 m/s and 1.2 m/s */
        float walk_weight = 0.0f;
        if (speed > 0.4f) {
            walk_weight = (speed - 0.4f) / 0.8f;
            if (walk_weight > 1.0f) walk_weight = 1.0f;
        }

        /* Subtle idle breathing sway (standing still) */
        float breath_phase = (float)(now_ms % 4000u) * (6.2831853f / 4000.0f);
        float breath_z = pic_sinf(breath_phase) * 0.0015f * bob_scale;
        float breath_x = pic_cosf(breath_phase * 0.5f) * 0.0010f * bob_scale;

        /* Walking / running stride gait */
        float stride_z = 0.0f;
        float stride_x = 0.0f;
        float stride_roll = 0.0f;
        float stride_pitch = 0.0f;

        if (speed > 0.4f) {
            float cadence = 1.5f + (speed * 0.18f);
            if (cadence > 2.8f) cadence = 2.8f;
            oc->menu.afp_bob_phase += dt * cadence * 6.2831853f;
            if (oc->menu.afp_bob_phase > 62.831853f) oc->menu.afp_bob_phase -= 62.831853f;

            float sin_p = pic_sinf(oc->menu.afp_bob_phase);
            float cos_p = pic_cosf(oc->menu.afp_bob_phase * 0.5f);

            /* Parabolic bounce without DC offset */
            stride_z     = (sin_p * sin_p) * 0.0035f * bob_scale;
            stride_x     = (cos_p * 0.0030f) * bob_scale;
            stride_roll  = (cos_p * 0.10f) * bob_scale;
            stride_pitch = (sin_p * sin_p) * 0.05f * bob_scale;
        }

        bob_z     = (breath_z * (1.0f - walk_weight)) + (stride_z * walk_weight);
        bob_x     = (breath_x * (1.0f - walk_weight)) + (stride_x * walk_weight);
        bob_roll  = stride_roll * walk_weight;
        bob_pitch = stride_pitch * walk_weight;
    }

    uint32_t mode = oc->menu.afp_mode;

    /* Get player head bone world position (SKEL_Head = 31086, Spine3 = 24818) */
    Vec3 head_w;
    zero_bytes(&head_w, sizeof(head_w));
    int bone_id = (mode == 2u || mode == 3u) ? 24818 : 31086;
    if (oc->menu.natives[ORBX_NAT_GET_PED_BONE_COORDS]) {
        uint64_t ba[5] = { (uint64_t)ped, (uint64_t)bone_id, f2u(0.0f), f2u(0.0f), f2u(0.0f) };
        nat_invoke(oc, ORBX_NAT_GET_PED_BONE_COORDS, ba, 5, &head_w);
    }
    if (head_w.x == 0.0f && head_w.y == 0.0f && head_w.z == 0.0f &&
        oc->menu.natives[ORBX_NAT_GET_ENTITY_COORDS]) {
        uint64_t ea[2] = { (uint64_t)ped, 1u };
        nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ea, 2, &head_w);
        head_w.z += (mode == 2u) ? 0.25f : 0.68f;
    }

    if (veh) {
        /* ================================================================
         * INSIDE VEHICLE: Attached to vehicle entity for 60fps physics interpolation.
         * Offset is placed safely in front of driver's head (28cm forward) so
         * when braking hard and the driver pitches forward, the head/body NEVER
         * reaches or overlaps the camera.
         * ================================================================ */
        float off_x = 0.0f, off_y = 0.0f, off_z = 0.0f;

        if (mode == 4u) {
            /* Dash Cam: centered at windshield */
            off_x = 0.0f;
            off_y = 0.45f;
            off_z = 0.38f;
        } else {
            /* Convert current head bone world coords to vehicle local space */
            Vec3 local_pos;
            zero_bytes(&local_pos, sizeof(local_pos));
            int got_offset = 0;

            if (oc->menu.natives[ORBX_NAT_GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS] &&
                (head_w.x != 0.0f || head_w.y != 0.0f || head_w.z != 0.0f)) {
                uint64_t oa[4] = { (uint64_t)veh, f2u(head_w.x), f2u(head_w.y), f2u(head_w.z) };
                nat_invoke(oc, ORBX_NAT_GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS, oa, 4, &local_pos);
                if (local_pos.x != 0.0f || local_pos.y != 0.0f || local_pos.z != 0.0f) {
                    off_x = local_pos.x;
                    off_y = local_pos.y;
                    off_z = local_pos.z;
                    got_offset = 1;
                }
            }

            if (!got_offset || off_z < 0.1f || off_z > 1.8f) {
                off_x = -0.38f; /* typical driver seat left side */
                off_y = -0.10f;
                off_z =  0.55f;
            }

            /* Extra forward clearance during hard braking deceleration */
            float brake_fwd = 0.0f;
            if (accel < -1.5f) {
                brake_fwd = (-accel) * 0.006f;
                if (brake_fwd > 0.12f) brake_fwd = 0.12f;
            }

            if (mode == 1u) {
                /* Alt First-Person: aligned right with driver eyes, 28cm forward to eliminate braking overlap */
                off_y += 0.28f + brake_fwd;
                off_z += 0.04f;
            } else if (mode == 2u) {
                /* Tactical Bodycam: chest level */
                off_y += 0.32f + brake_fwd;
                off_z -= 0.12f;
            } else if (mode == 3u) {
                /* Shoulder GoPro */
                off_x += 0.18f;
                off_y += 0.14f + brake_fwd;
                off_z += 0.04f;
            } else if (mode == 5u) {
                /* Helmet Cam */
                off_y += 0.22f + brake_fwd;
                off_z += 0.16f;
            }
        }

        int need_attach = (oc->menu.afp_attached_entity != veh ||
                           oc->menu.afp_attached_mode   != mode ||
                           newly_activated || veh_changed);

        if (need_attach) {
            if (oc->menu.afp_attached_entity && oc->menu.natives[ORBX_NAT_DETACH_CAM]) {
                nat1(oc, ORBX_NAT_DETACH_CAM, (uint64_t)cam);
            }
            if (oc->menu.natives[ORBX_NAT_ATTACH_CAM_TO_ENTITY]) {
                uint64_t aargs[6] = {
                    (uint64_t)cam,
                    (uint64_t)veh,
                    f2u(off_x), f2u(off_y), f2u(off_z),
                    1u /* isRelative */
                };
                nat_invoke(oc, ORBX_NAT_ATTACH_CAM_TO_ENTITY, aargs, 6, NULL);
                oc->menu.afp_attached_entity = veh;
                oc->menu.afp_attached_mode   = mode;
                oc->menu.afp_veh_off_x       = off_x;
                oc->menu.afp_veh_off_y       = off_y;
                oc->menu.afp_veh_off_z       = off_z;
            }
        }
    } else {
        /* ================================================================
         * ON FOOT: Detach from vehicle cleanly if we just exited.
         * Direct 1:1 head-bone positioning with eye-level forward offset.
         * Seamlessly follows head during vehicle exit animation onto the street.
         * NO sluggish z-lag, NO perspective dipping when stopping from run.
         * ================================================================ */
        if (oc->menu.afp_attached_entity != 0) {
            if (oc->menu.natives[ORBX_NAT_DETACH_CAM]) {
                nat1(oc, ORBX_NAT_DETACH_CAM, (uint64_t)cam);
            }
            oc->menu.afp_attached_entity = 0;
            oc->menu.afp_attached_mode   = 0u;
            oc->menu.afp_smooth_z        = 0.0f;
        }

        float yaw_r = oc->menu.afp_cur_yaw * 0.0174532925f;
        float fwd_x = -pic_sinf(yaw_r);
        float fwd_y =  pic_cosf(yaw_r);
        float rgt_x =  pic_cosf(yaw_r);
        float rgt_y =  pic_sinf(yaw_r);

        float off_fwd = 0.18f, off_rgt = 0.0f, off_up = 0.02f;
        if (mode == 1u) {
            /* Alt First-Person: at eye line, 18cm forward in front of nose/face */
            off_fwd = 0.18f;
            off_up  = 0.02f;
        } else if (mode == 2u) {
            /* Tactical Bodycam: chest height */
            off_fwd = 0.22f;
            off_up  = -0.15f;
        } else if (mode == 3u) {
            /* Shoulder GoPro */
            off_fwd = -0.10f;
            off_rgt =  0.22f;
            off_up  =  0.08f;
        } else if (mode == 4u) {
            /* Dash Cam on foot */
            off_fwd = 0.20f;
            off_up  = -0.10f;
        } else if (mode == 5u) {
            /* Helmet Cam */
            off_fwd = 0.10f;
            off_up  = 0.14f;
        }

        Vec3 cam_pos;
        cam_pos.x = head_w.x + (fwd_x * off_fwd) + (rgt_x * (off_rgt + bob_x));
        cam_pos.y = head_w.y + (fwd_y * off_fwd) + (rgt_y * (off_rgt + bob_x));
        cam_pos.z = head_w.z + off_up + bob_z;

        uint64_t cargs[4] = { (uint64_t)cam, f2u(cam_pos.x), f2u(cam_pos.y), f2u(cam_pos.z) };
        nat_invoke(oc, ORBX_NAT_SET_CAM_COORD, cargs, 4, NULL);
    }

    /* 7. ADS Aim Zoom */
    float base_fov = kAfpFovValues[(oc->menu.afp_fov_idx < 8u) ? oc->menu.afp_fov_idx : 3u];
    float target_fov = base_fov;
    if (aiming && oc->menu.afp_ads_zoom > 0u && oc->menu.afp_ads_zoom < 4u) {
        target_fov = base_fov - kAfpAdsZoomDeg[oc->menu.afp_ads_zoom];
        if (target_fov < 35.0f) target_fov = 35.0f;
    }
    oc->menu.afp_cur_fov += (target_fov - oc->menu.afp_cur_fov) * 0.25f;

    /* 8. Apply Camera Rotation, FOV & Near Clip */
    float final_pitch = oc->menu.afp_cur_pitch + oc->menu.afp_recoil_pitch + oc->menu.afp_inertia_pitch + bob_pitch;
    float final_roll  = oc->menu.afp_recoil_roll + bob_roll;
    float final_yaw   = oc->menu.afp_cur_yaw;

    uint64_t rotargs[5] = {
        (uint64_t)cam,
        f2u(final_pitch), f2u(final_roll), f2u(final_yaw),
        2u
    };
    nat_invoke(oc, ORBX_NAT_SET_CAM_ROT, rotargs, 5, NULL);
    nat2(oc, ORBX_NAT_SET_CAM_FOV, (uint64_t)cam, f2u(oc->menu.afp_cur_fov));

    if (oc->menu.natives[ORBX_NAT_SET_CAM_NEAR_CLIP]) {
        nat2(oc, ORBX_NAT_SET_CAM_NEAR_CLIP, (uint64_t)cam, f2u(0.05f));
    }

    if (newly_activated) {
        nat2(oc, ORBX_NAT_SET_CAM_ACTIVE, (uint64_t)cam, 1u);
        uint64_t rargs[5] = { 1u, 0u, 0u, 1u, 0u };
        nat_invoke(oc, ORBX_NAT_RENDER_SCRIPT_CAMS, rargs, 5, NULL);
    }
}

/* ============================================================================
 * Realistic Fuel System & Physics Rope Nozzle Refueling (GTA VI Immersive)
 * ============================================================================ */

static const uint32_t kGasPumpHashes[7] = {
    0x4FD621BCu, /* prop_gas_pump_1a */
    0x64FF4C0Eu, /* prop_gas_pump_1b */
    0x7339E883u, /* prop_gas_pump_1c */
    0x885C12C7u, /* prop_gas_pump_1d */
    0xE40106F5u, /* prop_gas_pump_old2 */
    0xF62C2B4Bu, /* prop_gas_pump_old3 */
    0xE469F8B3u  /* prop_vintage_pump */
};
static const uint32_t kNozzleModelHash   = 0x8C820201u; /* prop_cs_fuel_nozle */
static const uint32_t kWeaponUnarmedHash = 0xA2719263u; /* weapon_unarmed */

static void cleanup_fuel_hose(OrbxNativeContext *oc) {
    if (oc->menu.fuel_rope_id) {
        int32_t r = oc->menu.fuel_rope_id;
        if (oc->menu.natives[ORBX_NAT_DELETE_ROPE]) {
            uint64_t dr[1] = { (uint64_t)(uintptr_t)&r };
            nat_invoke(oc, ORBX_NAT_DELETE_ROPE, dr, 1, NULL);
        }
        oc->menu.fuel_rope_id = 0;
    }
    if (oc->menu.fuel_nozzle_obj) {
        int32_t o = oc->menu.fuel_nozzle_obj;
        if (oc->menu.natives[ORBX_NAT_DELETE_OBJECT]) {
            uint64_t do_args[1] = { (uint64_t)(uintptr_t)&o };
            nat_invoke(oc, ORBX_NAT_DELETE_OBJECT, do_args, 1, NULL);
        }
        oc->menu.fuel_nozzle_obj = 0;
    }
    oc->menu.fuel_stage = 0u;
    oc->menu.fuel_pump_obj = 0;
}

static void update_realistic_fuel_system(OrbxNativeContext *oc, int32_t pl, int32_t ped,
                                        int32_t veh, int veh_changed, uint32_t now_ms) {
    (void)pl;
    if (!oc->menu.fuel_system_on) {
        if (oc->menu.fuel_stage != 0u) {
            cleanup_fuel_hose(oc);
        }
        return;
    }

    /* Tracked vehicle fuel level initialization */
    if (oc->menu.fuel_level <= 0.0f && oc->menu.fuel_veh_handle == 0) {
        oc->menu.fuel_level = 100.0f;
    }
    if (veh_changed && veh) {
        if (oc->menu.fuel_veh_handle != veh) {
            oc->menu.fuel_veh_handle = veh;
            if (oc->menu.fuel_level <= 0.0f) {
                oc->menu.fuel_level = 80.0f;
            }
            oc->menu.fuel_last_burn_ms = now_ms;
        }
    }

    /* 1. Dynamic Fuel Consumption (when driving) */
    if (veh && nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)veh)) {
        if (!oc->menu.fuel_last_burn_ms) oc->menu.fuel_last_burn_ms = now_ms;
        uint32_t dt_ms = now_ms - oc->menu.fuel_last_burn_ms;
        if (dt_ms > 2000u) dt_ms = 100u;
        oc->menu.fuel_last_burn_ms = now_ms;
        float dt_sec = (float)dt_ms * 0.001f;

        int throttle = 0;
        if (oc->menu.natives[ORBX_NAT_IS_CONTROL_PRESSED]) {
            throttle = nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, 0, 71) ? 1 : 0; /* R2 / Accelerate */
        }
        float spd_ms = 0.0f;
        if (oc->menu.natives[ORBX_NAT_GET_ENTITY_SPEED]) {
            spd_ms = u2f(nat1(oc, ORBX_NAT_GET_ENTITY_SPEED, (uint64_t)veh));
        }
        if (spd_ms < 0.0f) spd_ms = 0.0f;
        float spd_kph = spd_ms * 3.6f;

        /* Realistic burn rates: Idle=~0.005%/s, Cruise=~0.025%/s, High-RPM Full Throttle=~0.080%/s */
        float rpm = (oc->menu.speedo_needle_rpm > 0.05f) ? oc->menu.speedo_needle_rpm : 0.15f;
        float burn_rate = 0.005f;
        if (throttle) {
            burn_rate += 0.040f * rpm;
        }
        if (spd_kph > 10.0f) {
            burn_rate += (spd_kph * 0.00020f);
        }
        oc->menu.fuel_level -= (burn_rate * dt_sec);
        if (oc->menu.fuel_level < 0.0f) oc->menu.fuel_level = 0.0f;

        /* Engine Cut / Stall at 0.0% */
        if (oc->menu.fuel_level <= 0.0f) {
            if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_ENGINE_ON]) {
                nat4(oc, ORBX_NAT_SET_VEHICLE_ENGINE_ON, (uint64_t)veh, 0, 1, 1);
            }
            if (spd_ms > 0.5f && oc->menu.natives[ORBX_NAT_SET_VEHICLE_FORWARD_SPEED]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_FORWARD_SPEED, (uint64_t)veh, f2u(spd_ms * 0.96f));
            }
            if ((now_ms - oc->menu.fuel_last_stall_ms) > 6000u) {
                oc->menu.fuel_last_stall_ms = now_ms;
                set_toast(oc, "Vehicle Out of Fuel! Stalled.");
            }
        }
    }

    /* 2. On-Foot / Gas Pump Interaction */
    Vec3 ped_pos;
    zero_bytes(&ped_pos, sizeof(ped_pos));
    uint64_t ga[2] = { (uint64_t)ped, 1u };
    nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ga, 2, &ped_pos);

    /* Search for nearest gas pump prop */
    int32_t near_pump = 0;
    float best_pump_d2 = 999.0f;
    Vec3 best_pump_pos;
    zero_bytes(&best_pump_pos, sizeof(best_pump_pos));

    if (oc->menu.natives[ORBX_NAT_GET_CLOSEST_OBJECT_OF_TYPE]) {
        for (size_t i = 0; i < 7; i++) {
            uint64_t oargs[8] = {
                f2u(ped_pos.x), f2u(ped_pos.y), f2u(ped_pos.z),
                f2u(7.0f), (uint64_t)kGasPumpHashes[i], 0, 0, 0
            };
            int32_t p = (int32_t)nat_invoke(oc, ORBX_NAT_GET_CLOSEST_OBJECT_OF_TYPE, oargs, 8, NULL);
            if (p && nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)p)) {
                Vec3 ppos;
                zero_bytes(&ppos, sizeof(ppos));
                uint64_t pa[2] = { (uint64_t)p, 1u };
                nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, pa, 2, &ppos);
                float dx = ped_pos.x - ppos.x, dy = ped_pos.y - ppos.y, dz = ped_pos.z - ppos.z;
                float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best_pump_d2) {
                    best_pump_d2 = d2;
                    near_pump = p;
                    best_pump_pos = ppos;
                }
            }
        }
    }

    uint32_t raw_live = oc->menu.pad_buttons_live;
    uint32_t raw_prev = oc->menu.prev_live_btn_mask;
    uint32_t raw_edge = raw_live & ~raw_prev;

    /* Accept both D-Pad Right and D-Pad Left for refueling interactions */
    const uint32_t kFuelBtns = SCE_PAD_BTN_RIGHT | SCE_PAD_BTN_LEFT;
    int btn_edge = ((raw_edge & kFuelBtns) != 0u) ||
                   (oc->menu.natives[ORBX_NAT_IS_DISABLED_CONTROL_JUST_PRESSED] &&
                    (nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 51) ||
                     nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_JUST_PRESSED, 0, 74)));
    int btn_held = ((raw_live & kFuelBtns) != 0u) ||
                   (oc->menu.natives[ORBX_NAT_IS_CONTROL_PRESSED] &&
                    (nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, 0, 51) ||
                     nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, 0, 74)));

    /* Debounce: ignore edge triggers while transition cooldown is active */
    int act_edge = (now_ms >= oc->menu.fuel_cooldown_until_ms) ? btn_edge : 0;
    int act_held = btn_held;

    /* Case A: In-Vehicle Quick Refuel (player sitting stopped in car near gas pump) */
    if (veh && near_pump && best_pump_d2 <= (5.0f * 5.0f)) {
        float spd_ms = 0.0f;
        if (oc->menu.natives[ORBX_NAT_GET_ENTITY_SPEED]) {
            spd_ms = u2f(nat1(oc, ORBX_NAT_GET_ENTITY_SPEED, (uint64_t)veh));
        }
        if (spd_ms < 1.0f) {
            ui_draw_text(oc, "[Hold D-Pad] Quick Refuel", 0.500f, 0.887f, 0.32f, 0, 1, 1.0f, 0, 230, 255, 255);
            if (act_held) {
                oc->menu.fuel_level += 0.45f;
                if (oc->menu.fuel_level >= 100.0f) {
                    oc->menu.fuel_level = 100.0f;
                    set_toast(oc, "Vehicle Tank Full (100%)");
                }
            }
        }
    }

    /* On-Foot Interactive Physics Hose & Nozzle Refueling Workflow */
    if (!veh) {
        /* Stage 0: On-Foot near Pump -> Take Nozzle */
        if (oc->menu.fuel_stage == 0u) {
            if (near_pump && best_pump_d2 <= (2.6f * 2.6f)) {
                ui_draw_text(oc, "[D-Pad] Take Fuel Nozzle", 0.500f, 0.887f, 0.32f, 0, 1, 1.0f, 0, 220, 255, 255);
                if (act_edge) {
                    nat1(oc, ORBX_NAT_REQUEST_MODEL, (uint64_t)kNozzleModelHash);
                    uint64_t co_args[7] = {
                        (uint64_t)kNozzleModelHash,
                        f2u(ped_pos.x), f2u(ped_pos.y), f2u(ped_pos.z + 0.5f),
                        0, 0, 0
                    };
                    int32_t noz = (int32_t)nat_invoke(oc, ORBX_NAT_CREATE_OBJECT, co_args, 7, NULL);
                    if (noz) {
                        if (oc->menu.natives[ORBX_NAT_SET_CURRENT_PED_WEAPON]) {
                            nat3(oc, ORBX_NAT_SET_CURRENT_PED_WEAPON, (uint64_t)ped, (uint64_t)kWeaponUnarmedHash, 1);
                        }
                        int32_t bone = (int32_t)nat2(oc, ORBX_NAT_GET_PED_BONE_INDEX, (uint64_t)ped, 60309u); /* SKEL_L_Hand */
                        uint64_t att_hand[16] = {
                            (uint64_t)noz, (uint64_t)ped, (uint64_t)bone,
                            f2u(0.055f), f2u(0.05f), f2u(0.0f),
                            f2u(-50.0f), f2u(-90.0f), f2u(-50.0f),
                            0, 0, 0, 0, 2, 1, 0
                        };
                        nat_invoke(oc, ORBX_NAT_ATTACH_ENTITY_TO_ENTITY, att_hand, 16, NULL);

                        if (oc->menu.natives[ORBX_NAT_ROPE_LOAD_TEXTURES]) {
                            nat0(oc, ORBX_NAT_ROPE_LOAD_TEXTURES);
                        }
                        uint64_t r_args[17] = {
                            f2u(best_pump_pos.x), f2u(best_pump_pos.y), f2u(best_pump_pos.z + 1.30f),
                            0, 0, 0, f2u(2.2f), 3u, f2u(7.0f), f2u(0.5f), f2u(0.5f),
                            0, 0, 1u, f2u(1.0f), 0, 0
                        };
                        int32_t rope = (int32_t)nat_invoke(oc, ORBX_NAT_ADD_ROPE, r_args, 17, NULL);
                        if (rope) {
                            uint64_t ro_args[14] = {
                                (uint64_t)rope, (uint64_t)near_pump, (uint64_t)noz,
                                0, 0, f2u(1.30f), 0, 0, f2u(-0.18f),
                                f2u(7.0f), 0, 0, 0, 0
                            };
                            nat_invoke(oc, ORBX_NAT_ATTACH_ENTITIES_TO_ROPE, ro_args, 14, NULL);
                        }
                        oc->menu.fuel_nozzle_obj = noz;
                        oc->menu.fuel_rope_id    = rope;
                        oc->menu.fuel_pump_obj   = near_pump;
                        oc->menu.fuel_stage      = 1u;
                        oc->menu.fuel_cooldown_until_ms = now_ms + 1200u;
                        set_toast(oc, "Fuel Nozzle in Hand. Walk to Vehicle Cap.");
                        play_ui_sound(oc, "SELECT");
                        return;
                    }
                }
            }
        }
        /* Stage 1: Nozzle in Hand -> Walk to Car or Return to Pump */
        else if (oc->menu.fuel_stage == 1u) {
            /* Check if walked too far from pump (> 7.5m) */
            if (oc->menu.fuel_pump_obj && nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)oc->menu.fuel_pump_obj)) {
                Vec3 ppos;
                zero_bytes(&ppos, sizeof(ppos));
                uint64_t pa[2] = { (uint64_t)oc->menu.fuel_pump_obj, 1u };
                nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, pa, 2, &ppos);
                float dx = ped_pos.x - ppos.x, dy = ped_pos.y - ppos.y, dz = ped_pos.z - ppos.z;
                if ((dx * dx + dy * dy + dz * dz) > (7.5f * 7.5f)) {
                    cleanup_fuel_hose(oc);
                    set_toast(oc, "Fuel Hose Snapped! Walked too far.");
                    return;
                }
            }

            /* Priority 1: Vehicle within reach -> Insert Nozzle in Car */
            uint64_t cargs6[6] = { f2u(ped_pos.x), f2u(ped_pos.y), f2u(ped_pos.z), f2u(3.2f), 0u, 70u };
            int32_t close_veh = (int32_t)nat_invoke(oc, ORBX_NAT_GET_CLOSEST_VEHICLE, cargs6, 6, NULL);
            if (close_veh && nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)close_veh)) {
                ui_draw_text(oc, "[D-Pad] Insert Nozzle in Car", 0.500f, 0.887f, 0.32f, 0, 1, 1.0f, 0, 240, 255, 255);
                if (act_edge) {
                    if (oc->menu.natives[ORBX_NAT_DETACH_ENTITY]) {
                        nat3(oc, ORBX_NAT_DETACH_ENTITY, (uint64_t)oc->menu.fuel_nozzle_obj, 1, 0);
                    }
                    uint64_t att_car[16] = {
                        (uint64_t)oc->menu.fuel_nozzle_obj, (uint64_t)close_veh, 0,
                        f2u(-0.95f), f2u(-1.30f), f2u(0.40f),
                        f2u(-45.0f), 0, f2u(-90.0f),
                        0, 0, 0, 0, 2, 1, 0
                    };
                    nat_invoke(oc, ORBX_NAT_ATTACH_ENTITY_TO_ENTITY, att_car, 16, NULL);
                    oc->menu.fuel_veh_handle = close_veh;
                    oc->menu.fuel_stage      = 2u;
                    oc->menu.fuel_cooldown_until_ms = now_ms + 1200u;
                    set_toast(oc, "Nozzle Inserted. Refueling Vehicle...");
                    play_ui_sound(oc, "SELECT");
                    return;
                }
            }
            /* Priority 2: Near Pump (and not targeting vehicle) -> Hang Up Nozzle */
            else if (near_pump && best_pump_d2 <= (2.4f * 2.4f)) {
                ui_draw_text(oc, "[Hold D-Pad] Hang Up Nozzle", 0.500f, 0.887f, 0.32f, 0, 1, 1.0f, 0, 220, 255, 255);
                if ((act_held || act_edge) && now_ms >= oc->menu.fuel_cooldown_until_ms) {
                    cleanup_fuel_hose(oc);
                    oc->menu.fuel_cooldown_until_ms = now_ms + 1200u;
                    set_toast(oc, "Fuel Nozzle Returned to Pump");
                    play_ui_sound(oc, "BACK");
                    return;
                }
            }
        }
        /* Stage 2: Nozzle in Car -> Refueling in progress */
        else if (oc->menu.fuel_stage == 2u) {
            /* Check if car moved too far from pump */
            if (oc->menu.fuel_pump_obj && nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)oc->menu.fuel_pump_obj)) {
                Vec3 ppos;
                zero_bytes(&ppos, sizeof(ppos));
                uint64_t pa[2] = { (uint64_t)oc->menu.fuel_pump_obj, 1u };
                nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, pa, 2, &ppos);
                float dx = ped_pos.x - ppos.x, dy = ped_pos.y - ppos.y, dz = ped_pos.z - ppos.z;
                if ((dx * dx + dy * dy + dz * dz) > (7.5f * 7.5f)) {
                    cleanup_fuel_hose(oc);
                    set_toast(oc, "Fuel Hose Snapped Off Vehicle!");
                    return;
                }
            }

            /* Tank fills smoothly */
            oc->menu.fuel_refuel_tick++;
            if ((oc->menu.fuel_refuel_tick % 2u) == 0u) {
                if (oc->menu.fuel_level < 100.0f) {
                    oc->menu.fuel_level += 0.35f;
                    if (oc->menu.fuel_level >= 100.0f) {
                        oc->menu.fuel_level = 100.0f;
                        set_toast(oc, "Vehicle Tank Full (100%)");
                    }
                }
            }

            char fbuf[48];
            size_t fp = orbx_pic_fmt_str(fbuf, 0, sizeof(fbuf) - 1, "[D-Pad] Remove Nozzle (Fuel: ");
            fp = orbx_pic_fmt_u64(fbuf, fp, sizeof(fbuf) - 1, (uint64_t)oc->menu.fuel_level);
            fp = orbx_pic_fmt_str(fbuf, fp, sizeof(fbuf) - 1, "%)");
            fbuf[fp] = '\0';
            ui_draw_text(oc, fbuf, 0.500f, 0.887f, 0.32f, 0, 1, 1.0f, 0, 240, 255, 255);

            if (act_edge) {
                if (oc->menu.natives[ORBX_NAT_DETACH_ENTITY]) {
                    nat3(oc, ORBX_NAT_DETACH_ENTITY, (uint64_t)oc->menu.fuel_nozzle_obj, 1, 0);
                }
                int32_t bone = (int32_t)nat2(oc, ORBX_NAT_GET_PED_BONE_INDEX, (uint64_t)ped, 60309u); /* SKEL_L_Hand */
                uint64_t att_hand[16] = {
                    (uint64_t)oc->menu.fuel_nozzle_obj, (uint64_t)ped, (uint64_t)bone,
                    f2u(0.055f), f2u(0.05f), f2u(0.0f),
                    f2u(-50.0f), f2u(-90.0f), f2u(-50.0f),
                    0, 0, 0, 0, 2, 1, 0
                };
                nat_invoke(oc, ORBX_NAT_ATTACH_ENTITY_TO_ENTITY, att_hand, 16, NULL);
                oc->menu.fuel_stage = 1u;
                oc->menu.fuel_cooldown_until_ms = now_ms + 1200u;
                set_toast(oc, "Nozzle Removed. Walk Back to Hang Up.");
                play_ui_sound(oc, "SELECT");
                return;
            }
        }
    }
}

/* ============================================================================
 * Calmer Police AI (GTA VI Law Enforcement Dispatch)
 * ============================================================================ */

static void update_calmer_police_ai(OrbxNativeContext *oc, int32_t pl) {
    if (!oc->menu.calmer_police_on) {
        if (oc->menu.calmer_last_wanted != 0u) {
            oc->menu.calmer_last_wanted = 0u;
            if (oc->menu.natives[ORBX_NAT_RESET_DISPATCH_IDEAL_SPAWN_DISTANCE]) {
                nat0(oc, ORBX_NAT_RESET_DISPATCH_IDEAL_SPAWN_DISTANCE);
            }
            if (oc->menu.natives[ORBX_NAT_SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER]) {
                nat2(oc, ORBX_NAT_SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER, 1u, f2u(1.0f));
            }
        }
        return;
    }

    if (oc->menu.natives[ORBX_NAT_GET_PLAYER_WANTED_LEVEL]) {
        uint32_t wl = (uint32_t)nat1(oc, ORBX_NAT_GET_PLAYER_WANTED_LEVEL, (uint64_t)pl);
        if (wl > 0u) {
            if (oc->menu.natives[ORBX_NAT_SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER]) {
                nat2(oc, ORBX_NAT_SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER, 1u, f2u(2.2f));
            }
            if (oc->menu.natives[ORBX_NAT_SET_DISPATCH_IDEAL_SPAWN_DISTANCE]) {
                nat1(oc, ORBX_NAT_SET_DISPATCH_IDEAL_SPAWN_DISTANCE, f2u(280.0f));
            }
            oc->menu.calmer_last_wanted = wl;
        } else if (oc->menu.calmer_last_wanted != 0u) {
            oc->menu.calmer_last_wanted = 0u;
            if (oc->menu.natives[ORBX_NAT_RESET_DISPATCH_IDEAL_SPAWN_DISTANCE]) {
                nat0(oc, ORBX_NAT_RESET_DISPATCH_IDEAL_SPAWN_DISTANCE);
            }
            if (oc->menu.natives[ORBX_NAT_SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER]) {
                nat2(oc, ORBX_NAT_SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER, 1u, f2u(1.0f));
            }
        }
    }
}

static void apply_continuous_features(OrbxNativeContext *oc, uint32_t now_ms) {
    if (oc->menu.quick_heal_req)   { oc->menu.quick_heal_req = 0u;   act_heal_armor(oc); }
    if (oc->menu.quick_repair_req) { oc->menu.quick_repair_req = 0u; act_fix_vehicle(oc); }

    int32_t pl  = (int32_t)nat0(oc, ORBX_NAT_PLAYER_ID);
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    if (!ped) return;
    if (oc->menu.natives[ORBX_NAT_DOES_ENTITY_EXIST] &&
        !nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)ped)) {
        return;
    }
    int32_t veh = get_current_veh(oc, ped);

    int ped_changed = (ped != oc->menu.last_applied_ped) ? 1 : 0;
    int veh_changed = (veh != oc->menu.last_applied_veh) ? 1 : 0;
    int periodic    = ((oc->menu.main_ticks % 120u) == 0u) ? 1 : 0;

    /* God Mode */
    if (oc->menu.godmode) {
        if (!(oc->menu.applied_flags & APP_GOD) || ped_changed || periodic) {
            nat2(oc, ORBX_NAT_SET_PLAYER_INVINCIBLE, (uint64_t)pl, 1);
            nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)ped, 1);
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_PROOFS]) {
                nat9(oc, ORBX_NAT_SET_ENTITY_PROOFS, (uint64_t)ped, 1, 1, 1, 1, 1, 1, 1, 1);
            }
            oc->menu.applied_flags |= APP_GOD;
        }
    } else if (oc->menu.applied_flags & APP_GOD) {
        nat2(oc, ORBX_NAT_SET_PLAYER_INVINCIBLE, (uint64_t)pl, 0);
        nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)ped, 0);
        if (oc->menu.natives[ORBX_NAT_SET_ENTITY_PROOFS]) {
            nat9(oc, ORBX_NAT_SET_ENTITY_PROOFS, (uint64_t)ped, 0, 0, 0, 0, 0, 0, 0, 0);
        }
        oc->menu.applied_flags &= ~APP_GOD;
    }

    /* Invisibility */
    if (oc->menu.invisible) {
        if (!(oc->menu.applied_flags & APP_INVISIBLE) || ped_changed || periodic) {
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_VISIBLE]) {
                nat3(oc, ORBX_NAT_SET_ENTITY_VISIBLE, (uint64_t)ped, 0, 0);
            }
            oc->menu.applied_flags |= APP_INVISIBLE;
        }
    } else {
        if ((oc->menu.applied_flags & APP_INVISIBLE) || ped_changed || (oc->menu.main_ticks < 300u && (oc->menu.main_ticks % 30u) == 0u)) {
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_VISIBLE]) {
                nat3(oc, ORBX_NAT_SET_ENTITY_VISIBLE, (uint64_t)ped, 1, 0);
            }
            if (oc->menu.natives[ORBX_NAT_RESET_ENTITY_ALPHA]) {
                nat1(oc, ORBX_NAT_RESET_ENTITY_ALPHA, (uint64_t)ped);
            }
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE]) {
                nat1(oc, ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE, (uint64_t)ped);
            }
            oc->menu.applied_flags &= ~APP_INVISIBLE;
        }
    }

    /* Never Wanted & Wanted Level Cap */
    if (oc->menu.never_wanted) {
        if (oc->menu.natives[ORBX_NAT_GET_PLAYER_WANTED_LEVEL]) {
            if (nat1(oc, ORBX_NAT_GET_PLAYER_WANTED_LEVEL, (uint64_t)pl) > 0u) {
                nat1(oc, ORBX_NAT_CLEAR_PLAYER_WANTED_LEVEL, (uint64_t)pl);
            }
        } else if (!(oc->menu.applied_flags & APP_WANTED) || periodic) {
            nat1(oc, ORBX_NAT_CLEAR_PLAYER_WANTED_LEVEL, (uint64_t)pl);
        }
        if (!(oc->menu.applied_flags & APP_WANTED) || ped_changed || periodic) {
            nat1(oc, ORBX_NAT_SET_MAX_WANTED_LEVEL, 0);
            nat2(oc, ORBX_NAT_SET_POLICE_IGNORE_PLAYER, (uint64_t)pl, 1);
            oc->menu.applied_flags |= APP_WANTED;
        }
        oc->menu.last_want_cap_applied = 0u;
    } else if (oc->menu.applied_flags & APP_WANTED) {
        nat1(oc, ORBX_NAT_SET_MAX_WANTED_LEVEL, 5);
        nat2(oc, ORBX_NAT_SET_POLICE_IGNORE_PLAYER, (uint64_t)pl, 0);
        oc->menu.applied_flags &= ~APP_WANTED;
    } else if (oc->menu.wanted_cap_idx > 0u && oc->menu.wanted_cap_idx <= 5u &&
               oc->menu.natives[ORBX_NAT_SET_PLAYER_WANTED_LEVEL] &&
               oc->menu.natives[ORBX_NAT_GET_PLAYER_WANTED_LEVEL]) {
        if ((oc->menu.wanted_cap_idx != oc->menu.last_want_cap_applied) ||
            ((oc->menu.main_ticks % 15u) == 0u)) {
            int32_t cur_w = (int32_t)nat1(oc, ORBX_NAT_GET_PLAYER_WANTED_LEVEL, (uint64_t)pl);
            if (cur_w != (int32_t)oc->menu.wanted_cap_idx) {
                nat3(oc, ORBX_NAT_SET_PLAYER_WANTED_LEVEL, (uint64_t)pl, (uint64_t)oc->menu.wanted_cap_idx, 0u);
                if (oc->menu.natives[ORBX_NAT_SET_PLAYER_WANTED_LEVEL_NOW]) {
                    nat2(oc, ORBX_NAT_SET_PLAYER_WANTED_LEVEL_NOW, (uint64_t)pl, 0u);
                }
            }
            oc->menu.last_want_cap_applied = oc->menu.wanted_cap_idx;
        }
    } else {
        oc->menu.last_want_cap_applied = 0u;
    }

    if (oc->menu.super_jump) {
        nat1(oc, ORBX_NAT_SET_SUPER_JUMP_THIS_FRAME, (uint64_t)pl);
    }

    if (oc->menu.fast_run) {
        if (!(oc->menu.applied_flags & APP_FASTRUN_TOG)) {
            oc->menu.sprint_mult_idx = 2u;
            oc->menu.swim_mult_idx   = 2u;
            oc->menu.applied_flags |= APP_FASTRUN_TOG;
        }
    } else if (oc->menu.applied_flags & APP_FASTRUN_TOG) {
        oc->menu.sprint_mult_idx = 0u;
        oc->menu.swim_mult_idx   = 0u;
        oc->menu.applied_flags &= ~APP_FASTRUN_TOG;
    }

    if (oc->menu.sprint_mult_idx > 0u || oc->menu.swim_mult_idx > 0u) {
        if (!(oc->menu.applied_flags & APP_FASTRUN) || ped_changed || periodic) {
            float sr = (oc->menu.sprint_mult_idx < 3u) ? kMultSpeeds[oc->menu.sprint_mult_idx] : 1.0f;
            float sw = (oc->menu.swim_mult_idx   < 3u) ? kMultSpeeds[oc->menu.swim_mult_idx]   : 1.0f;
            nat2(oc, ORBX_NAT_SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(sr));
            nat2(oc, ORBX_NAT_SET_SWIM_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(sw));
            oc->menu.applied_flags |= APP_FASTRUN;
        }
    } else if (oc->menu.applied_flags & APP_FASTRUN) {
        nat2(oc, ORBX_NAT_SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(1.0f));
        nat2(oc, ORBX_NAT_SET_SWIM_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(1.0f));
        oc->menu.applied_flags &= ~APP_FASTRUN;
    }

    if (oc->menu.spec_ability_inf && ((oc->menu.main_ticks % 10u) == 0u) &&
        oc->menu.natives[ORBX_NAT_SPECIAL_ABILITY_FILL_METER]) {
        nat2(oc, ORBX_NAT_SPECIAL_ABILITY_FILL_METER, (uint64_t)pl, 1u);
    }

    if (oc->menu.no_ragdoll) {
        if (!(oc->menu.applied_flags & APP_NORAGDOLL) || ped_changed || periodic) {
            nat2(oc, ORBX_NAT_SET_PED_CAN_RAGDOLL, (uint64_t)ped, 0);
            oc->menu.applied_flags |= APP_NORAGDOLL;
        }
    } else if (oc->menu.applied_flags & APP_NORAGDOLL) {
        nat2(oc, ORBX_NAT_SET_PED_CAN_RAGDOLL, (uint64_t)ped, 1);
        oc->menu.applied_flags &= ~APP_NORAGDOLL;
    }

    if (oc->menu.seatbelt_on) {
        if (!(oc->menu.applied_flags & APP_SEATBELT) || ped_changed || veh_changed || periodic) {
            if (oc->menu.natives[ORBX_NAT_SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE]) {
                nat2(oc, ORBX_NAT_SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE, (uint64_t)ped, 1u);
            }
            if (oc->menu.natives[ORBX_NAT_SET_PED_CONFIG_FLAG]) {
                nat3(oc, ORBX_NAT_SET_PED_CONFIG_FLAG, (uint64_t)ped, 32u, 0u);
            }
            oc->menu.applied_flags |= APP_SEATBELT;
        }
    } else if (oc->menu.applied_flags & APP_SEATBELT) {
        if (oc->menu.natives[ORBX_NAT_SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE]) {
            nat2(oc, ORBX_NAT_SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE, (uint64_t)ped, 0u);
        }
        if (oc->menu.natives[ORBX_NAT_SET_PED_CONFIG_FLAG]) {
            nat3(oc, ORBX_NAT_SET_PED_CONFIG_FLAG, (uint64_t)ped, 32u, 1u);
        }
        oc->menu.applied_flags &= ~APP_SEATBELT;
    }

    if (oc->menu.inf_ammo) {
        if (!(oc->menu.applied_flags & APP_INF_AMMO) || ped_changed || periodic) {
            nat2(oc, ORBX_NAT_SET_PED_INFINITE_AMMO_CLIP, (uint64_t)ped, 1);
            oc->menu.applied_flags |= APP_INF_AMMO;
        }
    } else if (oc->menu.applied_flags & APP_INF_AMMO) {
        nat2(oc, ORBX_NAT_SET_PED_INFINITE_AMMO_CLIP, (uint64_t)ped, 0);
        oc->menu.applied_flags &= ~APP_INF_AMMO;
    }

    if (oc->menu.explosive_ammo)  nat1(oc, ORBX_NAT_SET_EXPLOSIVE_AMMO_THIS_FRAME, (uint64_t)pl);
    if (oc->menu.fire_ammo)       nat1(oc, ORBX_NAT_SET_FIRE_AMMO_THIS_FRAME, (uint64_t)pl);
    if (oc->menu.explosive_melee) nat1(oc, ORBX_NAT_SET_EXPLOSIVE_MELEE_THIS_FRAME, (uint64_t)pl);

    /* Revert vehicle state on previous vehicle when exiting or changing cars */
    int32_t prev_veh = oc->menu.last_applied_veh;
    if (prev_veh && prev_veh != veh) {
        if (!oc->menu.natives[ORBX_NAT_DOES_ENTITY_EXIST] ||
            nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)prev_veh)) {
            if (oc->menu.applied_flags & APP_VEH_GOD) {
                nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)prev_veh, 0);
                if (oc->menu.natives[ORBX_NAT_SET_ENTITY_PROOFS]) {
                    nat9(oc, ORBX_NAT_SET_ENTITY_PROOFS, (uint64_t)prev_veh, 0, 0, 0, 0, 0, 0, 0, 0);
                }
                if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED]) {
                    nat2(oc, ORBX_NAT_SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED, (uint64_t)prev_veh, 1);
                }
                if (!oc->menu.veh_bulletproof_tyres && oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
                    nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)prev_veh, 1);
                }
            }
            if ((oc->menu.applied_flags & APP_BULLETPROOF) && oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)prev_veh, 1);
            }
            if ((oc->menu.applied_flags & APP_DRIFT) && oc->menu.natives[ORBX_NAT_SET_VEHICLE_REDUCE_GRIP]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_REDUCE_GRIP, (uint64_t)prev_veh, 0u);
            }
            if ((oc->menu.applied_flags & APP_TORQUE) && oc->menu.natives[ORBX_NAT_SET_VEHICLE_CHEAT_POWER_INCREASE]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_CHEAT_POWER_INCREASE, (uint64_t)prev_veh, f2u(1.0f));
            }
            if ((oc->menu.applied_flags & APP_DOORS_LOCK) && oc->menu.natives[ORBX_NAT_SET_VEHICLE_DOORS_LOCKED]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_DOORS_LOCKED, (uint64_t)prev_veh, 1u);
            }
        }
        oc->menu.applied_flags &= ~(APP_VEH_GOD | APP_BULLETPROOF | APP_DRIFT | APP_TORQUE | APP_DOORS_LOCK);
    }

    if (veh) {
        if (oc->menu.veh_god) {
            if (!(oc->menu.applied_flags & APP_VEH_GOD) || veh_changed || periodic) {
                nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)veh, 1);
                if (oc->menu.natives[ORBX_NAT_SET_ENTITY_PROOFS]) {
                    nat9(oc, ORBX_NAT_SET_ENTITY_PROOFS, (uint64_t)veh, 1, 1, 1, 1, 1, 1, 1, 1);
                }
                if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED]) {
                    nat2(oc, ORBX_NAT_SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED, (uint64_t)veh, 0);
                }
                if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
                    nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)veh, 0);
                }
                oc->menu.applied_flags |= APP_VEH_GOD;
            }
        } else if (oc->menu.applied_flags & APP_VEH_GOD) {
            nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)veh, 0);
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_PROOFS]) {
                nat9(oc, ORBX_NAT_SET_ENTITY_PROOFS, (uint64_t)veh, 0, 0, 0, 0, 0, 0, 0, 0);
            }
            if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED, (uint64_t)veh, 1);
            }
            if (!oc->menu.veh_bulletproof_tyres && oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)veh, 1);
            }
            oc->menu.applied_flags &= ~APP_VEH_GOD;
        }

        if (oc->menu.veh_bulletproof_tyres) {
            if (!(oc->menu.applied_flags & APP_BULLETPROOF) || veh_changed || periodic) {
                if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
                    nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)veh, 0);
                }
                oc->menu.applied_flags |= APP_BULLETPROOF;
            }
        } else if (oc->menu.applied_flags & APP_BULLETPROOF) {
            if (!oc->menu.veh_god && oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)veh, 1);
            }
            oc->menu.applied_flags &= ~APP_BULLETPROOF;
        }

        if (oc->menu.veh_drift_mode) {
            if (!(oc->menu.applied_flags & APP_DRIFT) || veh_changed) {
                if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_REDUCE_GRIP]) {
                    nat2(oc, ORBX_NAT_SET_VEHICLE_REDUCE_GRIP, (uint64_t)veh, 1u);
                }
                oc->menu.applied_flags |= APP_DRIFT;
            }
        } else if (oc->menu.applied_flags & APP_DRIFT) {
            if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_REDUCE_GRIP]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_REDUCE_GRIP, (uint64_t)veh, 0u);
            }
            oc->menu.applied_flags &= ~APP_DRIFT;
        }

        if (oc->menu.veh_doors_locked) {
            if (!(oc->menu.applied_flags & APP_DOORS_LOCK) || veh_changed || periodic) {
                if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_DOORS_LOCKED]) {
                    nat2(oc, ORBX_NAT_SET_VEHICLE_DOORS_LOCKED, (uint64_t)veh, 2u);
                }
                oc->menu.applied_flags |= APP_DOORS_LOCK;
            }
        } else if (oc->menu.applied_flags & APP_DOORS_LOCK) {
            if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_DOORS_LOCKED]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_DOORS_LOCKED, (uint64_t)veh, 1u);
            }
            oc->menu.applied_flags &= ~APP_DOORS_LOCK;
        }

        if (oc->menu.veh_torque_idx > 0u && oc->menu.veh_torque_idx < 4u &&
            oc->menu.natives[ORBX_NAT_SET_VEHICLE_CHEAT_POWER_INCREASE]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_CHEAT_POWER_INCREASE, (uint64_t)veh, f2u(kTorqueMults[oc->menu.veh_torque_idx]));
            oc->menu.applied_flags |= APP_TORQUE;
        } else if (oc->menu.applied_flags & APP_TORQUE) {
            if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_CHEAT_POWER_INCREASE]) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_CHEAT_POWER_INCREASE, (uint64_t)veh, f2u(1.0f));
            }
            oc->menu.applied_flags &= ~APP_TORQUE;
        }

        if (oc->menu.horn_boost && !oc->menu.menu_open) {
            int hb = 0;
            if (oc->menu.bind_horn_boost && (oc->menu.pad_buttons_live & oc->menu.bind_horn_boost)) hb = 1;
            if (nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, CTRL_PLAYER, IN_VEH_HORN) ||
                nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_PRESSED, CTRL_PLAYER, IN_VEH_HORN)) hb = 1;
            if (hb) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_FORWARD_SPEED, (uint64_t)veh, f2u(65.0f));
            }
        }
    }

    tick_noclip(oc, ped, veh);

    /* World: Ped & Vehicle Density, Time, TimeScale, Gravity, Radio, Weather */
    if (oc->menu.density_pct < 100u) {
        float mul = (float)oc->menu.density_pct * 0.01f;
        nat1(oc, ORBX_NAT_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME, f2u(mul));
        nat1(oc, ORBX_NAT_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, f2u(mul));
        oc->menu.applied_flags |= APP_DENSITY;
        oc->menu.last_density_pct = oc->menu.density_pct;
    } else if (oc->menu.applied_flags & APP_DENSITY) {
        nat1(oc, ORBX_NAT_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME, f2u(1.0f));
        nat1(oc, ORBX_NAT_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, f2u(1.0f));
        oc->menu.applied_flags &= ~APP_DENSITY;
        oc->menu.last_density_pct = 100u;
    }

    if (oc->menu.clock_paused) {
        if (oc->menu.lock_hour >= 0 && oc->menu.lock_hour <= 23 && oc->menu.natives[ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME]) {
            if ((oc->menu.main_ticks - oc->menu.last_lock_hour_tick) >= 20u) {
                oc->menu.last_lock_hour_tick = oc->menu.main_ticks;
                nat3(oc, ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME, (uint64_t)oc->menu.lock_hour, 0, 0);
            }
        }
        if (!(oc->menu.applied_flags & APP_CLOCK_PAUSE) || periodic) {
            if (oc->menu.natives[ORBX_NAT_PAUSE_CLOCK]) nat1(oc, ORBX_NAT_PAUSE_CLOCK, 1u);
            oc->menu.applied_flags |= APP_CLOCK_PAUSE;
        }
    } else if (oc->menu.applied_flags & APP_CLOCK_PAUSE) {
        if (oc->menu.natives[ORBX_NAT_PAUSE_CLOCK]) nat1(oc, ORBX_NAT_PAUSE_CLOCK, 0u);
        if (oc->menu.natives[ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE]) {
            nat0(oc, ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE);
        }
        oc->menu.applied_flags &= ~APP_CLOCK_PAUSE;
    }

    if (oc->menu.time_scale_idx > 0u && oc->menu.time_scale_idx < 4u && oc->menu.natives[ORBX_NAT_SET_TIME_SCALE]) {
        if (!(oc->menu.applied_flags & APP_TIMESCALE) || periodic) {
            nat1(oc, ORBX_NAT_SET_TIME_SCALE, f2u(kTimeScales[oc->menu.time_scale_idx]));
            oc->menu.applied_flags |= APP_TIMESCALE;
        }
    } else if (oc->menu.applied_flags & APP_TIMESCALE) {
        if (oc->menu.natives[ORBX_NAT_SET_TIME_SCALE]) nat1(oc, ORBX_NAT_SET_TIME_SCALE, f2u(1.0f));
        oc->menu.applied_flags &= ~APP_TIMESCALE;
    }

    if (oc->menu.low_gravity) {
        if (!(oc->menu.applied_flags & APP_LOWGRAV)) {
            if (oc->menu.natives[ORBX_NAT_SET_GRAVITY_LEVEL]) nat1(oc, ORBX_NAT_SET_GRAVITY_LEVEL, 1u);
            oc->menu.applied_flags |= APP_LOWGRAV;
        }
    } else if (oc->menu.applied_flags & APP_LOWGRAV) {
        if (oc->menu.natives[ORBX_NAT_SET_GRAVITY_LEVEL]) nat1(oc, ORBX_NAT_SET_GRAVITY_LEVEL, 0u);
        oc->menu.applied_flags &= ~APP_LOWGRAV;
    }

    if (oc->menu.mobile_radio) {
        if (!(oc->menu.applied_flags & APP_MOBILE_RADIO)) {
            if (oc->menu.natives[ORBX_NAT_SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY]) {
                nat1(oc, ORBX_NAT_SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY, 1u);
            }
            oc->menu.applied_flags |= APP_MOBILE_RADIO;
        }
    } else if (oc->menu.applied_flags & APP_MOBILE_RADIO) {
        if (oc->menu.natives[ORBX_NAT_SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY]) {
            nat1(oc, ORBX_NAT_SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY, 0u);
        }
        oc->menu.applied_flags &= ~APP_MOBILE_RADIO;
    }

    /* Fix Weather Persist: re-assert cleanly once per 120 ticks instead of restarting OVERTIME every frame */
    if (oc->menu.weather_persist && oc->menu.weather_idx >= 0 && oc->menu.weather_idx < 15 &&
        (oc->menu.main_ticks - oc->menu.last_weather_apply_tick) > 120u) {
        oc->menu.last_weather_apply_tick = oc->menu.main_ticks;
        char wname[18];
        copy_cstr(wname, sizeof(wname), kWeathers[oc->menu.weather_idx].name);
        if (oc->menu.natives[ORBX_NAT_SET_OVERRIDE_WEATHER]) {
            nat1(oc, ORBX_NAT_SET_OVERRIDE_WEATHER, ptr2u(wname));
        } else {
            nat1(oc, ORBX_NAT_SET_WEATHER_TYPE_NOW_PERSIST, ptr2u(wname));
        }
    }

    if (oc->menu.cloud_alpha_pct < 100u) {
        if (!(oc->menu.applied_flags & APP_CLOUD_ALPHA) || ((oc->menu.main_ticks % 30u) == 0u)) {
            if (oc->menu.natives[ORBX_NAT_SET_CLOUDS_ALPHA]) {
                nat1(oc, ORBX_NAT_SET_CLOUDS_ALPHA, f2u((float)oc->menu.cloud_alpha_pct * 0.01f));
            }
            oc->menu.applied_flags |= APP_CLOUD_ALPHA;
        }
    } else if (oc->menu.applied_flags & APP_CLOUD_ALPHA) {
        if (oc->menu.natives[ORBX_NAT_SET_CLOUDS_ALPHA]) nat1(oc, ORBX_NAT_SET_CLOUDS_ALPHA, f2u(1.0f));
        oc->menu.applied_flags &= ~APP_CLOUD_ALPHA;
    }

    update_alternate_first_person(oc, pl, ped, veh, veh_changed, now_ms);
    update_realistic_fuel_system(oc, pl, ped, veh, veh_changed, now_ms);
    update_calmer_police_ai(oc, pl);

    /* Stage 1 & Stage 2 Continuous Graphics Enforcement (only after world load main_ticks > 180) */
    if (oc->menu.main_ticks > 180u) {
        if (oc->menu.enb_extra_active > 0u) {
            /* Stage 1 Anti-Aliasing (Smooth): continuously suppress RAGE film grain / pixel dither */
            if (oc->menu.natives[ORBX_NAT_SET_NOISINESSOVERIDE]) nat1(oc, ORBX_NAT_SET_NOISINESSOVERIDE, f2u(0.0f));
            if (oc->menu.natives[ORBX_NAT_SET_NOISEOVERIDE])     nat1(oc, ORBX_NAT_SET_NOISEOVERIDE, 0u);
            oc->menu.applied_flags &= ~APP_GFX_GRAIN;
        } else if (oc->menu.gfx_film_grain == 0u && (oc->menu.applied_flags & APP_GFX_GRAIN)) {
            if (oc->menu.natives[ORBX_NAT_SET_NOISINESSOVERIDE]) nat1(oc, ORBX_NAT_SET_NOISINESSOVERIDE, f2u(0.0f));
            if (oc->menu.natives[ORBX_NAT_SET_NOISEOVERIDE])     nat1(oc, ORBX_NAT_SET_NOISEOVERIDE, 0u);
            oc->menu.applied_flags &= ~APP_GFX_GRAIN;
        }

        /* Stage 1 LOD Draw Distance & HD Streaming Area */
        if (oc->menu.enb_shadow_scale >= 15u) {
            float lod_val = (float)oc->menu.enb_shadow_scale * 0.1f;
            if (lod_val > 5.0f) lod_val = 5.0f;
            if (oc->menu.natives[ORBX_NAT_OVERRIDE_LODSCALE_THIS_FRAME]) {
                nat1(oc, ORBX_NAT_OVERRIDE_LODSCALE_THIS_FRAME, f2u(lod_val));
            }
            if ((oc->menu.main_ticks % 15u) == 0u && ped > 0 && oc->menu.natives[ORBX_NAT_SET_HD_AREA]) {
                Vec3 ppos;
                zero_bytes(&ppos, sizeof(ppos));
                uint64_t ga[2] = { (uint64_t)ped, 1u };
                nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ga, 2, &ppos);
                float hd_rad = (float)oc->menu.enb_shadow_scale * 12.0f;
                if (hd_rad > 600.0f) hd_rad = 600.0f;
                uint64_t hda[4] = { f2u(ppos.x), f2u(ppos.y), f2u(ppos.z), f2u(hd_rad) };
                nat_invoke(oc, ORBX_NAT_SET_HD_AREA, hda, 4, NULL);
            }
        }

        /* Stage 2 Dynamic Shadows (3D): keep real RAGE CSM filtering & crisp cascade bounds */
        if (oc->menu.enb_shadow_soft > 0u) {
            uint32_t sh_lvl = oc->menu.enb_shadow_soft;
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_SHADOW_SAMPLE_TYPE]) {
                const char *stype = "CSM_ST_BOX4x4";
                if (sh_lvl == 2u)      stype = "CSM_ST_HIGHRES_BOX4x4";
                else if (sh_lvl == 3u) stype = "CSM_ST_SOFT16";
                else if (sh_lvl >= 4u) stype = "CSM_ST_POISSON16_RPDB_GNORM";
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_SHADOW_SAMPLE_TYPE, ptr2u(stype));
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE]) {
                float bscale = 1.0f - (float)sh_lvl * 0.12f;
                if (bscale < 0.40f) bscale = 0.40f;
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE, f2u(bscale));
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER, 1u);
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE, 0u);
            }
            oc->menu.applied_flags |= APP_GFX_SHADOWS;
        } else if (oc->menu.applied_flags & APP_GFX_SHADOWS) {
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE]) {
                nat0(oc, ORBX_NAT_CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE);
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE, f2u(1.0f));
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER, 1u);
            }
            if (oc->menu.natives[ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE]) {
                nat1(oc, ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE, 0u);
            }
            oc->menu.applied_flags &= ~APP_GFX_SHADOWS;
        }

        /* Stage 1 High-Precision Bokeh Depth of Field (DOF) - dramatic near-to-mid cinematic blur */
        if (oc->menu.enb_dof_lvl > 0u) {
            if (oc->menu.natives[ORBX_NAT_SET_USE_HI_DOF]) {
                nat1(oc, ORBX_NAT_SET_USE_HI_DOF, 1u);
            }
            if (oc->menu.natives[ORBX_NAT_SET_HIDOF_OVERRIDE]) {
                /* kDofNear = distance where background blur starts; kDofFar = distance where blur is 100% */
                static const float kDofNear[5] = { 45.0f, 32.0f, 22.0f, 15.0f, 8.0f };
                static const float kDofFar[5]  = { 140.0f, 100.0f, 70.0f, 48.0f, 26.0f };
                uint32_t idx = (oc->menu.enb_dof_lvl <= 5u) ? (oc->menu.enb_dof_lvl - 1u) : 4u;
                uint64_t da[6] = { 0u, 1u, f2u(0.0f), f2u(0.0f), f2u(kDofFar[idx]), f2u(kDofNear[idx]) };
                nat_invoke(oc, ORBX_NAT_SET_HIDOF_OVERRIDE, da, 6, NULL);
            }
        } else {
            if (oc->menu.natives[ORBX_NAT_SET_USE_HI_DOF]) {
                nat1(oc, ORBX_NAT_SET_USE_HI_DOF, 0u);
            }
            if (oc->menu.natives[ORBX_NAT_SET_HIDOF_OVERRIDE]) {
                uint64_t da[6] = { 0u, 0u, f2u(0.0f), f2u(0.0f), f2u(0.0f), f2u(0.0f) };
                nat_invoke(oc, ORBX_NAT_SET_HIDOF_OVERRIDE, da, 6, NULL);
            }
        }

        /* Periodically re-assert active Stage 1/2 + Base Preset timecycle stack */
        if (periodic && (oc->menu.gfx_preset_idx > 0u || oc->menu.enb_motion_blur > 0u ||
                         oc->menu.enb_ssao_boost > 0u || oc->menu.enb_refl_lvl > 0u || oc->menu.enb_bloom_lvl > 0u)) {
            sync_enb_graphics_pipeline(oc);
        }
    }

    oc->menu.last_applied_ped = ped;
    oc->menu.last_applied_veh = veh;
}

/* ============================================================================
 * Input Handling & Menu State Machine
 * ============================================================================ */

static uint32_t btn_down(OrbxNativeContext *oc, int id) {
    return (nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, CTRL_FRONTEND, (uint64_t)id) ||
            nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, CTRL_PLAYER, (uint64_t)id) ||
            nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_PRESSED, CTRL_FRONTEND, (uint64_t)id) ||
            nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_PRESSED, CTRL_PLAYER, (uint64_t)id)) ? 1u : 0u;
}

static uint32_t match_bind_btn(OrbxNativeContext *oc, uint32_t raw) {
    uint32_t m = 0u;
    uint32_t b_acc  = oc->menu.bind_accept   ? oc->menu.bind_accept   : SCE_PAD_BTN_CROSS;
    uint32_t b_can  = oc->menu.bind_cancel   ? oc->menu.bind_cancel   : SCE_PAD_BTN_CIRCLE;
    uint32_t b_prev = oc->menu.bind_tab_prev ? oc->menu.bind_tab_prev : SCE_PAD_BTN_L1;
    uint32_t b_next = oc->menu.bind_tab_next ? oc->menu.bind_tab_next : SCE_PAD_BTN_R1;

    if (raw & b_next)             m |= BTN_RB;
    if (raw & b_prev)             m |= BTN_LB;
    if (raw & SCE_PAD_BTN_UP)     m |= BTN_UP;
    if (raw & SCE_PAD_BTN_DOWN)   m |= BTN_DOWN;
    if (raw & SCE_PAD_BTN_LEFT)   m |= BTN_LEFT;
    if (raw & SCE_PAD_BTN_RIGHT)  m |= BTN_RIGHT;
    if (raw & b_acc)              m |= BTN_ACCEPT;
    if (raw & b_can)              m |= BTN_CANCEL;
    return m;
}

static uint32_t poll_button_mask_sampled(OrbxNativeContext *oc, uint32_t raw_sampled) {
    if (oc->menu.pad_calls > 0) {
        uint32_t m = match_bind_btn(oc, raw_sampled);
        if (oc->menu.menu_open && oc->menu.nav_stick && !oc->menu.keep_steering &&
            oc->menu.natives[ORBX_NAT_GET_DISABLED_CONTROL_NORMAL]) {
            float sx = u2f(nat2(oc, ORBX_NAT_GET_DISABLED_CONTROL_NORMAL, 0, IN_MOVE_LR));
            float sy = u2f(nat2(oc, ORBX_NAT_GET_DISABLED_CONTROL_NORMAL, 0, IN_MOVE_UD));
            float ax = (sx < 0.0f) ? -sx : sx;
            float ay = (sy < 0.0f) ? -sy : sy;
            if (ay > 0.72f && ax < 0.35f) {
                m |= (sy < 0.0f) ? BTN_UP : BTN_DOWN;
            } else if (ax > 0.72f && ay < 0.35f) {
                m |= (sx < 0.0f) ? BTN_LEFT : BTN_RIGHT;
            }
        }
        return m;
    }

    uint32_t m = 0;
    if (btn_down(oc, IN_FE_RB))     m |= BTN_RB;
    if (btn_down(oc, IN_FE_LB))     m |= BTN_LB;
    if (btn_down(oc, IN_FE_UP)    || btn_down(oc, IN_SPAD_UP)    || btn_down(oc, IN_CELL_UP))     m |= BTN_UP;
    if (btn_down(oc, IN_FE_DOWN)  || btn_down(oc, IN_SPAD_DOWN)  || btn_down(oc, IN_CELL_DOWN))   m |= BTN_DOWN;
    if (btn_down(oc, IN_FE_LEFT)  || btn_down(oc, IN_SPAD_LEFT)  || btn_down(oc, IN_CELL_LEFT))   m |= BTN_LEFT;
    if (btn_down(oc, IN_FE_RIGHT) || btn_down(oc, IN_SPAD_RIGHT) || btn_down(oc, IN_CELL_RIGHT))  m |= BTN_RIGHT;
    if (btn_down(oc, IN_FE_ACCEPT) || btn_down(oc, IN_CELL_SELECT)) m |= BTN_ACCEPT;
    if (btn_down(oc, IN_FE_CANCEL) || btn_down(oc, IN_CELL_CANCEL)) m |= BTN_CANCEL;
    return m;
}

static uint32_t poll_button_mask(OrbxNativeContext *oc) {
    uint32_t raw = 0u;
    if (oc->menu.pad_calls > 0) {
        uint32_t latch = __atomic_exchange_n(&oc->menu.pad_buttons_latch, 0u, __ATOMIC_ACQ_REL);
        raw = __atomic_load_n(&oc->menu.pad_buttons_live, __ATOMIC_ACQUIRE) | latch;
    }
    return poll_button_mask_sampled(oc, raw);
}

static uint32_t compute_triggered_mask(OrbxNativeContext *oc, uint32_t now_ms, uint32_t cur) {
    uint32_t prev  = oc->menu.prev_btn_mask;
    uint32_t newly = cur & ~prev;
    uint32_t trig  = newly;

    const uint32_t kRepeatMask = (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
    uint32_t held = cur & kRepeatMask;

    if (held == 0 || (held != (oc->menu.hold_btn_mask & kRepeatMask))) {
        oc->menu.hold_btn_mask  = held;
        oc->menu.hold_start_ms  = now_ms;
        oc->menu.hold_repeat_ms = now_ms;
    } else {
        if ((now_ms - oc->menu.hold_start_ms) > 280u) {
            if ((now_ms - oc->menu.hold_repeat_ms) > 110u) {
                trig |= held;
                oc->menu.hold_repeat_ms = now_ms;
            }
        }
    }

    oc->menu.prev_btn_mask = cur;
    return trig;
}

static void disable_game_controls_while_open(OrbxNativeContext *oc) {
    static const int kDisControls[] = {
        IN_PHONE, IN_CHAR_WHEEL, IN_COVER, IN_RELOAD,
        IN_VEH_CIN_CAM, IN_VEH_NEXT_WPN, IN_VEH_PREV_WPN,
        IN_FE_UP, IN_FE_DOWN, IN_FE_LEFT, IN_FE_RIGHT,
        IN_FE_ACCEPT, IN_FE_CANCEL, IN_FE_LB, IN_FE_RB,
        IN_SPAD_UP, IN_SPAD_DOWN, IN_SPAD_LEFT, IN_SPAD_RIGHT,
        IN_CELL_UP, IN_CELL_DOWN, IN_CELL_LEFT, IN_CELL_RIGHT,
        IN_CELL_SELECT, IN_CELL_CANCEL
    };
    for (size_t i = 0; i < (sizeof(kDisControls) / sizeof(kDisControls[0])); i++) {
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, (uint64_t)kDisControls[i], 1);
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_FRONTEND, (uint64_t)kDisControls[i], 1);
    }
    if (!oc->menu.keep_steering) {
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, IN_SPRINT, 1);
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, IN_JUMP, 1);
        if (oc->menu.nav_stick) {
            nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, IN_MOVE_LR, 1);
            nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, IN_MOVE_UD, 1);
        }
    }
    if (oc->menu.keybind_capture_slot) {
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, IN_ATTACK, 1);
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, IN_AIM, 1);
    }
}

static void add_row(MenuRow *rows, int *cnt, int max_rows,
                    const char *label, const char *badge, const char *desc,
                    uint8_t kind, uint8_t id, uint8_t arg, uint8_t on) {
    if (*cnt >= max_rows) return;
    MenuRow *r = &rows[(*cnt)++];
    zero_bytes(r, sizeof(*r));
    copy_cstr(r->label, sizeof(r->label), label);
    copy_cstr(r->badge, sizeof(r->badge), badge);
    copy_cstr(r->desc,  sizeof(r->desc),  desc);
    r->kind = kind;
    r->id   = id;
    r->arg  = arg;
    r->on   = on;
}

static void add_tog(MenuRow *rows, int *cnt, const char *label, const char *desc, uint8_t id, uint32_t val) {
    add_row(rows, cnt, MAX_MENU_ROWS, label, val ? "ON" : "OFF", desc,
            ROW_TOGGLE, id, 0, val ? 1u : 0u);
}

static void add_div(MenuRow *rows, int *cnt, const char *label) {
    add_row(rows, cnt, MAX_MENU_ROWS, label, "", "", ROW_DIVIDER, ITEM_NONE, 0, 0);
}

static void add_keybind_row(MenuRow *rows, int *cnt, OrbxNativeContext *oc,
                            const char *label, uint32_t mod, uint32_t btn,
                            uint8_t slot_id, const char *desc) {
    char bdg[24];
    bdg[0] = '\0';
    if (oc->menu.keybind_capture_slot == (uint32_t)slot_id) {
        copy_cstr(bdg, sizeof(bdg), "PRESS BTN...");
    } else {
        fmt_combo_btn(bdg, sizeof(bdg), mod, btn);
    }
    add_row(rows, cnt, MAX_MENU_ROWS, label, bdg, desc, ROW_ACTION, ITEM_ACT_KEYBIND_SLOT, slot_id, 0);
}

static void build_submenu_rows(OrbxNativeContext *oc, uint32_t sub,
                               MenuRow *rows, int *out_cnt, const char **out_title) {
    int n = 0;
    char buf[24];
    size_t pos = 0;

    switch (sub) {
    case ORBX_SUB_PLAYER:
        *out_title = "SELF";
        add_div(rows, &n, "-- SURVIVAL & FLIGHT --");
        add_tog(rows, &n, "God Mode",            "Invincibility & full proof protection", ITEM_TOG_GODMODE,      oc->menu.godmode);
        add_tog(rows, &n, "Airbreak / No-Clip",  "Fly through walls (Stick + L2/R2)",   ITEM_TOG_NOCLIP,       oc->menu.noclip_on);
        add_row(rows, &n, MAX_MENU_ROWS, "Airbreak Speed", kAirbreakNames[(oc->menu.noclip_speed_idx < 4u) ? oc->menu.noclip_speed_idx : 1u],
                "Adjust Airbreak flight speed", ROW_CYCLE, ITEM_CYC_NOCLIP_SPEED, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Heal & Full Armor",  "RUN", "Restore health and 100% armor", ROW_ACTION, ITEM_ACT_HEAL_ARMOR, 0, 0);
        add_div(rows, &n, "-- WANTED & STEALTH --");
        add_tog(rows, &n, "Never Wanted",        "Permanently disables police pursuit",  ITEM_TOG_NEVER_WANTED, oc->menu.never_wanted);
        if (oc->menu.wanted_cap_idx == 0u) copy_cstr(buf, sizeof(buf), "Normal");
        else { pos = orbx_pic_fmt_u64(buf, 0, sizeof(buf) - 3, oc->menu.wanted_cap_idx); pos = orbx_pic_fmt_str(buf, pos, sizeof(buf) - 1, " Star"); buf[pos] = '\0'; }
        add_row(rows, &n, MAX_MENU_ROWS, "Lock Wanted Level", buf, "Lock desired wanted level (0=Normal)", ROW_CYCLE, ITEM_CYC_WANTED_CAP, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Clear Wanted Level", "RUN", "Clear current wanted stars immediately", ROW_ACTION, ITEM_ACT_CLEAR_WANTED, 0, 0);
        add_tog(rows, &n, "Invisible Ped",       "Make player completely invisible",     ITEM_TOG_INVISIBLE,    oc->menu.invisible ? 1u : 0u);
        add_row(rows, &n, MAX_MENU_ROWS, "Force Player Visible", "RUN", "Restore player visibility & entity alpha", ROW_ACTION, ITEM_ACT_FORCE_VISIBLE, 0, 0);
        add_div(rows, &n, "-- MOVEMENT & ABILITIES --");
        add_tog(rows, &n, "Super Jump",          "Leap dramatically higher",            ITEM_TOG_SUPER_JUMP,   oc->menu.super_jump);
        add_row(rows, &n, MAX_MENU_ROWS, "Run Speed",  kMultLabels[(oc->menu.sprint_mult_idx < 3u) ? oc->menu.sprint_mult_idx : 0u],
                "Sprint speed multiplier (up to 1.49x)", ROW_CYCLE, ITEM_CYC_SPRINT_MULT, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Swim Speed", kMultLabels[(oc->menu.swim_mult_idx < 3u) ? oc->menu.swim_mult_idx : 0u],
                "Swim speed multiplier (up to 1.49x)",   ROW_CYCLE, ITEM_CYC_SWIM_MULT,   0, 0);
        add_tog(rows, &n, "No Ragdoll",          "Prevent stumbling and falling",       ITEM_TOG_NO_RAGDOLL,   oc->menu.no_ragdoll);
        add_tog(rows, &n, "Inf Special Ability", "Keep special meter constantly full",  ITEM_TOG_SPEC_INF,     oc->menu.spec_ability_inf);
        break;

    case ORBX_SUB_WEAPONS:
        *out_title = "WEAPONS";
        add_div(rows, &n, "-- ARSENAL & AMMO --");
        add_row(rows, &n, MAX_MENU_ROWS, "Give All Weapons",   "RUN", "Grants full loadout & parachute",      ROW_ACTION, ITEM_ACT_GIVE_WEAPONS,   0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Remove All Weapons", "RUN", "Strips all weapons from inventory",    ROW_ACTION, ITEM_ACT_REMOVE_WEAPONS, 0, 0);
        add_tog(rows, &n, "Infinite Clip Ammo", "Never reload & infinite rounds",       ITEM_TOG_INF_AMMO,  oc->menu.inf_ammo);
        add_div(rows, &n, "-- COMBAT MODIFIERS --");
        add_tog(rows, &n, "Explosive Bullets",  "Every bullet explodes on impact",      ITEM_TOG_EXP_AMMO,  oc->menu.explosive_ammo);
        add_tog(rows, &n, "Fire Bullets",       "Ignites targets on impact",            ITEM_TOG_FIRE_AMMO, oc->menu.fire_ammo);
        add_tog(rows, &n, "Explosive Melee",    "Melee punches trigger blasts",         ITEM_TOG_EXP_MELEE, oc->menu.explosive_melee);
        break;

    case ORBX_SUB_VEHICLE:
        *out_title = "VEHICLE";
        add_div(rows, &n, "-- LS CUSTOMS & VISUALS --");
        add_row(rows, &n, MAX_MENU_ROWS, "LSC Tuning & Body",   "OPEN", "Engine, spoiler, bumper, rims, plates",   ROW_SUB, ITEM_SUB_VEH_CUSTOM, ORBX_SUB_VEH_CUSTOM, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Colors & Paint",      "OPEN", "26 finishes & window tint",               ROW_SUB, ITEM_SUB_VEH_COLORS, ORBX_SUB_VEH_COLORS, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Neons & Smoke",       "OPEN", "Underglow neon tubes & tire smoke",       ROW_SUB, ITEM_SUB_VEH_LIGHTS, ORBX_SUB_VEH_LIGHTS, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Doors, Locks & Extra","OPEN", "Open/close doors, locks & extras",       ROW_SUB, ITEM_SUB_VEH_DOORS,  ORBX_SUB_VEH_DOORS,  0);
        add_div(rows, &n, "-- DRIVING & PROTECTION --");
        add_tog(rows, &n, "Vehicle God Mode",   "Vehicle indestructible & spotless",         ITEM_TOG_VEH_GOD,  oc->menu.veh_god);
        add_tog(rows, &n, "True Seatbelt",      "Never fly through windshield or off bike",  ITEM_TOG_SEATBELT, oc->menu.seatbelt_on);
        add_row(rows, &n, MAX_MENU_ROWS, "Speedometer HUD", kSpeedoNames[(oc->menu.speedo_mode < 3u) ? oc->menu.speedo_mode : 0u],
                "Forza 6 tachometer, gear, needle & dynamic speed", ROW_CYCLE, ITEM_CYC_SPEEDO_HUD, 0, 0);
        add_tog(rows, &n, "Horn Boost",         "Hold Horn/L3 or hotkey for burst speed",  ITEM_TOG_HORN_BOOST, oc->menu.horn_boost);
        add_tog(rows, &n, "Drift Mode",         "Reduce tire grip for easy power slides",  ITEM_TOG_VEH_DRIFT,   oc->menu.veh_drift_mode);
        add_row(rows, &n, MAX_MENU_ROWS, "Engine Torque", kTorqueLabels[(oc->menu.veh_torque_idx < 4u) ? oc->menu.veh_torque_idx : 0u],
                "Multiply vehicle acceleration & torque", ROW_CYCLE, ITEM_CYC_VEH_TORQUE, 0, 0);
        add_tog(rows, &n, "Bulletproof Tires",  "Tires cannot burst or deflate",           ITEM_TOG_BULLETPROOF_TIRES, oc->menu.veh_bulletproof_tyres);
        add_tog(rows, &n, "Realistic Fuel",     "Dynamic burn, stalling at 0% & gas stations", ITEM_TOG_REALISTIC_FUEL, oc->menu.fuel_system_on);
        add_row(rows, &n, MAX_MENU_ROWS, "Refuel Tank (100%)",  "FILL", "Fill vehicle gas tank to 100% full", ROW_ACTION, ITEM_ACT_REFUEL_VEH, 0, 0);
        add_div(rows, &n, "-- QUICK ACTIONS --");
        add_row(rows, &n, MAX_MENU_ROWS, "Warp Into Nearest",  "RUN", "Teleport inside closest vehicle",        ROW_ACTION, ITEM_ACT_WARP_NEAREST_VEH, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Repair & Wash",      "RUN", "Fixes all dents, engine & dirt",         ROW_ACTION, ITEM_ACT_FIX_VEH,   0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Flip Upright",       "RUN", "Places flipped vehicle wheels-down",     ROW_ACTION, ITEM_ACT_FLIP_VEH,  0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Super Speed Boost",  "RUN", "Instant forward velocity launch",        ROW_ACTION, ITEM_ACT_BOOST_VEH, 0, 0);
        break;

    case ORBX_SUB_VEH_DOORS:
        *out_title = "VEHICLE / DOORS";
        add_row(rows, &n, MAX_MENU_ROWS, "Open All Doors",    "RUN", "Swings open all doors, hood & trunk", ROW_ACTION, ITEM_ACT_DOORS_OPEN_ALL, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Shut All Doors",    "RUN", "Closes every open vehicle door",      ROW_ACTION, ITEM_ACT_DOORS_SHUT_ALL, 0, 0);
        add_tog(rows, &n, "Lock All Doors",   "Locks vehicle doors from AI/outside", ITEM_TOG_DOORS_LOCK, oc->menu.veh_doors_locked);
        add_row(rows, &n, MAX_MENU_ROWS, "Open Front Left",   "OPEN","Open driver door",                    ROW_ACTION, ITEM_ACT_DOOR_TOGGLE, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Open Front Right",  "OPEN","Open passenger door",                 ROW_ACTION, ITEM_ACT_DOOR_TOGGLE, 1, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Open Hood",         "OPEN","Pop engine hood",                     ROW_ACTION, ITEM_ACT_DOOR_TOGGLE, 4, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Open Trunk",        "OPEN","Pop trunk lid",                       ROW_ACTION, ITEM_ACT_DOOR_TOGGLE, 5, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Wash Vehicle",      "RUN", "Set dirt level to 0.0 (spotless)",    ROW_ACTION, ITEM_ACT_WASH_VEH, 0, 0);
        add_div(rows, &n, "-- VEHICLE EXTRAS --");
        for (int ex = 1; ex <= 4; ex++) {
            pos = orbx_pic_fmt_str(buf, 0, sizeof(buf) - 4, "Enable Extra ");
            pos = orbx_pic_fmt_u64(buf, pos, sizeof(buf) - 1, (uint64_t)ex);
            buf[pos] = '\0';
            add_row(rows, &n, MAX_MENU_ROWS, buf, "ADD", "Enable vehicle roof/light/rollbar extra", ROW_ACTION, ITEM_ACT_EXTRA_TOGGLE, (uint8_t)ex, 0);
        }
        break;

    case ORBX_SUB_VEH_COLORS: {
        *out_title = "VEHICLE / PAINT";
        uint32_t tgt = oc->menu.veh_color_target;
        if (tgt >= 5) tgt = 0;
        add_row(rows, &n, MAX_MENU_ROWS, "Target Surface", kColorTargetNames[tgt], "Choose surface to paint", ROW_CYCLE, ITEM_CYC_COLOR_TARGET, 0, 0);
        uint32_t ti = oc->menu.veh_tint_idx;
        if (ti >= 6) ti = 0;
        add_row(rows, &n, MAX_MENU_ROWS, "Window Tint", kWindowTintNames[ti], "Select window tint darkness", ROW_CYCLE, ITEM_CYC_WINDOW_TINT, 0, 0);
        uint32_t cat = oc->menu.veh_color_cat;
        if (cat >= COLOR_CAT_MAX) cat = 0;
        add_row(rows, &n, MAX_MENU_ROWS, "Finish Variation", kColorCatNames[cat], "Cycle: Classic, Matte, Metals, Pearl", ROW_CYCLE, ITEM_CYC_COLOR_CAT, 0, 0);
        add_div(rows, &n, "-- COLOR PALETTE --");
        for (size_t i = 0; i < (sizeof(kColorPresets) / sizeof(kColorPresets[0])); i++) {
            if (kColorPresets[i].cat == (uint8_t)cat) {
                add_row(rows, &n, MAX_MENU_ROWS, kColorPresets[i].name, "",
                        "Press X to apply color swatch to vehicle", ROW_ACTION, ITEM_ACT_COLOR_PRESET, (uint8_t)i, 0);
            }
        }
        break;
    }

    case ORBX_SUB_VEH_CUSTOM: {
        *out_title = "VEHICLE / LSC";
        add_row(rows, &n, MAX_MENU_ROWS, "Max All Performance", "MAX", "Install top Engine/Brakes/Trans/Susp/Armor", ROW_ACTION, ITEM_ACT_MAX_PERF, 0, 0);
        add_div(rows, &n, "-- POWERTRAIN & ARMOR --");
        add_row(rows, &n, MAX_MENU_ROWS, "Engine Level", kEngineNames[(oc->menu.veh_engine_lvl < 5) ? oc->menu.veh_engine_lvl : 0], "Engine EMS upgrade tier", ROW_CYCLE, ITEM_CYC_MOD_ENGINE, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Brakes",       kBrakesNames[(oc->menu.veh_brake_lvl < 4) ? oc->menu.veh_brake_lvl : 0],  "Brake system tier", ROW_CYCLE, ITEM_CYC_MOD_BRAKES, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Transmission", kTransNames[(oc->menu.veh_trans_lvl < 4) ? oc->menu.veh_trans_lvl : 0],   "Gearbox shift speed tier", ROW_CYCLE, ITEM_CYC_MOD_TRANS, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Suspension",   kSuspNames[(oc->menu.veh_susp_lvl < 5) ? oc->menu.veh_susp_lvl : 0],     "Lowering & stiffness", ROW_CYCLE, ITEM_CYC_MOD_SUSP, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Armor",        kArmorNames[(oc->menu.veh_armor_lvl < 6) ? oc->menu.veh_armor_lvl : 0],  "Chassis armor plating", ROW_CYCLE, ITEM_CYC_MOD_ARMOR, 0, 0);
        add_tog(rows, &n, "Turbo Tuning",     "Forced induction turbo", ITEM_TOG_TURBO, oc->menu.veh_turbo_on);
        add_tog(rows, &n, "Xenon Headlights", "HID xenon headlights",     ITEM_TOG_XENON, oc->menu.veh_xenon_on);
        add_div(rows, &n, "-- BODY TUNING PARTS --");
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD_KIT]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_MOD_KIT, (uint64_t)veh, 0u);
        }
        for (size_t s = 0; s < (sizeof(kLscBodySlots) / sizeof(kLscBodySlots[0])); s++) {
            char pbadge[16];
            pbadge[0] = '\0';
            int slot_id = (int)kLscBodySlots[s].slot;
            if (veh && oc->menu.natives[ORBX_NAT_GET_NUM_VEHICLE_MODS] && oc->menu.natives[ORBX_NAT_GET_VEHICLE_MOD]) {
                int32_t total = (int32_t)nat2(oc, ORBX_NAT_GET_NUM_VEHICLE_MODS, (uint64_t)veh, (uint64_t)slot_id);
                int32_t cur_m = (int32_t)nat2(oc, ORBX_NAT_GET_VEHICLE_MOD, (uint64_t)veh, (uint64_t)slot_id);
                if (total <= 0) {
                    copy_cstr(pbadge, sizeof(pbadge), "N/A");
                } else if (cur_m < 0) {
                    pos = orbx_pic_fmt_str(pbadge, 0, sizeof(pbadge) - 1, "Stock");
                    pbadge[pos] = '\0';
                } else {
                    pos = orbx_pic_fmt_u64(pbadge, 0, sizeof(pbadge) - 1, (uint64_t)(cur_m + 1));
                    pos = orbx_pic_fmt_str(pbadge, pos, sizeof(pbadge) - 1, "/");
                    pos = orbx_pic_fmt_u64(pbadge, pos, sizeof(pbadge) - 1, (uint64_t)total);
                    pbadge[pos] = '\0';
                }
            } else {
                copy_cstr(pbadge, sizeof(pbadge), "NO VEH");
            }
            add_row(rows, &n, MAX_MENU_ROWS, kLscBodySlots[s].name, pbadge,
                    "Press X or Left/Right to equip part", ROW_CYCLE, ITEM_CYC_LSC_PART, (uint8_t)s, 0);
        }
        add_div(rows, &n, "-- LICENSE PLATES --");
        add_row(rows, &n, MAX_MENU_ROWS, "Custom Plate Text...", "TYPE", "Type custom plate (max 8 chars)", ROW_ACTION, ITEM_ACT_PLATE_KEYBOARD, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Plate: MODHEN",        "SET",  "Set plate to MODHEN", ROW_ACTION, ITEM_ACT_PLATE_MODHEN, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Plate: OXAGON",        "SET",  "Set plate to OXAGON", ROW_ACTION, ITEM_ACT_PLATE_OXAGON, 0, 0);
        break;
    }

    case ORBX_SUB_VEH_LIGHTS:
        *out_title = "VEHICLE / LIGHTS";
        add_tog(rows, &n, "All Neon Tubes",   "Toggle all underglow neon bars", ITEM_TOG_NEON_ALL,   oc->menu.veh_neon_on);
        add_tog(rows, &n, "Front Neon",       "Front bumper tube",              ITEM_TOG_NEON_FRONT, oc->menu.veh_neon_front);
        add_tog(rows, &n, "Back Neon",        "Rear bumper tube",               ITEM_TOG_NEON_BACK,  oc->menu.veh_neon_back);
        add_tog(rows, &n, "Left Neon",        "Left skirt tube",                ITEM_TOG_NEON_LEFT,  oc->menu.veh_neon_left);
        add_tog(rows, &n, "Right Neon",       "Right skirt tube",               ITEM_TOG_NEON_RIGHT, oc->menu.veh_neon_right);
        add_row(rows, &n, MAX_MENU_ROWS, "Neon Color", kNeonColors[(oc->menu.veh_neon_color_idx < 9) ? oc->menu.veh_neon_color_idx : 0].name,
                "Underglow color preset", ROW_CYCLE, ITEM_CYC_NEON_COLOR, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Tire Smoke", kSmokeColors[(oc->menu.veh_smoke_color_idx < 9) ? oc->menu.veh_smoke_color_idx : 0].name,
                "Burnout smoke color", ROW_CYCLE, ITEM_CYC_SMOKE_COLOR, 0, 0);
        break;

    case ORBX_SUB_SPAWN: {
        *out_title = "VEHICLE SPAWNER";
        add_div(rows, &n, "-- INSTALLED ADD-ON DLC CARS --");
        uint32_t cnt = oc->menu.addon_car_count;
        if (cnt > ORBX_MAX_ADDON_CARS) cnt = ORBX_MAX_ADDON_CARS;
        if (cnt > 0) {
            for (uint32_t i = 0; i < cnt && i < 16u; i++) {
                const char *lbl = oc->menu.addon_cars[i].label[0] ? oc->menu.addon_cars[i].label : oc->menu.addon_cars[i].name;
                const char *bdg = oc->menu.addon_cars[i].name[0]  ? oc->menu.addon_cars[i].name  : "SPAWN";
                add_row(rows, &n, MAX_MENU_ROWS, lbl, bdg, "Spawn add-on vehicle", ROW_ACTION, ITEM_ADDON_IDX, (uint8_t)i, 0);
            }
            if (cnt > 16u) {
                add_row(rows, &n, MAX_MENU_ROWS, "All Add-On DLC Cars...", "OPEN", "Browse complete list", ROW_SUB, ITEM_SUB_ADDON_SPAWN, ORBX_SUB_ADDON_SPAWN, 0);
            }
        } else {
            add_row(rows, &n, MAX_MENU_ROWS, "No Add-On DLC Found", "INFO", "Add models to addon_cars.txt", ROW_ACTION, ITEM_NONE, 0, 0);
        }
        add_div(rows, &n, "-- CUSTOM & KEYBOARD --");
        add_row(rows, &n, MAX_MENU_ROWS, "Type Model Name...", "TYPE", "Type any stock or DLC model name", ROW_ACTION, ITEM_SPAWN_KEYBOARD, 0, 0);
        add_div(rows, &n, "-- VEHICLE CLASSES --");
        for (size_t i = 0; i < (sizeof(kSpawnCategories) / sizeof(kSpawnCategories[0])); i++) {
            add_row(rows, &n, MAX_MENU_ROWS, kSpawnCategories[i].cat_name, "OPEN",
                    kSpawnCategories[i].cat_desc, ROW_SUB, ITEM_SUB_SPAWN_CAT, (uint8_t)i, 0);
        }
        break;
    }

    case ORBX_SUB_SPAWN_CAT: {
        uint32_t ci = (oc->menu.spawn_cat_idx < 6u) ? oc->menu.spawn_cat_idx : 0u;
        *out_title = kSpawnCategories[ci].cat_name;
        for (uint8_t i = 0; i < kSpawnCategories[ci].count && i < 10u; i++) {
            add_row(rows, &n, MAX_MENU_ROWS, kSpawnCategories[ci].items[i].label, "SPAWN",
                    kSpawnCategories[ci].items[i].name, ROW_ACTION, ITEM_SPAWN_CAT_ITEM, i, 0);
        }
        break;
    }

    case ORBX_SUB_ADDON_SPAWN: {
        *out_title = "SPAWN / ADD-ONS";
        add_row(rows, &n, MAX_MENU_ROWS, "Type Model Name...", "TYPE", "Open keyboard to spawn by model", ROW_ACTION, ITEM_SPAWN_KEYBOARD, 0, 0);
        uint32_t cnt = oc->menu.addon_car_count;
        if (cnt > ORBX_MAX_ADDON_CARS) cnt = ORBX_MAX_ADDON_CARS;
        for (uint32_t i = 0; i < cnt; i++) {
            const char *lbl = oc->menu.addon_cars[i].label[0] ? oc->menu.addon_cars[i].label : oc->menu.addon_cars[i].name;
            const char *bdg = oc->menu.addon_cars[i].name[0]  ? oc->menu.addon_cars[i].name  : "SPAWN";
            add_row(rows, &n, MAX_MENU_ROWS, lbl, bdg, "Spawn add-on vehicle", ROW_ACTION, ITEM_ADDON_IDX, (uint8_t)i, 0);
        }
        break;
    }

    case ORBX_SUB_WORLD:
        *out_title = "WORLD";
        add_div(rows, &n, "-- SKY, WEATHER & CLOUDS --");
        add_row(rows, &n, MAX_MENU_ROWS, "Weather", kWeathers[(oc->menu.weather_idx >= 0 && oc->menu.weather_idx < 15) ? oc->menu.weather_idx : 0].label,
                "Change weather condition", ROW_CYCLE, ITEM_CYC_WEATHER, 0, 0);
        add_tog(rows, &n, "Lock Weather",        "Override & hold current weather", ITEM_TOG_WEATHER_PERSIST, oc->menu.weather_persist);
        add_row(rows, &n, MAX_MENU_ROWS, "Clouds & NVE Skyframes", "OPEN", "Cloud hats, NVE keyframes & opacity", ROW_SUB, ITEM_SUB_CLOUDS, ORBX_SUB_CLOUDS, 0);
        add_div(rows, &n, "-- TELEPORTS & PLACES --");
        add_row(rows, &n, MAX_MENU_ROWS, "Teleport Landmarks",   "OPEN", "Waypoint & 14 quick landmarks", ROW_SUB, ITEM_SUB_TELEPORT, ORBX_SUB_TELEPORT, 0);
        add_div(rows, &n, "-- TIME & PHYSICS --");
        int disp_h = oc->menu.lock_hour;
        if (disp_h < 0 && oc->menu.natives[ORBX_NAT_GET_CLOCK_HOURS]) {
            disp_h = (int)nat0(oc, ORBX_NAT_GET_CLOCK_HOURS);
        }
        if (disp_h >= 0 && disp_h <= 23) {
            pos = 0;
            if (disp_h < 10) buf[pos++] = '0';
            pos = orbx_pic_fmt_u64(buf, pos, sizeof(buf) - 4, (uint64_t)disp_h);
            pos = orbx_pic_fmt_str(buf, pos, sizeof(buf) - 1, ":00");
            buf[pos] = '\0';
        } else {
            copy_cstr(buf, sizeof(buf), "12:00");
        }
        add_row(rows, &n, MAX_MENU_ROWS, "Clock Time",    buf, "Press Left/Right for -1h / +1h", ROW_CYCLE, ITEM_CYC_TIME, 0, 0);
        add_tog(rows, &n, "Freeze Clock",  "Pause time progression completely", ITEM_TOG_CLOCK_PAUSE, oc->menu.clock_paused);
        add_row(rows, &n, MAX_MENU_ROWS, "Reset Dynamic Clock", "RUN", "Restore continuous 24h day/night cycle", ROW_ACTION, ITEM_ACT_RESET_TIME, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Slow-Motion",   kTimeScaleNames[(oc->menu.time_scale_idx < 4u) ? oc->menu.time_scale_idx : 0u],
                "Adjust world simulation speed", ROW_CYCLE, ITEM_CYC_TIME_SCALE, 0, 0);
        add_tog(rows, &n, "Moon Gravity",  "Low gravity physics for jumps & cars", ITEM_TOG_LOW_GRAV, oc->menu.low_gravity);
        add_tog(rows, &n, "Mobile Radio",  "Listen to vehicle radio while on foot", ITEM_TOG_MOBILE_RADIO, oc->menu.mobile_radio);
        add_tog(rows, &n, "Calmer Police AI", "Realistic arrival times & non-psychic search", ITEM_TOG_CALMER_POLICE, oc->menu.calmer_police_on);
        pos = orbx_pic_fmt_u64(buf, 0, sizeof(buf) - 2, oc->menu.density_pct); buf[pos++] = '%'; buf[pos] = '\0';
        add_row(rows, &n, MAX_MENU_ROWS, "Traffic / Ped", buf, "Population density multiplier", ROW_CYCLE, ITEM_CYC_DENSITY, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Clear Nearby Peds",     "RUN", "Remove NPCs within 150m",     ROW_ACTION, ITEM_ACT_CLEAR_PEDS, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Clear Nearby Vehicles", "RUN", "Remove traffic within 150m",  ROW_ACTION, ITEM_ACT_CLEAR_VEHS, 0, 0);
        break;

    case ORBX_SUB_CLOUDS: {
        *out_title = "WORLD / CLOUDS";
        int32_t chi = oc->menu.cloud_hat_idx;
        const char *hat_lbl = (chi >= 0 && chi < 16) ? kCloudHats[chi].label : "None";
        add_row(rows, &n, MAX_MENU_ROWS, "Cloud Hat (Auto)", hat_lbl,
                "Loads NVE/vanilla cloud hat immediately", ROW_CYCLE, ITEM_CYC_CLOUD_HAT_CHOICE, 0, 0);
        int32_t kfi = oc->menu.cloud_keyframe_idx;
        const char *kf_lbl = (kfi >= 0 && kfi < 14) ? kCloudKeyframes[kfi].label : "Weather Default";
        add_row(rows, &n, MAX_MENU_ROWS, "NVE Cloud Keyframe", kf_lbl,
                "Apply NVE cloudkeyframes.xml atmosphere", ROW_CYCLE, ITEM_CYC_CLOUD_KEYFRAME, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Reload Cloud Hat",  "RUN", "Force reload active cloud hat", ROW_ACTION, ITEM_ACT_APPLY_CLOUD_HAT, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Clear Clouds / KF", "RUN", "Unload all hats & reset NVE keyframe", ROW_ACTION, ITEM_ACT_CLEAR_CLOUDS, 0, 0);
        pos = orbx_pic_fmt_u64(buf, 0, sizeof(buf) - 2, oc->menu.cloud_alpha_pct); buf[pos++] = '%'; buf[pos] = '\0';
        add_row(rows, &n, MAX_MENU_ROWS, "Cloud Opacity", buf, "Cloud alpha density (0..100%)", ROW_CYCLE, ITEM_CYC_CLOUD_ALPHA, 0, 0);
        break;
    }

    case ORBX_SUB_TELEPORT:
        *out_title = "WORLD / TELEPORT";
        add_row(rows, &n, MAX_MENU_ROWS, "To Map Waypoint", "TP", "Smart Z-probe teleport to purple marker", ROW_ACTION, ITEM_TP_WP, 0, 0);
        add_div(rows, &n, "-- LANDMARKS --");
        for (size_t i = 0; i < (sizeof(kTeleports) / sizeof(kTeleports[0])); i++) {
            add_row(rows, &n, MAX_MENU_ROWS, kTeleports[i].name, "TP", "Teleport directly here", ROW_ACTION, ITEM_TP_IDX, (uint8_t)i, 0);
        }
        break;

    case ORBX_SUB_CAMERA: {
        *out_title = "CAMERA & FOV";
        add_row(rows, &n, MAX_MENU_ROWS, "ENB & ReShade GFX", "OPEN", "Color grading, HDR bloom & film grain", ROW_SUB, ITEM_SUB_GRAPHICS, ORBX_SUB_GRAPHICS, 0);
        add_div(rows, &n, "-- ALTERNATE FIRST PERSON --");
        uint32_t mi = (oc->menu.afp_mode < 6u) ? oc->menu.afp_mode : 0u;
        add_row(rows, &n, MAX_MENU_ROWS, "Camera Mode", kAfpModeNames[mi], "Alt FP, Bodycam, Shoulder, Dash, Helmet", ROW_CYCLE, ITEM_CYC_AFP_MODE, 0, 0);
        uint32_t fi = (oc->menu.afp_fov_idx < 8u) ? oc->menu.afp_fov_idx : 3u;
        add_row(rows, &n, MAX_MENU_ROWS, "Field of View (FOV)", kAfpFovNames[fi], "Field of view angle (75-110 deg)", ROW_CYCLE, ITEM_CYC_AFP_FOV, 0, 0);
        uint32_t bi = (oc->menu.afp_headbob < 4u) ? oc->menu.afp_headbob : 0u;
        add_row(rows, &n, MAX_MENU_ROWS, "Head Bobbing", kAfpBobNames[bi], "Procedural gait & breathing sway", ROW_CYCLE, ITEM_CYC_AFP_HEADBOB, 0, 0);
        uint32_t di = (oc->menu.afp_smooth_follow < 4u) ? oc->menu.afp_smooth_follow : 0u;
        add_row(rows, &n, MAX_MENU_ROWS, "Bodycam Drift", kAfpDriftNames[di], "Authentic loose handheld lag", ROW_CYCLE, ITEM_CYC_AFP_DRIFT, 0, 0);
        uint32_t ri = (oc->menu.afp_recoil < 4u) ? oc->menu.afp_recoil : 0u;
        add_row(rows, &n, MAX_MENU_ROWS, "Weapon Recoil", kAfpRecoilNames[ri], "Firing camera shake & lateral kick", ROW_CYCLE, ITEM_CYC_AFP_RECOIL, 0, 0);
        add_tog(rows, &n, "Vehicle Inertia", "Camera pitch on accel/braking", ITEM_TOG_AFP_INERTIA, oc->menu.afp_inertia);
        add_tog(rows, &n, "Crouch Lowering", "Lower camera smoothly when crouching", ITEM_TOG_AFP_CROUCH, oc->menu.afp_crouch_lower);
        uint32_t zi = (oc->menu.afp_ads_zoom < 4u) ? oc->menu.afp_ads_zoom : 0u;
        add_row(rows, &n, MAX_MENU_ROWS, "ADS Aim Zoom", kAfpAdsNames[zi], "Smooth FOV zoom while aiming", ROW_CYCLE, ITEM_CYC_AFP_ADS_ZOOM, 0, 0);
        uint32_t ci = (oc->menu.afp_crosshair < 3u) ? oc->menu.afp_crosshair : 0u;
        add_row(rows, &n, MAX_MENU_ROWS, "Dot Crosshair", kAfpCrosshairNames[ci], "Center tactical aiming dot", ROW_CYCLE, ITEM_CYC_AFP_CROSSHAIR, 0, 0);
        add_tog(rows, &n, "Auto-Turn Body", "Align character to camera yaw", ITEM_TOG_AFP_AUTOTURN, oc->menu.afp_autoturn);
        add_tog(rows, &n, "Near-Clip Fix", "Eliminate weapon/collar clipping", ITEM_TOG_AFP_CLIP_FIX, oc->menu.afp_clip_fix);
        add_row(rows, &n, MAX_MENU_ROWS, "Reset AFP Settings", "RESET", "Restore default camera settings", ROW_ACTION, ITEM_ACT_AFP_RESET, 0, 0);

        add_div(rows, &n, "-- STOCK GAME CAMERA --");
        uint32_t pi = cam_mode_to_opt_idx(oc->menu.cam_ped_view_mode);
        uint32_t vi = cam_mode_to_opt_idx(oc->menu.cam_veh_view_mode);
        add_row(rows, &n, MAX_MENU_ROWS, "On-Foot View",  kCamViewOptions[pi].ped_label, "Stock third/first-person camera mode", ROW_CYCLE, ITEM_CYC_PED_VIEW, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Vehicle View",  kCamViewOptions[vi].veh_label, "Stock vehicle/cockpit camera mode",    ROW_CYCLE, ITEM_CYC_VEH_VIEW, 0, 0);
        add_tog(rows, &n, "Cinematic Camera",  "Toggle GTA V theatrical cinematic cam",  ITEM_TOG_CINEMATIC_CAM, oc->menu.cinematic_cam);
        add_tog(rows, &n, "No Camera Shake",   "Eliminate explosion & hit camera shake", ITEM_TOG_CAM_ANTI_SHAKE, oc->menu.cam_anti_shake);
        add_row(rows, &n, MAX_MENU_ROWS, "Reset Cam Behind", "RUN", "Center camera behind player", ROW_ACTION, ITEM_ACT_CAM_RESET_BEHIND, 0, 0);
        break;
    }

    case ORBX_SUB_GRAPHICS: {
        *out_title = "CAMERA / GFX";

        /* --- STAGE 1: ANTI-ALIASING & LOD DISTANCE --- */
        add_div(rows, &n, "-- STAGE 1: ANTI-ALIASING & LOD --");
        const char *aa_lbl = "OFF";
        if (oc->menu.enb_extra_active == 1u)      aa_lbl = "LV1 Soft";
        else if (oc->menu.enb_extra_active == 2u) aa_lbl = "LV2 SMAA";
        else if (oc->menu.enb_extra_active == 3u) aa_lbl = "LV3 Smooth";
        else if (oc->menu.enb_extra_active == 4u) aa_lbl = "LV4 Ultra";
        else if (oc->menu.enb_extra_active >= 5u) aa_lbl = "LV5 Max Clean";
        add_row(rows, &n, MAX_MENU_ROWS, "Anti-Aliasing", aa_lbl,
                "Eliminate pixelation, grain & jagged shimmer", ROW_CYCLE, ITEM_CYC_ENB_EXTRA, 0, 0);

        const char *sc_lbl = "1.0x Vanilla";
        if (oc->menu.enb_shadow_scale == 15u)      sc_lbl = "1.5x High";
        else if (oc->menu.enb_shadow_scale == 20u) sc_lbl = "2.0x Ultra";
        else if (oc->menu.enb_shadow_scale == 25u) sc_lbl = "2.5x Extreme";
        else if (oc->menu.enb_shadow_scale == 35u) sc_lbl = "3.5x Horizon";
        else if (oc->menu.enb_shadow_scale >= 50u) sc_lbl = "5.0x Max HD";
        add_row(rows, &n, MAX_MENU_ROWS, "LOD & Draw Distance", sc_lbl,
                "High-poly building models & extended draw range", ROW_CYCLE, ITEM_CYC_ENB_SHADOW_SCALE, 0, 0);

        const char *mb_lbl = "OFF (NVE Base)";
        if (oc->menu.enb_motion_blur == 1u)      mb_lbl = "LV1 Subtle";
        else if (oc->menu.enb_motion_blur == 2u) mb_lbl = "LV2 Smooth";
        else if (oc->menu.enb_motion_blur == 3u) mb_lbl = "LV3 NVE Classic";
        else if (oc->menu.enb_motion_blur == 4u) mb_lbl = "LV4 High Speed";
        else if (oc->menu.enb_motion_blur >= 5u) mb_lbl = "LV5 Cinema";
        add_row(rows, &n, MAX_MENU_ROWS, "NVE Motion Blur", mb_lbl,
                "Per-pixel camera & high-speed velocity blur", ROW_CYCLE, ITEM_CYC_ENB_MOTIONBLUR, 0, 0);

        const char *df_lbl = "OFF";
        if (oc->menu.enb_dof_lvl == 1u)      df_lbl = "LV1 Subtle";
        else if (oc->menu.enb_dof_lvl == 2u) df_lbl = "LV2 Cinematic";
        else if (oc->menu.enb_dof_lvl == 3u) df_lbl = "LV3 Portrait";
        else if (oc->menu.enb_dof_lvl == 4u) df_lbl = "LV4 Macro";
        else if (oc->menu.enb_dof_lvl >= 5u) df_lbl = "LV5 Ultra Bokeh";
        add_row(rows, &n, MAX_MENU_ROWS, "NVE Depth of Field", df_lbl,
                "High-precision bokeh background blur (Hi-DOF)", ROW_CYCLE, ITEM_CYC_ENB_DOF, 0, 0);

        /* --- STAGE 2: NVE / ENB SHADOWS, AO & FX (STACKABLE) --- */
        add_div(rows, &n, "-- STAGE 2: NVE SHADOWS & FX --");
        const char *sh_lbl = "OFF (NVE Base)";
        if (oc->menu.enb_shadow_soft == 1u)      sh_lbl = "LV1 +25% Range";
        else if (oc->menu.enb_shadow_soft == 2u) sh_lbl = "LV2 +50% Roof";
        else if (oc->menu.enb_shadow_soft == 3u) sh_lbl = "LV3 +100% Deep";
        else if (oc->menu.enb_shadow_soft == 4u) sh_lbl = "LV4 +150% Far";
        else if (oc->menu.enb_shadow_soft >= 5u) sh_lbl = "LV5 +200% Max";
        add_row(rows, &n, MAX_MENU_ROWS, "Dynamic Shadows (3D)", sh_lbl,
                "Extend 3D cascade shadow depth (never overwrites NVE)", ROW_CYCLE, ITEM_CYC_ENB_SHADOW, 0, 0);

        const char *ao_lbl = "OFF (NVE Base)";
        if (oc->menu.enb_ssao_boost == 1u)      ao_lbl = "LV1 Subtle AO";
        else if (oc->menu.enb_ssao_boost == 2u) ao_lbl = "LV2 Deep HBAO";
        else if (oc->menu.enb_ssao_boost == 3u) ao_lbl = "LV3 Dark AO";
        else if (oc->menu.enb_ssao_boost == 4u) ao_lbl = "LV4 Heavy AO";
        else if (oc->menu.enb_ssao_boost >= 5u) ao_lbl = "LV5 Max HBAO";
        add_row(rows, &n, MAX_MENU_ROWS, "NVE Contact AO (HBAO)", ao_lbl,
                "Stackable building & vehicle ambient occlusion", ROW_CYCLE, ITEM_CYC_ENB_SSAO, 0, 0);

        const char *rf_lbl = "OFF (NVE Base)";
        if (oc->menu.enb_refl_lvl == 1u)      rf_lbl = "LV1 Glossy";
        else if (oc->menu.enb_refl_lvl == 2u) rf_lbl = "LV2 Chrome";
        else if (oc->menu.enb_refl_lvl == 3u) rf_lbl = "LV3 Mirror";
        else if (oc->menu.enb_refl_lvl == 4u) rf_lbl = "LV4 Raytraced";
        else if (oc->menu.enb_refl_lvl >= 5u) rf_lbl = "LV5 Max Mirror";
        add_row(rows, &n, MAX_MENU_ROWS, "NVE Reflections", rf_lbl,
                "Stackable glossy reflections on cars & glass", ROW_CYCLE, ITEM_CYC_ENB_REFL, 0, 0);

        const char *bl_lbl = "OFF (NVE Base)";
        if (oc->menu.enb_bloom_lvl == 1u)      bl_lbl = "LV1 Warm";
        else if (oc->menu.enb_bloom_lvl == 2u) bl_lbl = "LV2 Radiant";
        else if (oc->menu.enb_bloom_lvl == 3u) bl_lbl = "LV3 High HDR";
        else if (oc->menu.enb_bloom_lvl == 4u) bl_lbl = "LV4 Ultra Neon";
        else if (oc->menu.enb_bloom_lvl >= 5u) bl_lbl = "LV5 Extreme";
        add_row(rows, &n, MAX_MENU_ROWS, "NVE Bloom & Glow", bl_lbl,
                "Stackable HDR streetlight, neon & headlight glow", ROW_CYCLE, ITEM_CYC_ENB_BLOOM, 0, 0);

        /* --- BASE GRAPHICS PRESET --- */
        add_div(rows, &n, "-- BASE GRAPHICS PRESET --");
        uint32_t gi = (oc->menu.gfx_preset_idx < 15u) ? oc->menu.gfx_preset_idx : 0u;
        add_row(rows, &n, MAX_MENU_ROWS, "Active Preset", kGfxPresets[gi].label, kGfxPresets[gi].desc, ROW_CYCLE, ITEM_CYC_GFX_PRESET, 0, 0);
        uint32_t sp = oc->menu.gfx_strength_pct;
        if (sp < 10u || sp > 200u) sp = 100u;
        pos = orbx_pic_fmt_u64(buf, 0, sizeof(buf) - 2, sp); buf[pos++] = '%'; buf[pos] = '\0';
        add_row(rows, &n, MAX_MENU_ROWS, "Filter Strength", buf, "Blend intensity (10..200%)", ROW_CYCLE, ITEM_CYC_GFX_STRENGTH, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Lens Post-FX", kPostFxPresets[(oc->menu.gfx_postfx_idx < 7u) ? oc->menu.gfx_postfx_idx : 0u].label,
                "Optional screen lens FX shader", ROW_CYCLE, ITEM_CYC_GFX_POSTFX, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Restore Original GFX", "RESET", "Clear all overlays & reset shadows", ROW_ACTION, ITEM_ACT_GFX_RESET, 0, 0);
        add_div(rows, &n, "-- QUICK PRESET LIST --");
        for (size_t i = 1; i < 15u; i++) {
            add_row(rows, &n, MAX_MENU_ROWS, kGfxPresets[i].label, (gi == (uint32_t)i) ? "ACTIVE" : "APPLY",
                    kGfxPresets[i].desc, ROW_ACTION, ITEM_ACT_GFX_QUICK, (uint8_t)i, (gi == (uint32_t)i) ? 1u : 0u);
        }
        break;
    }

    case ORBX_SUB_SETTINGS:
        *out_title = "SETTINGS";
        add_row(rows, &n, MAX_MENU_ROWS, "Keybinds & Hotkeys", "OPEN", "Customize menu combo & quick hotkeys", ROW_SUB, ITEM_SUB_KEYBINDS, ORBX_SUB_KEYBINDS, 0);
        add_div(rows, &n, "-- DISPLAY & ACCESSIBILITY --");
        add_row(rows, &n, MAX_MENU_ROWS, "Menu Position", kMenuPosNames[(oc->menu.menu_position < 3u) ? oc->menu.menu_position : 0u],
                "Left, Right, or Center screen align", ROW_CYCLE, ITEM_CYC_MENU_POS, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Menu Scale", kMenuScaleNames[(oc->menu.menu_scale < 3u) ? oc->menu.menu_scale : 1u],
                "Compact, Normal, or Large readable text", ROW_CYCLE, ITEM_CYC_MENU_SCALE, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Theme Accent", kThemePresets[(oc->menu.theme_accent < 6u) ? oc->menu.theme_accent : 0u].name,
                "Highlight & border accent color", ROW_CYCLE, ITEM_CYC_THEME_ACCENT, 0, 0);
        add_tog(rows, &n, "Sound Effects",      "Frontend UI click sounds",         ITEM_TOG_SOUND_FX,   oc->menu.sound_enabled);
        add_tog(rows, &n, "Keep Driving Open",  "Keep L-Stick & triggers active",   ITEM_TOG_KEEP_STEER, oc->menu.keep_steering);
        add_tog(rows, &n, "L-Stick Navigation", "Allow Left Stick to scroll menu",  ITEM_TOG_NAV_STICK,  oc->menu.nav_stick);
        add_div(rows, &n, "-- PERSISTENCE --");
        add_row(rows, &n, MAX_MENU_ROWS, "Save Settings INI", "SAVE", "Save UX preferences & keybinds to SSD", ROW_ACTION, ITEM_ACT_SAVE_INI, 0, 0);
        break;

    case ORBX_SUB_KEYBINDS:
        *out_title = "SETTINGS / KEYS";
        add_div(rows, &n, "-- MENU CONTROLS (X=REBIND) --");
        add_keybind_row(rows, &n, oc, "Open / Close Combo", oc->menu.bind_open_mod, oc->menu.bind_open_btn, KB_SLOT_OPEN, "Tap 1 button or hold 2 buttons to bind");
        add_keybind_row(rows, &n, oc, "Select / Toggle",    0u, oc->menu.bind_accept,   KB_SLOT_ACCEPT,   "Button to activate selected row");
        add_keybind_row(rows, &n, oc, "Back / Close Menu",  0u, oc->menu.bind_cancel,   KB_SLOT_CANCEL,   "Button to go back or close menu");
        add_keybind_row(rows, &n, oc, "Prev Category Tab",  0u, oc->menu.bind_tab_prev, KB_SLOT_TAB_PREV, "Previous top tab");
        add_keybind_row(rows, &n, oc, "Next Category Tab",  0u, oc->menu.bind_tab_next, KB_SLOT_TAB_NEXT, "Next top tab");
        add_div(rows, &n, "-- QUICK HOTKEYS (CLOSED) --");
        add_keybind_row(rows, &n, oc, "Horn Boost Hold",    0u, oc->menu.bind_horn_boost,   KB_SLOT_HORN_BOOST,   "Hold in car for turbo (Square=Clear)");
        add_keybind_row(rows, &n, oc, "Airbreak Toggle",    0u, oc->menu.bind_noclip,       KB_SLOT_NOCLIP,       "Toggle No-Clip flight (Square=Clear)");
        add_keybind_row(rows, &n, oc, "Quick Repair Car",   0u, oc->menu.bind_quick_repair, KB_SLOT_QUICK_REPAIR, "Fix & wash car (Square=Clear)");
        add_keybind_row(rows, &n, oc, "Quick Heal & Armor", 0u, oc->menu.bind_quick_heal,   KB_SLOT_QUICK_HEAL,   "Full HP & armor (Square=Clear)");
        add_keybind_row(rows, &n, oc, "Super Brake Hold",   0u, oc->menu.bind_super_brake,  KB_SLOT_SUPER_BRAKE,  "Instant dead-stop in car (Square=Clear)");
        add_div(rows, &n, "-- SAVE OR RESET --");
        add_row(rows, &n, MAX_MENU_ROWS, "Clear Optional Hotkeys", "CLR",  "Unbind all 5 quick hotkeys cleanly", ROW_ACTION, ITEM_ACT_KEYBIND_CLEAR, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Reset Default Keys",     "RST",  "Restore R1+Down, X/O, L1/R1, L3",    ROW_ACTION, ITEM_ACT_KEYBIND_RESET, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Save Keybinds to SSD",   "SAVE", "Write keybinds to /data/modHEN/config.ini", ROW_ACTION, ITEM_ACT_SAVE_INI, 0, 0);
        break;

    case ORBX_SUB_MAIN:
    default:
        *out_title = "HOME";
        add_div(rows, &n, "-- CATEGORIES (L1/R1 TABS) --");
        add_row(rows, &n, MAX_MENU_ROWS, "Self Options",        "OPEN", "Godmode, Airbreak, wanted, speed, stealth", ROW_SUB, ITEM_SUB_PLAYER,   ORBX_SUB_PLAYER,   0);
        add_row(rows, &n, MAX_MENU_ROWS, "Weapon Options",      "OPEN", "All weapons, infinite clip, explosive ammo",ROW_SUB, ITEM_SUB_WEAPONS,  ORBX_SUB_WEAPONS,  0);
        add_row(rows, &n, MAX_MENU_ROWS, "Vehicle Options",     "OPEN", "LSC, paint, doors, seatbelt, speedo",       ROW_SUB, ITEM_SUB_VEHICLE,  ORBX_SUB_VEHICLE,  0);
        add_row(rows, &n, MAX_MENU_ROWS, "Vehicle Spawner",     "OPEN", "6 car classes & add-on DLC vehicles",       ROW_SUB, ITEM_SUB_SPAWN,    ORBX_SUB_SPAWN,    0);
        add_row(rows, &n, MAX_MENU_ROWS, "World, Clouds & TP",  "OPEN", "Weather, NVE clouds, slow-mo, IPL/TP",      ROW_SUB, ITEM_SUB_WORLD,    ORBX_SUB_WORLD,    0);
        add_row(rows, &n, MAX_MENU_ROWS, "Camera, POV & GFX",   "OPEN", "First-person FOV, view modes, ENB/ReShade", ROW_SUB, ITEM_SUB_CAMERA,   ORBX_SUB_CAMERA,   0);
        add_row(rows, &n, MAX_MENU_ROWS, "Settings & Keybinds", "OPEN", "Remap buttons, layout scale, save config",  ROW_SUB, ITEM_SUB_SETTINGS, ORBX_SUB_SETTINGS, 0);
        add_div(rows, &n, "-- QUICK ESSENTIALS --");
        add_row(rows, &n, MAX_MENU_ROWS, "Teleport to Waypoint","TP",  "Smart Z-probed waypoint jump",           ROW_ACTION, ITEM_TP_WP,           0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Repair & Wash Car",   "RUN", "Fix & clean current vehicle",            ROW_ACTION, ITEM_ACT_FIX_VEH,     0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Heal & Full Armor",   "RUN", "Restore HP & 100% armor immediately",    ROW_ACTION, ITEM_ACT_HEAL_ARMOR,  0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Clear Wanted Stars",  "RUN", "Drop active police pursuit immediately", ROW_ACTION, ITEM_ACT_CLEAR_WANTED,0, 0);
        add_tog(rows, &n, "Airbreak / No-Clip", "Instant toggle fly-through-walls mode", ITEM_TOG_NOCLIP, oc->menu.noclip_on);
        break;
    }
    *out_cnt = n;
}

static void unbind_quick_slot(OrbxNativeContext *oc, uint8_t slot) {
    if      (slot == KB_SLOT_HORN_BOOST)   oc->menu.bind_horn_boost   = 0u;
    else if (slot == KB_SLOT_NOCLIP)       oc->menu.bind_noclip       = 0u;
    else if (slot == KB_SLOT_QUICK_REPAIR) oc->menu.bind_quick_repair = 0u;
    else if (slot == KB_SLOT_QUICK_HEAL)   oc->menu.bind_quick_heal   = 0u;
    else if (slot == KB_SLOT_SUPER_BRAKE)  oc->menu.bind_super_brake  = 0u;
    else return;
    recompute_suppress_mask(oc);
    oc->menu.cfg_save_req = 1u;
    set_toast(oc, "Quick Hotkey Unbound");
}

static void execute_row(OrbxNativeContext *oc, const MenuRow *r, int dir) {
    switch (r->id) {
    /* Submenus */
    case ITEM_SUB_SPAWN_CAT:
        oc->menu.spawn_cat_idx = r->arg;
        push_submenu(oc, ORBX_SUB_SPAWN_CAT);
        break;
    case ITEM_SUB_VEH_CUSTOM: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD_KIT]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_MOD_KIT, (uint64_t)veh, 0u);
        }
        push_submenu(oc, r->arg);
        break;
    }
    case ITEM_SUB_PLAYER:
    case ITEM_SUB_WEAPONS:
    case ITEM_SUB_VEHICLE:
    case ITEM_SUB_SPAWN:
    case ITEM_SUB_TELEPORT:
    case ITEM_SUB_WORLD:
    case ITEM_SUB_ADDON_SPAWN:
    case ITEM_SUB_VEH_COLORS:
    case ITEM_SUB_VEH_LIGHTS:
    case ITEM_SUB_VEH_DOORS:
    case ITEM_SUB_CAMERA:
    case ITEM_SUB_CLOUDS:
    case ITEM_SUB_SETTINGS:
    case ITEM_SUB_GRAPHICS:
    case ITEM_SUB_KEYBINDS:
    case ITEM_SUB_IPL_INTERIORS:
        push_submenu(oc, r->arg);
        break;

    /* Player / Self options */
    case ITEM_TOG_GODMODE:
        oc->menu.godmode = !oc->menu.godmode ? 1u : 0u;
        set_toast(oc, oc->menu.godmode ? "God Mode ON" : "God Mode OFF");
        break;
    case ITEM_TOG_NEVER_WANTED:
        oc->menu.never_wanted = !oc->menu.never_wanted ? 1u : 0u;
        set_toast(oc, oc->menu.never_wanted ? "Never Wanted ON" : "Never Wanted OFF");
        break;
    case ITEM_CYC_WANTED_CAP: {
        int wc = (int)oc->menu.wanted_cap_idx + (dir < 0 ? -1 : 1);
        if (wc > 5) wc = 0;
        if (wc < 0) wc = 5;
        oc->menu.wanted_cap_idx = (uint32_t)wc;
        if (wc > 0) {
            oc->menu.never_wanted = 0u;
            int32_t pl = (int32_t)nat0(oc, ORBX_NAT_PLAYER_ID);
            if (oc->menu.natives[ORBX_NAT_SET_PLAYER_WANTED_LEVEL]) {
                nat3(oc, ORBX_NAT_SET_PLAYER_WANTED_LEVEL, (uint64_t)pl, (uint64_t)wc, 0u);
            }
            if (oc->menu.natives[ORBX_NAT_SET_PLAYER_WANTED_LEVEL_NOW]) {
                nat2(oc, ORBX_NAT_SET_PLAYER_WANTED_LEVEL_NOW, (uint64_t)pl, 0u);
            }
            set_toast(oc, "Wanted Level Locked");
        } else {
            set_toast(oc, "Wanted Level: Normal");
        }
        break;
    }
    case ITEM_TOG_SUPER_JUMP:
        oc->menu.super_jump = !oc->menu.super_jump ? 1u : 0u;
        set_toast(oc, oc->menu.super_jump ? "Super Jump ON" : "Super Jump OFF");
        break;
    case ITEM_TOG_FAST_RUN:
        oc->menu.fast_run = !oc->menu.fast_run ? 1u : 0u;
        set_toast(oc, oc->menu.fast_run ? "Fast Run ON" : "Fast Run OFF");
        break;
    case ITEM_CYC_SPRINT_MULT: {
        int sm = (int)oc->menu.sprint_mult_idx + (dir < 0 ? -1 : 1);
        if (sm >= 3) sm = 0;
        if (sm < 0)  sm = 2;
        oc->menu.sprint_mult_idx = (uint32_t)sm;
        int32_t pl = (int32_t)nat0(oc, ORBX_NAT_PLAYER_ID);
        nat2(oc, ORBX_NAT_SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(kMultSpeeds[sm]));
        set_toast(oc, kMultLabels[sm]);
        break;
    }
    case ITEM_CYC_SWIM_MULT: {
        int wm = (int)oc->menu.swim_mult_idx + (dir < 0 ? -1 : 1);
        if (wm >= 3) wm = 0;
        if (wm < 0)  wm = 2;
        oc->menu.swim_mult_idx = (uint32_t)wm;
        int32_t pl = (int32_t)nat0(oc, ORBX_NAT_PLAYER_ID);
        nat2(oc, ORBX_NAT_SET_SWIM_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(kMultSpeeds[wm]));
        set_toast(oc, kMultLabels[wm]);
        break;
    }
    case ITEM_TOG_NO_RAGDOLL:
        oc->menu.no_ragdoll = !oc->menu.no_ragdoll ? 1u : 0u;
        set_toast(oc, oc->menu.no_ragdoll ? "No Ragdoll ON" : "No Ragdoll OFF");
        break;
    case ITEM_TOG_INVISIBLE: {
        oc->menu.invisible = !oc->menu.invisible ? 1u : 0u;
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        if (ped) {
            if (oc->menu.natives[ORBX_NAT_SET_ENTITY_VISIBLE]) {
                nat3(oc, ORBX_NAT_SET_ENTITY_VISIBLE, (uint64_t)ped, oc->menu.invisible ? 0 : 1, 0);
            }
            if (!oc->menu.invisible) {
                if (oc->menu.natives[ORBX_NAT_RESET_ENTITY_ALPHA]) {
                    nat1(oc, ORBX_NAT_RESET_ENTITY_ALPHA, (uint64_t)ped);
                }
                if (oc->menu.natives[ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE]) {
                    nat1(oc, ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE, (uint64_t)ped);
                }
                oc->menu.applied_flags &= ~APP_INVISIBLE;
            } else {
                oc->menu.applied_flags |= APP_INVISIBLE;
            }
        }
        set_toast(oc, oc->menu.invisible ? "Invisible Ped ON" : "Invisible Ped OFF");
        break;
    }
    case ITEM_ACT_FORCE_VISIBLE: act_force_visible(oc); break;
    case ITEM_TOG_NOCLIP:
        oc->menu.noclip_on = !oc->menu.noclip_on ? 1u : 0u;
        set_toast(oc, oc->menu.noclip_on ? "Airbreak / No-Clip ON" : "Airbreak / No-Clip OFF");
        break;
    case ITEM_CYC_NOCLIP_SPEED: {
        int ns = (int)oc->menu.noclip_speed_idx + (dir < 0 ? -1 : 1);
        if (ns >= 4) ns = 0;
        if (ns < 0)  ns = 3;
        oc->menu.noclip_speed_idx = (uint32_t)ns;
        set_toast(oc, kAirbreakNames[ns]);
        break;
    }
    case ITEM_TOG_SPEC_INF:
        oc->menu.spec_ability_inf = !oc->menu.spec_ability_inf ? 1u : 0u;
        set_toast(oc, oc->menu.spec_ability_inf ? "Inf Special ON" : "Inf Special OFF");
        break;
    case ITEM_ACT_HEAL_ARMOR:   act_heal_armor(oc); break;
    case ITEM_ACT_CLEAR_WANTED: act_clear_wanted(oc); break;

    /* Weapon toggles & actions */
    case ITEM_ACT_GIVE_WEAPONS: act_give_weapons(oc); break;
    case ITEM_ACT_REMOVE_WEAPONS: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        if (oc->menu.natives[ORBX_NAT_REMOVE_ALL_PED_WEAPONS]) {
            nat2(oc, ORBX_NAT_REMOVE_ALL_PED_WEAPONS, (uint64_t)ped, 1u);
            set_toast(oc, "All Weapons Stripped");
        }
        break;
    }
    case ITEM_TOG_INF_AMMO:
        oc->menu.inf_ammo ^= 1u;
        set_toast(oc, oc->menu.inf_ammo ? "Infinite Clip ON" : "Infinite Clip OFF");
        break;
    case ITEM_TOG_EXP_AMMO:
        oc->menu.explosive_ammo ^= 1u;
        set_toast(oc, oc->menu.explosive_ammo ? "Explosive Bullets ON" : "Explosive Bullets OFF");
        break;
    case ITEM_TOG_FIRE_AMMO:
        oc->menu.fire_ammo ^= 1u;
        set_toast(oc, oc->menu.fire_ammo ? "Fire Bullets ON" : "Fire Bullets OFF");
        break;
    case ITEM_TOG_EXP_MELEE:
        oc->menu.explosive_melee ^= 1u;
        set_toast(oc, oc->menu.explosive_melee ? "Explosive Melee ON" : "Explosive Melee OFF");
        break;

    /* Vehicle general */
    case ITEM_TOG_VEH_GOD:
        oc->menu.veh_god ^= 1u;
        set_toast(oc, oc->menu.veh_god ? "Vehicle God Mode ON" : "Vehicle God Mode OFF");
        break;
    case ITEM_TOG_SEATBELT:
        oc->menu.seatbelt_on ^= 1u;
        set_toast(oc, oc->menu.seatbelt_on ? "Seatbelt ON" : "Seatbelt OFF");
        break;
    case ITEM_TOG_HORN_BOOST:
        oc->menu.horn_boost ^= 1u;
        set_toast(oc, oc->menu.horn_boost ? "Horn Boost ON" : "Horn Boost OFF");
        break;
    case ITEM_TOG_VEH_DRIFT:
        oc->menu.veh_drift_mode ^= 1u;
        set_toast(oc, oc->menu.veh_drift_mode ? "Drift Mode ON" : "Drift Mode OFF");
        break;
    case ITEM_CYC_VEH_TORQUE: {
        int tq = (int)oc->menu.veh_torque_idx + (dir < 0 ? -1 : 1);
        if (tq >= 4) tq = 0;
        if (tq < 0)  tq = 3;
        oc->menu.veh_torque_idx = (uint32_t)tq;
        set_toast(oc, kTorqueLabels[tq]);
        break;
    }
    case ITEM_TOG_BULLETPROOF_TIRES: {
        oc->menu.veh_bulletproof_tyres ^= 1u;
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST, (uint64_t)veh, oc->menu.veh_bulletproof_tyres ? 0 : 1);
        }
        set_toast(oc, oc->menu.veh_bulletproof_tyres ? "Bulletproof Tires ON" : "Bulletproof Tires OFF");
        break;
    }
    case ITEM_CYC_SPEEDO_HUD: {
        int sm = (int)oc->menu.speedo_mode + (dir < 0 ? -1 : 1);
        if (sm >= 3) sm = 0;
        if (sm < 0)  sm = 2;
        oc->menu.speedo_mode = (uint32_t)sm;
        set_toast(oc, kSpeedoNames[sm]);
        break;
    }
    case ITEM_ACT_WARP_NEAREST_VEH: act_warp_nearest_veh(oc); break;
    case ITEM_ACT_FIX_VEH:          act_fix_vehicle(oc); break;
    case ITEM_ACT_WASH_VEH: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_DIRT_LEVEL, (uint64_t)veh, f2u(0.0f));
            set_toast(oc, "Vehicle Washed");
        } else {
            set_toast(oc, "Not in a vehicle");
        }
        break;
    }
    case ITEM_ACT_FLIP_VEH:         act_flip_vehicle(oc); break;
    case ITEM_ACT_BOOST_VEH:        act_boost_vehicle(oc, 85.0f); break;
    case ITEM_ACT_PLATE_MODHEN:     act_plate_vehicle(oc, "MODHEN"); break;
    case ITEM_ACT_PLATE_OXAGON:     act_plate_vehicle(oc, "OXAGON"); break;
    case ITEM_ACT_PLATE_KEYBOARD:   act_open_keyboard(oc, 1u); break;
    case ITEM_ACT_DOORS_OPEN_ALL:   act_door_op(oc, 0, 0); break;
    case ITEM_ACT_DOORS_SHUT_ALL:   act_door_op(oc, 1, 0); break;
    case ITEM_TOG_DOORS_LOCK: {
        oc->menu.veh_doors_locked ^= 1u;
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_DOORS_LOCKED]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_DOORS_LOCKED, (uint64_t)veh, oc->menu.veh_doors_locked ? 2u : 1u);
        }
        set_toast(oc, oc->menu.veh_doors_locked ? "Doors Locked" : "Doors Unlocked");
        break;
    }
    case ITEM_ACT_DOOR_TOGGLE:  act_door_op(oc, 2, (int)r->arg); break;
    case ITEM_ACT_EXTRA_TOGGLE: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_EXTRA]) {
            nat3(oc, ORBX_NAT_SET_VEHICLE_EXTRA, (uint64_t)veh, (uint64_t)r->arg, 0u);
            set_toast(oc, "Vehicle Extra Applied");
        } else {
            set_toast(oc, "Not in a vehicle");
        }
        break;
    }
    case ITEM_TOG_REALISTIC_FUEL:
        oc->menu.fuel_system_on ^= 1u;
        if (oc->menu.fuel_system_on && oc->menu.fuel_level <= 0.0f) {
            oc->menu.fuel_level = 100.0f;
        }
        set_toast(oc, oc->menu.fuel_system_on ? "Realistic Fuel ON" : "Realistic Fuel OFF");
        break;
    case ITEM_ACT_REFUEL_VEH:
        oc->menu.fuel_level = 100.0f;
        play_ui_sound(oc, "SELECT");
        set_toast(oc, "Vehicle Fuel: 100% Full");
        break;
    case ITEM_TOG_GTA6_HUD:
        oc->menu.gta6_hud_on ^= 1u;
        set_toast(oc, oc->menu.gta6_hud_on ? "GTA VI Immersive HUD ON" : "GTA VI Immersive HUD OFF");
        break;
    case ITEM_TOG_GTA6_WHEEL:
        oc->menu.gta6_wheel_on ^= 1u;
        set_toast(oc, oc->menu.gta6_wheel_on ? "GTA VI Weapon Wheel ON" : "GTA VI Weapon Wheel OFF");
        break;

    /* Vehicle Color Customization */
    case ITEM_CYC_COLOR_TARGET: {
        int tgt = (int)oc->menu.veh_color_target + (dir < 0 ? -1 : 1);
        if (tgt >= 5) tgt = 0;
        if (tgt < 0)  tgt = 4;
        oc->menu.veh_color_target = (uint32_t)tgt;
        break;
    }
    case ITEM_CYC_COLOR_CAT: {
        int cat = (int)oc->menu.veh_color_cat + (dir < 0 ? -1 : 1);
        if (cat >= COLOR_CAT_MAX) cat = 0;
        if (cat < 0)              cat = COLOR_CAT_MAX - 1;
        oc->menu.veh_color_cat = (uint32_t)cat;
        set_toast(oc, kColorCatNames[cat]);
        break;
    }
    case ITEM_ACT_COLOR_PRESET:
        apply_color_preset(oc, r->arg);
        break;
    case ITEM_CYC_WINDOW_TINT: {
        int ti = (int)oc->menu.veh_tint_idx + (dir < 0 ? -1 : 1);
        if (ti >= 6) ti = 0;
        if (ti < 0)  ti = 5;
        oc->menu.veh_tint_idx = (uint32_t)ti;
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_WINDOW_TINT]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_WINDOW_TINT, (uint64_t)veh, (uint64_t)ti);
            set_toast(oc, kWindowTintNames[ti]);
        }
        break;
    }

    /* Vehicle Tuning & LSC Body Slots */
    case ITEM_ACT_MAX_PERF:
        act_max_vehicle_mods(oc);
        break;
    case ITEM_CYC_MOD_ENGINE: {
        int el = (int)oc->menu.veh_engine_lvl + (dir < 0 ? -1 : 1);
        if (el >= 5) el = 0;
        if (el < 0)  el = 4;
        oc->menu.veh_engine_lvl = (uint32_t)el;
        apply_vehicle_mod_item(oc, 11, el ? el - 1 : -1);
        set_toast(oc, kEngineNames[el]);
        break;
    }
    case ITEM_CYC_MOD_BRAKES: {
        int bl = (int)oc->menu.veh_brake_lvl + (dir < 0 ? -1 : 1);
        if (bl >= 4) bl = 0;
        if (bl < 0)  bl = 3;
        oc->menu.veh_brake_lvl = (uint32_t)bl;
        apply_vehicle_mod_item(oc, 12, bl ? bl - 1 : -1);
        set_toast(oc, kBrakesNames[bl]);
        break;
    }
    case ITEM_CYC_MOD_TRANS: {
        int tl = (int)oc->menu.veh_trans_lvl + (dir < 0 ? -1 : 1);
        if (tl >= 4) tl = 0;
        if (tl < 0)  tl = 3;
        oc->menu.veh_trans_lvl = (uint32_t)tl;
        apply_vehicle_mod_item(oc, 13, tl ? tl - 1 : -1);
        set_toast(oc, kTransNames[tl]);
        break;
    }
    case ITEM_CYC_MOD_SUSP: {
        int sl = (int)oc->menu.veh_susp_lvl + (dir < 0 ? -1 : 1);
        if (sl >= 5) sl = 0;
        if (sl < 0)  sl = 4;
        oc->menu.veh_susp_lvl = (uint32_t)sl;
        apply_vehicle_mod_item(oc, 15, sl ? sl - 1 : -1);
        set_toast(oc, kSuspNames[sl]);
        break;
    }
    case ITEM_CYC_MOD_ARMOR: {
        int al = (int)oc->menu.veh_armor_lvl + (dir < 0 ? -1 : 1);
        if (al >= 6) al = 0;
        if (al < 0)  al = 5;
        oc->menu.veh_armor_lvl = (uint32_t)al;
        apply_vehicle_mod_item(oc, 16, al ? al - 1 : -1);
        set_toast(oc, kArmorNames[al]);
        break;
    }
    case ITEM_TOG_TURBO: {
        oc->menu.veh_turbo_on ^= 1u;
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_TOGGLE_VEHICLE_MOD]) {
            nat3(oc, ORBX_NAT_TOGGLE_VEHICLE_MOD, (uint64_t)veh, 18, oc->menu.veh_turbo_on ? 1u : 0u);
        }
        set_toast(oc, oc->menu.veh_turbo_on ? "Turbo Tuning ON" : "Turbo Tuning OFF");
        break;
    }
    case ITEM_TOG_XENON: {
        oc->menu.veh_xenon_on ^= 1u;
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_TOGGLE_VEHICLE_MOD]) {
            nat3(oc, ORBX_NAT_TOGGLE_VEHICLE_MOD, (uint64_t)veh, 22, oc->menu.veh_xenon_on ? 1u : 0u);
        }
        set_toast(oc, oc->menu.veh_xenon_on ? "Xenon Lights ON" : "Xenon Lights OFF");
        break;
    }
    case ITEM_CYC_LSC_PART: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (!veh) { set_toast(oc, "Not in a vehicle"); break; }
        uint8_t s_idx = r->arg;
        if (s_idx >= (sizeof(kLscBodySlots) / sizeof(kLscBodySlots[0]))) break;
        int slot_id = (int)kLscBodySlots[s_idx].slot;
        if (oc->menu.natives[ORBX_NAT_SET_VEHICLE_MOD_KIT]) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_MOD_KIT, (uint64_t)veh, 0u);
        }
        if (oc->menu.natives[ORBX_NAT_GET_NUM_VEHICLE_MODS] &&
            oc->menu.natives[ORBX_NAT_GET_VEHICLE_MOD]) {
            int32_t total = (int32_t)nat2(oc, ORBX_NAT_GET_NUM_VEHICLE_MODS, (uint64_t)veh, (uint64_t)slot_id);
            if (total <= 0) {
                set_toast(oc, "No custom parts available");
                break;
            }
            int32_t cur_m = (int32_t)nat2(oc, ORBX_NAT_GET_VEHICLE_MOD, (uint64_t)veh, (uint64_t)slot_id);
            int32_t nxt = cur_m + (dir < 0 ? -1 : 1);
            if (nxt >= total) nxt = -1; /* Wrap to Stock */
            if (nxt < -1)     nxt = total - 1; /* Wrap to top */
            apply_vehicle_mod_item(oc, slot_id, nxt);
            char tmsg[48];
            size_t p = orbx_pic_fmt_str(tmsg, 0, sizeof(tmsg) - 1, kLscBodySlots[s_idx].name);
            p = orbx_pic_fmt_str(tmsg, p, sizeof(tmsg) - 1, ": ");
            if (nxt < 0) {
                p = orbx_pic_fmt_str(tmsg, p, sizeof(tmsg) - 1, "Stock");
            } else {
                p = orbx_pic_fmt_str(tmsg, p, sizeof(tmsg) - 1, "Part ");
                p = orbx_pic_fmt_u64(tmsg, p, sizeof(tmsg) - 1, (uint64_t)(nxt + 1));
                p = orbx_pic_fmt_str(tmsg, p, sizeof(tmsg) - 1, "/");
                p = orbx_pic_fmt_u64(tmsg, p, sizeof(tmsg) - 1, (uint64_t)total);
            }
            tmsg[p] = '\0';
            set_toast(oc, tmsg);
        }
        break;
    }
    case ITEM_CYC_LSC_SLOT:
    case ITEM_CYC_LSC_VAL:
        break;

    /* Vehicle Neons & Lights */
    case ITEM_TOG_NEON_ALL:
        oc->menu.veh_neon_on ^= 1u;
        apply_neon_lights(oc);
        set_toast(oc, oc->menu.veh_neon_on ? "Neons ON" : "Neons OFF");
        break;
    case ITEM_TOG_NEON_FRONT:
        oc->menu.veh_neon_front ^= 1u;
        apply_neon_lights(oc);
        set_toast(oc, oc->menu.veh_neon_front ? "Front Neon ON" : "Front Neon OFF");
        break;
    case ITEM_TOG_NEON_BACK:
        oc->menu.veh_neon_back ^= 1u;
        apply_neon_lights(oc);
        set_toast(oc, oc->menu.veh_neon_back ? "Back Neon ON" : "Back Neon OFF");
        break;
    case ITEM_TOG_NEON_LEFT:
        oc->menu.veh_neon_left ^= 1u;
        apply_neon_lights(oc);
        set_toast(oc, oc->menu.veh_neon_left ? "Left Neon ON" : "Left Neon OFF");
        break;
    case ITEM_TOG_NEON_RIGHT:
        oc->menu.veh_neon_right ^= 1u;
        apply_neon_lights(oc);
        set_toast(oc, oc->menu.veh_neon_right ? "Right Neon ON" : "Right Neon OFF");
        break;
    case ITEM_CYC_NEON_COLOR: {
        int ni = (int)oc->menu.veh_neon_color_idx + (dir < 0 ? -1 : 1);
        if (ni >= 9) ni = 0;
        if (ni < 0)  ni = 8;
        oc->menu.veh_neon_color_idx = (uint32_t)ni;
        apply_neon_lights(oc);
        set_toast(oc, kNeonColors[ni].name);
        break;
    }
    case ITEM_CYC_SMOKE_COLOR: {
        int si = (int)oc->menu.veh_smoke_color_idx + (dir < 0 ? -1 : 1);
        if (si >= 9) si = 0;
        if (si < 0)  si = 8;
        oc->menu.veh_smoke_color_idx = (uint32_t)si;
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        int32_t veh = get_current_veh(oc, ped);
        if (veh && oc->menu.natives[ORBX_NAT_SET_VEHICLE_TYRE_SMOKE_COLOR]) {
            nat3(oc, ORBX_NAT_TOGGLE_VEHICLE_MOD, (uint64_t)veh, 20, 1);
            nat4(oc, ORBX_NAT_SET_VEHICLE_TYRE_SMOKE_COLOR, (uint64_t)veh,
                 (uint64_t)kSmokeColors[si].r, (uint64_t)kSmokeColors[si].g, (uint64_t)kSmokeColors[si].b);
            set_toast(oc, kSmokeColors[si].name);
        }
        break;
    }

    /* Vehicle Spawner */
    case ITEM_SPAWN_KEYBOARD:
        act_open_keyboard(oc, 0u);
        break;
    case ITEM_ADDON_IDX:
        if (r->arg < oc->menu.addon_car_count && r->arg < ORBX_MAX_ADDON_CARS) {
            uint32_t h = oc->menu.addon_cars[r->arg].hash;
            if (!h && oc->menu.addon_cars[r->arg].name[0]) {
                h = orbx_pic_joaat(oc->menu.addon_cars[r->arg].name);
            }
            start_vehicle_spawn(oc, h);
        }
        break;
    case ITEM_SPAWN_CAT_ITEM: {
        uint32_t ci = (oc->menu.spawn_cat_idx < 6u) ? oc->menu.spawn_cat_idx : 0u;
        if (r->arg < kSpawnCategories[ci].count && r->arg < 10u) {
            uint32_t h = kSpawnCategories[ci].items[r->arg].hash;
            if (!h) h = orbx_pic_joaat(kSpawnCategories[ci].items[r->arg].name);
            start_vehicle_spawn(oc, h);
        }
        break;
    }

    /* Teleport & IPLs */
    case ITEM_TP_WP:
        act_tp_waypoint(oc);
        break;
    case ITEM_TP_IDX:
        if (r->arg < (uint8_t)(sizeof(kTeleports) / sizeof(kTeleports[0]))) {
            const TpEntry *tp = &kTeleports[r->arg];
            teleport_player_entity(oc, tp->x, tp->y, tp->z);
            set_toast(oc, tp->name);
        }
        break;
    case ITEM_TP_IPL:
        act_tp_ipl(oc, r->arg);
        break;

    /* World, Sky & Clouds */
    case ITEM_CYC_DENSITY: {
        int pct = (int)oc->menu.density_pct + (dir < 0 ? -25 : 25);
        if (pct > 100) pct = 0;
        if (pct < 0)   pct = 100;
        oc->menu.density_pct = (uint32_t)pct;
        break;
    }
    case ITEM_CYC_WEATHER: {
        int wi = (int)oc->menu.weather_idx + (dir < 0 ? -1 : 1);
        if (wi >= 15) wi = 0;
        if (wi < 0)   wi = 14;
        act_apply_weather_idx(oc, wi);
        break;
    }
    case ITEM_TOG_WEATHER_PERSIST:
        oc->menu.weather_persist = !oc->menu.weather_persist ? 1u : 0u;
        if (!oc->menu.weather_persist) {
            if (oc->menu.natives[ORBX_NAT_CLEAR_OVERRIDE_WEATHER]) {
                nat0(oc, ORBX_NAT_CLEAR_OVERRIDE_WEATHER);
            }
            if (oc->menu.natives[ORBX_NAT_CLEAR_WEATHER_TYPE_PERSIST]) {
                nat0(oc, ORBX_NAT_CLEAR_WEATHER_TYPE_PERSIST);
            }
            set_toast(oc, "Weather Lock OFF");
        } else {
            act_apply_weather_idx(oc, oc->menu.weather_idx);
            set_toast(oc, "Weather Locked");
        }
        break;
    case ITEM_CYC_TIME: {
        int h = 12;
        if (oc->menu.lock_hour >= 0 && oc->menu.lock_hour <= 23) {
            h = oc->menu.lock_hour;
        } else if (oc->menu.natives[ORBX_NAT_GET_CLOCK_HOURS]) {
            h = (int)nat0(oc, ORBX_NAT_GET_CLOCK_HOURS);
            if (h < 0 || h > 23) h = 12;
        }
        /* Step forward or backward by exactly 1 hour */
        h = (h + (dir < 0 ? 23 : 1)) % 24;
        oc->menu.lock_hour = h;
        oc->menu.last_lock_hour_tick = oc->menu.main_ticks;
        int m = 0;
        if (oc->menu.natives[ORBX_NAT_GET_CLOCK_MINUTES]) {
            m = (int)nat0(oc, ORBX_NAT_GET_CLOCK_MINUTES);
            if (m < 0 || m > 59) m = 0;
        }
        nat3(oc, ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME, (uint64_t)h, (uint64_t)m, 0);

        char tbuf[32];
        size_t tp = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, (dir < 0) ? "Time -1h: " : "Time +1h: ");
        if (h < 10) tp = orbx_pic_fmt_str(tbuf, tp, sizeof(tbuf) - 1, "0");
        tp = orbx_pic_fmt_u64(tbuf, tp, sizeof(tbuf) - 4, (uint64_t)h);
        tp = orbx_pic_fmt_str(tbuf, tp, sizeof(tbuf) - 1, ":00");
        tbuf[tp] = '\0';
        set_toast(oc, tbuf);
        break;
    }
    case ITEM_ACT_RESET_TIME:
        oc->menu.lock_hour = -1;
        oc->menu.clock_paused = 0u;
        oc->menu.applied_flags &= ~APP_CLOCK_PAUSE;
        if (oc->menu.natives[ORBX_NAT_PAUSE_CLOCK]) nat1(oc, ORBX_NAT_PAUSE_CLOCK, 0u);
        if (oc->menu.natives[ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE]) {
            nat0(oc, ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE);
        }
        set_toast(oc, "Clock: Dynamic Daylight Restored");
        break;
    case ITEM_TOG_CLOCK_PAUSE:
        oc->menu.clock_paused = !oc->menu.clock_paused ? 1u : 0u;
        if (oc->menu.clock_paused) {
            int h = 12, m = 0;
            if (oc->menu.natives[ORBX_NAT_GET_CLOCK_HOURS]) {
                h = (int)nat0(oc, ORBX_NAT_GET_CLOCK_HOURS);
                if (h < 0 || h > 23) h = 12;
            }
            if (oc->menu.natives[ORBX_NAT_GET_CLOCK_MINUTES]) {
                m = (int)nat0(oc, ORBX_NAT_GET_CLOCK_MINUTES);
                if (m < 0 || m > 59) m = 0;
            }
            oc->menu.lock_hour = h;
            oc->menu.last_lock_hour_tick = oc->menu.main_ticks;
            if (oc->menu.natives[ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME]) {
                nat3(oc, ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME, (uint64_t)h, (uint64_t)m, 0);
            }
            if (oc->menu.natives[ORBX_NAT_PAUSE_CLOCK]) {
                nat1(oc, ORBX_NAT_PAUSE_CLOCK, 1u);
            }
            oc->menu.applied_flags |= APP_CLOCK_PAUSE;
            set_toast(oc, "Clock Frozen");
        } else {
            if (oc->menu.natives[ORBX_NAT_PAUSE_CLOCK]) {
                nat1(oc, ORBX_NAT_PAUSE_CLOCK, 0u);
            }
            if (oc->menu.natives[ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE]) {
                nat0(oc, ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE);
            }
            oc->menu.lock_hour = -1;
            oc->menu.applied_flags &= ~APP_CLOCK_PAUSE;
            set_toast(oc, "Clock Resumed");
        }
        break;
    case ITEM_CYC_TIME_SCALE: {
        int ts = (int)oc->menu.time_scale_idx + (dir < 0 ? -1 : 1);
        if (ts >= 4) ts = 0;
        if (ts < 0)  ts = 3;
        oc->menu.time_scale_idx = (uint32_t)ts;
        if (oc->menu.natives[ORBX_NAT_SET_TIME_SCALE]) {
            nat1(oc, ORBX_NAT_SET_TIME_SCALE, f2u(kTimeScales[ts]));
        }
        set_toast(oc, kTimeScaleNames[ts]);
        break;
    }
    case ITEM_TOG_LOW_GRAV:
        oc->menu.low_gravity = !oc->menu.low_gravity ? 1u : 0u;
        set_toast(oc, oc->menu.low_gravity ? "Moon Gravity ON" : "Normal Gravity");
        break;
    case ITEM_TOG_MOBILE_RADIO:
        oc->menu.mobile_radio = !oc->menu.mobile_radio ? 1u : 0u;
        set_toast(oc, oc->menu.mobile_radio ? "Mobile Radio ON" : "Mobile Radio OFF");
        break;
    case ITEM_TOG_CALMER_POLICE:
        oc->menu.calmer_police_on = !oc->menu.calmer_police_on ? 1u : 0u;
        set_toast(oc, oc->menu.calmer_police_on ? "Calmer Police AI ON" : "Calmer Police AI OFF");
        break;
    case ITEM_ACT_CLEAR_PEDS: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        Vec3 pos;
        zero_bytes(&pos, sizeof(pos));
        uint64_t ga[2] = { (uint64_t)ped, 1u };
        nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ga, 2, &pos);
        if (oc->menu.natives[ORBX_NAT_CLEAR_AREA_OF_PEDS]) {
            uint64_t pa[5] = { f2u(pos.x), f2u(pos.y), f2u(pos.z), f2u(150.0f), 1u };
            nat_invoke(oc, ORBX_NAT_CLEAR_AREA_OF_PEDS, pa, 5, NULL);
            set_toast(oc, "Nearby Peds Cleared");
        }
        break;
    }
    case ITEM_ACT_CLEAR_VEHS: {
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        Vec3 pos;
        zero_bytes(&pos, sizeof(pos));
        uint64_t ga[2] = { (uint64_t)ped, 1u };
        nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ga, 2, &pos);
        if (oc->menu.natives[ORBX_NAT_CLEAR_AREA_OF_VEHICLES]) {
            uint64_t va[8] = { f2u(pos.x), f2u(pos.y), f2u(pos.z), f2u(150.0f), 0u, 0u, 0u, 0u };
            nat_invoke(oc, ORBX_NAT_CLEAR_AREA_OF_VEHICLES, va, 8, NULL);
            set_toast(oc, "Nearby Vehicles Cleared");
        }
        break;
    }
    case ITEM_CYC_CLOUD_HAT_CHOICE: {
        int chi = oc->menu.cloud_hat_idx;
        if (chi < 0) chi = (dir < 0) ? 5 : 0;
        else {
            chi += (dir < 0 ? -1 : 1);
            if (chi >= 6) chi = 0;
            if (chi < 0)  chi = 5;
        }
        act_apply_cloud_hat(oc, chi);
        break;
    }
    case ITEM_CYC_CLOUD_KEYFRAME: {
        int kfi = oc->menu.cloud_keyframe_idx + (dir < 0 ? -1 : 1);
        if (kfi >= 14) kfi = -1;
        if (kfi < -1)  kfi = 13;
        act_apply_cloud_keyframe(oc, kfi);
        break;
    }
    case ITEM_ACT_APPLY_CLOUD_HAT: {
        int chi = oc->menu.cloud_hat_idx;
        if (chi < 0 || chi >= 6) chi = 0;
        act_apply_cloud_hat(oc, chi);
        break;
    }
    case ITEM_ACT_CLEAR_CLOUDS:
        act_clear_cloud_hats(oc);
        break;
    case ITEM_CYC_CLOUD_ALPHA: {
        int a = (int)oc->menu.cloud_alpha_pct + (dir < 0 ? -10 : 10);
        if (a > 100) a = 0;
        if (a < 0)   a = 100;
        oc->menu.cloud_alpha_pct = (uint32_t)a;
        if (oc->menu.natives[ORBX_NAT_SET_CLOUDS_ALPHA]) {
            nat1(oc, ORBX_NAT_SET_CLOUDS_ALPHA, f2u((float)a * 0.01f));
        }
        break;
    }

    /* Camera & First Person POV */
    case ITEM_CYC_POV_FOV: {
        int fov = (int)oc->menu.fp_fov_idx + (dir < 0 ? -1 : 1);
        if (fov >= 11) fov = 0;
        if (fov < 0)   fov = 10;
        oc->menu.fp_fov_idx = (uint32_t)fov;
        if (fov == 0) {
            stop_fp_fov_cam(oc);
            set_toast(oc, "FP FOV: Default");
        } else {
            int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
            int32_t veh = ped ? get_current_veh(oc, ped) : 0;
            uint32_t cur_view = veh ? oc->menu.cam_veh_view_mode : oc->menu.cam_ped_view_mode;
            if (cur_view != 4u) {
                char tbuf[64];
                size_t tp = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, kFovLabels[fov]);
                tp = orbx_pic_fmt_str(tbuf, tp, sizeof(tbuf) - 1, " (Switch to 1st Person)");
                tbuf[tp] = '\0';
                set_toast(oc, tbuf);
            } else {
                set_toast(oc, kFovLabels[fov]);
            }
        }
        break;
    }
    case ITEM_TOG_FP_LOCK:
        oc->menu.fp_lock ^= 1u;
        set_toast(oc, oc->menu.fp_lock ? "First Person Locked" : "First Person Lock OFF");
        break;
    case ITEM_TOG_CLIP_FIX:
        oc->menu.fp_clip_fix ^= 1u;
        set_toast(oc, oc->menu.fp_clip_fix ? "Near-Clip 0.01m ON" : "Near-Clip Fix OFF");
        break;
    case ITEM_CYC_PED_VIEW: {
        uint32_t cur_i = cam_mode_to_opt_idx(oc->menu.cam_ped_view_mode);
        uint32_t nxt_i = (cur_i + (dir < 0 ? 3u : 1u)) % 4u;
        uint32_t mode  = kCamViewOptions[nxt_i].mode_val;
        if (mode != 4u && oc->menu.fp_lock) {
            oc->menu.fp_lock = 0u;
            oc->menu.applied_flags &= ~APP_FP_LOCK;
        }
        oc->menu.cam_ped_view_mode = mode;
        set_cam_view_mode_all(oc, 0, mode);
        set_toast(oc, kCamViewOptions[nxt_i].ped_label);
        break;
    }
    case ITEM_CYC_VEH_VIEW: {
        uint32_t cur_i = cam_mode_to_opt_idx(oc->menu.cam_veh_view_mode);
        uint32_t nxt_i = (cur_i + (dir < 0 ? 3u : 1u)) % 4u;
        uint32_t mode  = kCamViewOptions[nxt_i].mode_val;
        if (mode != 4u && oc->menu.fp_lock) {
            oc->menu.fp_lock = 0u;
            oc->menu.applied_flags &= ~APP_FP_LOCK;
        }
        oc->menu.cam_veh_view_mode = mode;
        set_cam_view_mode_all(oc, 1, mode);
        set_toast(oc, kCamViewOptions[nxt_i].veh_label);
        break;
    }
    case ITEM_TOG_CINEMATIC_CAM:
        oc->menu.cinematic_cam ^= 1u;
        if (oc->menu.natives[ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE]) {
            nat1(oc, ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE, oc->menu.cinematic_cam ? 1u : 0u);
        }
        set_toast(oc, oc->menu.cinematic_cam ? "Cinematic Cam ON" : "Cinematic Cam OFF");
        break;
    case ITEM_TOG_CAM_ANTI_SHAKE:
        oc->menu.cam_anti_shake ^= 1u;
        if (oc->menu.natives[ORBX_NAT_STOP_GAMEPLAY_CAM_SHAKING]) {
            nat1(oc, ORBX_NAT_STOP_GAMEPLAY_CAM_SHAKING, 1u);
        }
        set_toast(oc, oc->menu.cam_anti_shake ? "No Camera Shake ON" : "Camera Shake Normal");
        break;
    case ITEM_ACT_CAM_RESET_BEHIND:
        act_reset_cam_behind(oc);
        break;

    /* Alternate First Person (PC ASI Mod Port) Actions */
    case ITEM_CYC_AFP_MODE: {
        int m = (int)oc->menu.afp_mode + (dir < 0 ? -1 : 1);
        if (m >= 6) m = 0;
        if (m < 0)  m = 5;
        oc->menu.afp_mode = (uint32_t)m;
        if (m == 0) {
            stop_afp_cam(oc);
            set_toast(oc, "Camera: Stock GTA V");
        } else {
            char tbuf[48];
            size_t p = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, "Camera: ");
            p = orbx_pic_fmt_str(tbuf, p, sizeof(tbuf) - 1, kAfpModeNames[m]);
            tbuf[p] = '\0';
            set_toast(oc, tbuf);
        }
        break;
    }
    case ITEM_CYC_AFP_FOV: {
        int f = (int)oc->menu.afp_fov_idx + (dir < 0 ? -1 : 1);
        if (f >= 8) f = 0;
        if (f < 0)  f = 7;
        oc->menu.afp_fov_idx = (uint32_t)f;
        char tbuf[32];
        size_t p = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, "FOV: ");
        p = orbx_pic_fmt_str(tbuf, p, sizeof(tbuf) - 1, kAfpFovNames[f]);
        tbuf[p] = '\0';
        set_toast(oc, tbuf);
        break;
    }
    case ITEM_CYC_AFP_HEADBOB: {
        int b = (int)oc->menu.afp_headbob + (dir < 0 ? -1 : 1);
        if (b >= 4) b = 0;
        if (b < 0)  b = 3;
        oc->menu.afp_headbob = (uint32_t)b;
        char tbuf[32];
        size_t p = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, "Head Bob: ");
        p = orbx_pic_fmt_str(tbuf, p, sizeof(tbuf) - 1, kAfpBobNames[b]);
        tbuf[p] = '\0';
        set_toast(oc, tbuf);
        break;
    }
    case ITEM_CYC_AFP_DRIFT: {
        int d = (int)oc->menu.afp_smooth_follow + (dir < 0 ? -1 : 1);
        if (d >= 4) d = 0;
        if (d < 0)  d = 3;
        oc->menu.afp_smooth_follow = (uint32_t)d;
        char tbuf[32];
        size_t p = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, "Bodycam Drift: ");
        p = orbx_pic_fmt_str(tbuf, p, sizeof(tbuf) - 1, kAfpDriftNames[d]);
        tbuf[p] = '\0';
        set_toast(oc, tbuf);
        break;
    }
    case ITEM_CYC_AFP_RECOIL: {
        int r = (int)oc->menu.afp_recoil + (dir < 0 ? -1 : 1);
        if (r >= 4) r = 0;
        if (r < 0)  r = 3;
        oc->menu.afp_recoil = (uint32_t)r;
        char tbuf[32];
        size_t p = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, "Recoil Shake: ");
        p = orbx_pic_fmt_str(tbuf, p, sizeof(tbuf) - 1, kAfpRecoilNames[r]);
        tbuf[p] = '\0';
        set_toast(oc, tbuf);
        break;
    }
    case ITEM_TOG_AFP_INERTIA:
        oc->menu.afp_inertia ^= 1u;
        set_toast(oc, oc->menu.afp_inertia ? "Vehicle Inertia ON" : "Vehicle Inertia OFF");
        break;
    case ITEM_TOG_AFP_CROUCH:
        oc->menu.afp_crouch_lower ^= 1u;
        set_toast(oc, oc->menu.afp_crouch_lower ? "Crouch Lowering ON" : "Crouch Lowering OFF");
        break;
    case ITEM_CYC_AFP_ADS_ZOOM: {
        int z = (int)oc->menu.afp_ads_zoom + (dir < 0 ? -1 : 1);
        if (z >= 4) z = 0;
        if (z < 0)  z = 3;
        oc->menu.afp_ads_zoom = (uint32_t)z;
        char tbuf[32];
        size_t p = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, "ADS Zoom: ");
        p = orbx_pic_fmt_str(tbuf, p, sizeof(tbuf) - 1, kAfpAdsNames[z]);
        tbuf[p] = '\0';
        set_toast(oc, tbuf);
        break;
    }
    case ITEM_CYC_AFP_CROSSHAIR: {
        int c = (int)oc->menu.afp_crosshair + (dir < 0 ? -1 : 1);
        if (c >= 3) c = 0;
        if (c < 0)  c = 2;
        oc->menu.afp_crosshair = (uint32_t)c;
        char tbuf[32];
        size_t p = orbx_pic_fmt_str(tbuf, 0, sizeof(tbuf) - 1, "Crosshair: ");
        p = orbx_pic_fmt_str(tbuf, p, sizeof(tbuf) - 1, kAfpCrosshairNames[c]);
        tbuf[p] = '\0';
        set_toast(oc, tbuf);
        break;
    }
    case ITEM_TOG_AFP_AUTOTURN:
        oc->menu.afp_autoturn ^= 1u;
        set_toast(oc, oc->menu.afp_autoturn ? "Auto-Turn Body ON" : "Auto-Turn Body OFF");
        break;
    case ITEM_TOG_AFP_CLIP_FIX:
        oc->menu.afp_clip_fix ^= 1u;
        set_toast(oc, oc->menu.afp_clip_fix ? "Near-Clip Fix ON" : "Near-Clip Fix OFF");
        break;
    case ITEM_ACT_AFP_RESET:
        oc->menu.afp_fov_idx       = 3u; /* 90.0 deg */
        oc->menu.afp_headbob       = 2u; /* Normal */
        oc->menu.afp_smooth_follow = 2u; /* Bodycam */
        oc->menu.afp_recoil        = 2u; /* Standard */
        oc->menu.afp_inertia       = 1u;
        oc->menu.afp_crouch_lower  = 1u;
        oc->menu.afp_ads_zoom      = 2u; /* 25 deg */
        oc->menu.afp_crosshair     = 0u;
        oc->menu.afp_autoturn      = 1u;
        oc->menu.afp_clip_fix      = 1u;
        set_toast(oc, "AFP Camera Reset to Default");
        break;

    /* Settings, Keybinds & Themes */
    case ITEM_CYC_MENU_POS: {
        int mp = (int)oc->menu.menu_position + (dir < 0 ? -1 : 1);
        if (mp >= 3) mp = 0;
        if (mp < 0)  mp = 2;
        oc->menu.menu_position = (uint32_t)mp;
        set_toast(oc, kMenuPosNames[mp]);
        break;
    }
    case ITEM_CYC_MENU_SCALE: {
        int ms = (int)oc->menu.menu_scale + (dir < 0 ? -1 : 1);
        if (ms >= 3) ms = 0;
        if (ms < 0)  ms = 2;
        oc->menu.menu_scale = (uint32_t)ms;
        set_toast(oc, kMenuScaleNames[ms]);
        break;
    }
    case ITEM_CYC_THEME_ACCENT: {
        int ta = (int)oc->menu.theme_accent + (dir < 0 ? -1 : 1);
        if (ta >= 6) ta = 0;
        if (ta < 0)  ta = 5;
        oc->menu.theme_accent = (uint32_t)ta;
        set_toast(oc, kThemePresets[ta].name);
        break;
    }
    case ITEM_TOG_SOUND_FX:
        oc->menu.sound_enabled ^= 1u;
        set_toast(oc, oc->menu.sound_enabled ? "Sound FX ON" : "Sound FX OFF");
        break;
    case ITEM_TOG_KEEP_STEER:
        oc->menu.keep_steering ^= 1u;
        recompute_suppress_mask(oc);
        set_toast(oc, oc->menu.keep_steering ? "Driving Controls Active" : "Menu Captures Steering");
        break;
    case ITEM_TOG_NAV_STICK:
        oc->menu.nav_stick ^= 1u;
        set_toast(oc, oc->menu.nav_stick ? "L-Stick Navigation ON" : "D-Pad Only Navigation");
        break;
    case ITEM_ACT_KEYBIND_SLOT:
        oc->menu.keybind_capture_slot = r->arg;
        oc->menu.keybind_capture_step = 1u;
        set_toast(oc, (r->arg == KB_SLOT_OPEN)
                  ? "Release buttons, then tap/hold new open combo"
                  : "Release buttons, then press new button");
        break;
    case ITEM_ACT_KEYBIND_CLEAR:
        oc->menu.bind_horn_boost   = 0u;
        oc->menu.bind_noclip       = 0u;
        oc->menu.bind_quick_repair = 0u;
        oc->menu.bind_quick_heal   = 0u;
        oc->menu.bind_super_brake  = 0u;
        recompute_suppress_mask(oc);
        oc->menu.cfg_save_req = 1u;
        set_toast(oc, "Optional Quick Hotkeys Unbound");
        break;
    case ITEM_ACT_KEYBIND_RESET:
        reset_default_keybinds(oc);
        oc->menu.cfg_save_req = 1u;
        set_toast(oc, "Default Keybinds Restored");
        break;
    case ITEM_ACT_SAVE_INI:
        oc->menu.cfg_save_req = 1u;
        set_toast(oc, "Settings & Keybinds Saved");
        break;

    /* ENB / ReShade Live Graphics Enhancer */
    case ITEM_CYC_GFX_PRESET: {
        int gi = (int)oc->menu.gfx_preset_idx + (dir < 0 ? -1 : 1);
        if (gi >= 15) gi = 0;
        if (gi < 0)   gi = 14;
        apply_gfx_preset(oc, (uint32_t)gi);
        break;
    }
    case ITEM_CYC_GFX_STRENGTH: {
        int sp = (int)oc->menu.gfx_strength_pct + (dir < 0 ? -10 : 10);
        if (sp > 200) sp = 10;
        if (sp < 10)  sp = 200;
        apply_gfx_strength(oc, (uint32_t)sp);
        break;
    }
    case ITEM_CYC_GFX_GRAIN: {
        int fg = (int)oc->menu.gfx_film_grain + (dir < 0 ? -10 : 10);
        if (fg > 100) fg = 0;
        if (fg < 0)   fg = 100;
        apply_gfx_grain(oc, (uint32_t)fg);
        break;
    }
    case ITEM_CYC_GFX_POSTFX: {
        int pfx = (int)oc->menu.gfx_postfx_idx + (dir < 0 ? -1 : 1);
        if (pfx >= 7) pfx = 0;
        if (pfx < 0)  pfx = 6;
        apply_gfx_postfx(oc, (uint32_t)pfx);
        break;
    }
    case ITEM_ACT_GFX_RESET:
        reset_all_gfx(oc);
        break;
    case ITEM_ACT_GFX_QUICK:
        if (oc->menu.gfx_preset_idx == (uint32_t)r->arg) {
            apply_gfx_preset(oc, 0u);
        } else {
            apply_gfx_preset(oc, (uint32_t)r->arg);
        }
        break;

    /* Stage 1 & 2 PC ENB Controls in Mod Menu */
    case ITEM_CYC_ENB_EXTRA: {
        int aa = (int)oc->menu.enb_extra_active + (dir < 0 ? -1 : 1);
        if (aa > 5) aa = 0;
        if (aa < 0) aa = 5;
        oc->menu.enb_extra_active = (uint32_t)aa;
        sync_enb_graphics_pipeline(oc);
        if (aa == 0)      set_toast(oc, "Stage 1 Anti-Aliasing: OFF");
        else if (aa == 1) set_toast(oc, "Stage 1 Anti-Aliasing: LV1 Soft");
        else if (aa == 2) set_toast(oc, "Stage 1 Anti-Aliasing: LV2 SMAA");
        else if (aa == 3) set_toast(oc, "Stage 1 Anti-Aliasing: LV3 Smooth");
        else if (aa == 4) set_toast(oc, "Stage 1 Anti-Aliasing: LV4 Ultra");
        else              set_toast(oc, "Stage 1 Anti-Aliasing: LV5 Max Clean");
        break;
    }
    case ITEM_CYC_ENB_SHADOW_SCALE: {
        uint32_t cur_val = oc->menu.enb_shadow_scale;
        uint32_t next_val = 15u;
        if (dir > 0) {
            if (cur_val < 15u)      next_val = 15u;
            else if (cur_val < 20u) next_val = 20u;
            else if (cur_val < 25u) next_val = 25u;
            else if (cur_val < 35u) next_val = 35u;
            else if (cur_val < 50u) next_val = 50u;
            else                    next_val = 0u;
        } else {
            if (cur_val > 35u)      next_val = 35u;
            else if (cur_val > 25u) next_val = 25u;
            else if (cur_val > 20u) next_val = 20u;
            else if (cur_val > 15u) next_val = 15u;
            else if (cur_val >= 15u) next_val = 0u;
            else                    next_val = 50u;
        }
        oc->menu.enb_shadow_scale = next_val;
        if (next_val == 0u) {
            if (oc->menu.natives[ORBX_NAT_CLEAR_HD_AREA]) {
                nat0(oc, ORBX_NAT_CLEAR_HD_AREA);
            }
            if (oc->menu.natives[ORBX_NAT_OVERRIDE_LODSCALE_THIS_FRAME]) {
                nat1(oc, ORBX_NAT_OVERRIDE_LODSCALE_THIS_FRAME, f2u(1.0f));
            }
            sync_enb_graphics_pipeline(oc);
            set_toast(oc, "Stage 1 LOD Distance: 1.0x Vanilla");
        } else {
            int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
            if (ped > 0 && oc->menu.natives[ORBX_NAT_SET_HD_AREA]) {
                Vec3 ppos;
                zero_bytes(&ppos, sizeof(ppos));
                uint64_t ga[2] = { (uint64_t)ped, 1u };
                nat_invoke(oc, ORBX_NAT_GET_ENTITY_COORDS, ga, 2, &ppos);
                float hd_rad = (float)next_val * 12.0f;
                if (hd_rad > 600.0f) hd_rad = 600.0f;
                uint64_t hda[4] = { f2u(ppos.x), f2u(ppos.y), f2u(ppos.z), f2u(hd_rad) };
                nat_invoke(oc, ORBX_NAT_SET_HD_AREA, hda, 4, NULL);
            }
            sync_enb_graphics_pipeline(oc);
            char smsg[40];
            size_t sp = orbx_pic_fmt_str(smsg, 0, sizeof(smsg) - 1, "Stage 1 LOD Distance: ");
            sp = orbx_pic_fmt_u64(smsg, sp, sizeof(smsg) - 3, (uint64_t)(next_val / 10u));
            sp = orbx_pic_fmt_str(smsg, sp, sizeof(smsg) - 1, ".");
            sp = orbx_pic_fmt_u64(smsg, sp, sizeof(smsg) - 2, (uint64_t)(next_val % 10u));
            sp = orbx_pic_fmt_str(smsg, sp, sizeof(smsg) - 1, "x HD");
            smsg[sp] = '\0';
            set_toast(oc, smsg);
        }
        break;
    }
    case ITEM_CYC_ENB_SHADOW: {
        int sh = (int)oc->menu.enb_shadow_soft + (dir < 0 ? -1 : 1);
        if (sh > 5) sh = 0;
        if (sh < 0) sh = 5;
        oc->menu.enb_shadow_soft = (uint32_t)sh;
        sync_enb_graphics_pipeline(oc);
        if (sh == 0)      set_toast(oc, "Dynamic Shadows & AO: OFF (Vanilla)");
        else if (sh == 1) set_toast(oc, "Dynamic Shadows & AO: LV1 (+25%)");
        else if (sh == 2) set_toast(oc, "Dynamic Shadows & AO: LV2 (+50%)");
        else if (sh == 3) set_toast(oc, "Dynamic Shadows & AO: LV3 (+100% ENB)");
        else if (sh == 4) set_toast(oc, "Dynamic Shadows & AO: LV4 (+150% Deep)");
        else              set_toast(oc, "Dynamic Shadows & AO: LV5 (+200% Max)");
        break;
    }
    case ITEM_CYC_ENB_SSAO: {
        int dk = (int)oc->menu.enb_ssao_boost + (dir < 0 ? -1 : 1);
        if (dk > 5) dk = 0;
        if (dk < 0) dk = 5;
        oc->menu.enb_ssao_boost = (uint32_t)dk;
        sync_enb_graphics_pipeline(oc);
        if (dk == 0)      set_toast(oc, "ENB Scene Darkness: OFF");
        else if (dk == 1) set_toast(oc, "ENB Scene Darkness: LV1 Subtle");
        else if (dk == 2) set_toast(oc, "ENB Scene Darkness: LV2 Moody");
        else if (dk == 3) set_toast(oc, "ENB Scene Darkness: LV3 Dark ENB");
        else if (dk == 4) set_toast(oc, "ENB Scene Darkness: LV4 Very Dark");
        else              set_toast(oc, "ENB Scene Darkness: LV5 Pitch Noir");
        break;
    }
    case ITEM_CYC_ENB_BLOOM: {
        int bl = (int)oc->menu.enb_bloom_lvl + (dir < 0 ? -1 : 1);
        if (bl > 5) bl = 0;
        if (bl < 0) bl = 5;
        oc->menu.enb_bloom_lvl = (uint32_t)bl;
        sync_enb_graphics_pipeline(oc);
        if (bl == 0)      set_toast(oc, "ENB Bloom & Glow: OFF");
        else if (bl == 1) set_toast(oc, "ENB Bloom & Glow: LV1 Warm");
        else if (bl == 2) set_toast(oc, "ENB Bloom & Glow: LV2 Radiant");
        else if (bl == 3) set_toast(oc, "ENB Bloom & Glow: LV3 High HDR");
        else if (bl == 4) set_toast(oc, "ENB Bloom & Glow: LV4 Ultra Neon");
        else              set_toast(oc, "ENB Bloom & Glow: LV5 Extreme");
        break;
    }
    case ITEM_CYC_ENB_REFL: {
        int rf = (int)oc->menu.enb_refl_lvl + (dir < 0 ? -1 : 1);
        if (rf > 5) rf = 0;
        if (rf < 0) rf = 5;
        oc->menu.enb_refl_lvl = (uint32_t)rf;
        sync_enb_graphics_pipeline(oc);
        if (rf == 0)      set_toast(oc, "ENB Reflections: OFF");
        else if (rf == 1) set_toast(oc, "ENB Reflections: LV1 Glossy");
        else if (rf == 2) set_toast(oc, "ENB Reflections: LV2 Chrome");
        else if (rf == 3) set_toast(oc, "ENB Reflections: LV3 Mirror");
        else if (rf == 4) set_toast(oc, "ENB Reflections: LV4 Raytraced");
        else              set_toast(oc, "ENB Reflections: LV5 Max Mirror");
        break;
    }
    case ITEM_CYC_ENB_DOF: {
        int df = (int)oc->menu.enb_dof_lvl + (dir < 0 ? -1 : 1);
        if (df > 5) df = 0;
        if (df < 0) df = 5;
        oc->menu.enb_dof_lvl = (uint32_t)df;
        sync_enb_graphics_pipeline(oc);
        if (df == 0)      set_toast(oc, "NVE Depth of Field: OFF");
        else if (df == 1) set_toast(oc, "NVE Depth of Field: LV1 Subtle");
        else if (df == 2) set_toast(oc, "NVE Depth of Field: LV2 Cinematic");
        else if (df == 3) set_toast(oc, "NVE Depth of Field: LV3 Portrait");
        else if (df == 4) set_toast(oc, "NVE Depth of Field: LV4 Macro");
        else              set_toast(oc, "NVE Depth of Field: LV5 Ultra Bokeh");
        break;
    }
    case ITEM_CYC_ENB_MOTIONBLUR: {
        int mb = (int)oc->menu.enb_motion_blur + (dir < 0 ? -1 : 1);
        if (mb > 5) mb = 0;
        if (mb < 0) mb = 5;
        oc->menu.enb_motion_blur = (uint32_t)mb;
        sync_enb_graphics_pipeline(oc);
        if (mb == 0)      set_toast(oc, "NVE Motion Blur: OFF (NVE Base)");
        else if (mb == 1) set_toast(oc, "NVE Motion Blur: LV1 Subtle");
        else if (mb == 2) set_toast(oc, "NVE Motion Blur: LV2 Smooth");
        else if (mb == 3) set_toast(oc, "NVE Motion Blur: LV3 NVE Classic");
        else if (mb == 4) set_toast(oc, "NVE Motion Blur: LV4 High Speed");
        else              set_toast(oc, "NVE Motion Blur: LV5 Cinema");
        break;
    }

    default:
        break;
    }
}

/* ============================================================================
 * HUD Drawing - Tabbed Cyber Glass Theme, Adaptive Scale & Digital Speedo
 * ============================================================================ */

static void draw_menu_hud(OrbxNativeContext *oc, const char *subtitle,
                          const MenuRow *rows, int row_count, int cur) {
    static const char kTabShort[8][6] = { "HOME", "SELF", "WEAP", "VEH", "SPWN", "WRLD", "CAM", "SET" };

    float menu_x = 0.185f;
    if (oc->menu.menu_position == 1u)      menu_x = 0.815f;
    else if (oc->menu.menu_position == 2u) menu_x = 0.500f;

    float width = 0.286f, tab_h = 0.025f, hdr_h = 0.062f, sub_h = 0.027f, row_h = 0.032f;
    float txt_s = 0.31f,  bdg_s = 0.28f,  title_s = 0.64f;
    if (oc->menu.menu_scale == 0u) {
        width = 0.252f; tab_h = 0.022f; hdr_h = 0.054f; sub_h = 0.024f; row_h = 0.028f;
        txt_s = 0.27f;  bdg_s = 0.24f;  title_s = 0.54f;
    } else if (oc->menu.menu_scale == 2u) {
        width = 0.320f; tab_h = 0.028f; hdr_h = 0.068f; sub_h = 0.030f; row_h = 0.036f;
        txt_s = 0.35f;  bdg_s = 0.31f;  title_s = 0.70f;
    }

    const float left_x  = menu_x - width * 0.5f + 0.010f;
    const float right_x = menu_x + width * 0.5f - 0.010f;

    int tr = 0, tg = 215, tb = 255;
    if (oc->menu.theme_accent < 6u) {
        tr = kThemePresets[oc->menu.theme_accent].r;
        tg = kThemePresets[oc->menu.theme_accent].g;
        tb = kThemePresets[oc->menu.theme_accent].b;
    }

    /* 0. Top 8-Tab Category Strip (L1 / R1) */
    const float tab_y     = 0.072f;
    const float tab_w     = width / 8.0f;
    const float base_left = menu_x - width * 0.5f;
    const int   active_tab = find_active_tab_idx(oc->menu.submenu);
    for (int t = 0; t < 8; t++) {
        float tx = base_left + ((float)t + 0.5f) * tab_w;
        int is_act = (t == active_tab);
        if (is_act) {
            ui_draw_rect(oc, tx, tab_y, tab_w - 0.0012f, tab_h, tr, tg, tb, 240);
            ui_draw_text(oc, kTabShort[t], tx, tab_y - tab_h * 0.38f, txt_s * 0.72f, 0, 1, 1.0f, 10, 14, 22, 255);
        } else {
            ui_draw_rect(oc, tx, tab_y, tab_w - 0.0012f, tab_h, 12, 16, 26, 232);
            ui_draw_text(oc, kTabShort[t], tx, tab_y - tab_h * 0.38f, txt_s * 0.70f, 0, 1, 1.0f, 185, 200, 225, 235);
        }
    }

    /* 1. Header Banner */
    const float hdr_y = tab_y + tab_h * 0.5f + hdr_h * 0.5f + 0.002f;
    ui_draw_rect(oc, menu_x, hdr_y, width, hdr_h, 10, 14, 22, 246);
    ui_draw_rect(oc, menu_x, hdr_y - hdr_h * 0.5f + 0.0018f, width, 0.0036f, tr, tg, tb, 255);
    ui_draw_text(oc, "modHEN", menu_x, hdr_y - hdr_h * 0.36f, title_s, 1, 1, 1.0f, 255, 255, 255, 255);
    ui_draw_text(oc, "PS5 NATIVE TRAINER v2.0", menu_x, hdr_y + hdr_h * 0.10f, txt_s * 0.70f, 0, 1, 1.0f, tr, tg, tb, 255);
    ui_draw_rect(oc, menu_x, hdr_y + hdr_h * 0.5f - 0.001f, width, 0.002f, tr, tg, tb, 140);

    /* 2. Breadcrumb Subtitle + Counter + FPS Bar */
    const float sub_y = hdr_y + hdr_h * 0.5f + sub_h * 0.5f + 0.001f;
    ui_draw_rect(oc, menu_x, sub_y, width, sub_h, 14, 18, 28, 245);
    ui_draw_text(oc, subtitle, left_x, sub_y - sub_h * 0.38f, txt_s * 0.90f, 0, 0, 1.0f, tr, tg, tb, 255);

    char cnt_buf[32];
    size_t pos = orbx_pic_fmt_u64(cnt_buf, 0, sizeof(cnt_buf) - 1, (uint64_t)(cur + 1));
    pos = orbx_pic_fmt_str(cnt_buf, pos, sizeof(cnt_buf) - 1, "/");
    pos = orbx_pic_fmt_u64(cnt_buf, pos, sizeof(cnt_buf) - 1, (uint64_t)row_count);
    if (oc->menu.fps_x10 >= 10u) {
        pos = orbx_pic_fmt_str(cnt_buf, pos, sizeof(cnt_buf) - 1, "  ");
        pos = orbx_pic_fmt_u64(cnt_buf, pos, sizeof(cnt_buf) - 1, oc->menu.fps_x10 / 10u);
        pos = orbx_pic_fmt_str(cnt_buf, pos, sizeof(cnt_buf) - 1, " FPS");
    }
    cnt_buf[pos] = '\0';
    ui_draw_text(oc, cnt_buf, right_x, sub_y - sub_h * 0.36f, bdg_s * 0.92f, 0, 2, right_x, 180, 205, 235, 255);
    ui_draw_rect(oc, menu_x, sub_y + sub_h * 0.5f - 0.001f, width, 0.002f, tr, tg, tb, 160);

    /* 3. Option Rows (scrolling window of up to MAX_VISIBLE_ROWS) */
    int vis = (row_count < MAX_VISIBLE_ROWS) ? row_count : MAX_VISIBLE_ROWS;
    int start = 0;
    if (cur >= MAX_VISIBLE_ROWS) {
        start = cur - MAX_VISIBLE_ROWS + 1;
    }

    const float y0 = sub_y + sub_h * 0.5f + row_h * 0.5f;
    for (int i = 0; i < vis; i++) {
        int idx = start + i;
        const MenuRow *r = &rows[idx];
        float ry = y0 + (float)i * row_h;

        if (r->kind == ROW_DIVIDER) {
            ui_draw_rect(oc, menu_x, ry, width, row_h, 16, 22, 34, 238);
            ui_draw_rect(oc, menu_x, ry + row_h * 0.5f - 0.0008f, width * 0.92f, 0.0014f, tr, tg, tb, 115);
            ui_draw_text(oc, r->label, menu_x, ry - row_h * 0.32f, txt_s * 0.80f, 0, 1, 1.0f, tr, tg, tb, 230);
            continue;
        }

        int sel = (idx == cur);
        if (sel) {
            ui_draw_rect(oc, menu_x, ry, width, row_h, 240, 245, 255, 245);
            ui_draw_rect(oc, left_x - 0.008f, ry, 0.004f, row_h, tr, tg, tb, 255);
        } else {
            ui_draw_rect(oc, menu_x, ry, width, row_h, 12, 16, 24, 218);
        }

        int tr_txt = sel ? 12 : 235;
        int tg_txt = sel ? 16 : 240;
        int tb_txt = sel ? 24 : 250;
        ui_draw_text(oc, r->label, left_x, ry - row_h * 0.36f, txt_s, 0, 0, 1.0f, tr_txt, tg_txt, tb_txt, 255);

        int br = tr_txt, bg = tg_txt, bb = tb_txt;
        if (r->kind == ROW_TOGGLE) {
            if (r->on) { br = sel ? 10 : 35;  bg = sel ? 150 : 230; bb = sel ? 50 : 110; }
            else       { br = sel ? 170 : 230; bg = sel ? 35 : 60;   bb = sel ? 35 : 60; }
        } else if (r->kind == ROW_CYCLE || r->kind == ROW_SUB) {
            br = sel ? tr / 2 : tr; bg = sel ? tg / 2 : tg; bb = sel ? tb / 2 : tb;
        } else if (r->kind == ROW_ACTION) {
            br = sel ? 30 : tr; bg = sel ? 30 : tg; bb = sel ? 30 : tb;
        }

        if (r->id == ITEM_ACT_COLOR_PRESET && r->arg < (uint8_t)(sizeof(kColorPresets) / sizeof(kColorPresets[0]))) {
            const ColorPreset *cp = &kColorPresets[r->arg];
            float box_w = 0.024f;
            float box_h = row_h * 0.62f;
            float box_x = right_x - 0.015f;
            /* Border: highlighted bright border if selected, dark border otherwise */
            ui_draw_rect(oc, box_x, ry, box_w + 0.003f, box_h + 0.003f, sel ? 250 : 25, sel ? 250 : 25, sel ? 250 : 25, 255);
            /* Color fill */
            ui_draw_rect(oc, box_x, ry, box_w, box_h, cp->r, cp->g, cp->b, 255);
        }

        if (r->kind == ROW_CYCLE) {
            char cyc_buf[32];
            cyc_buf[0] = '\0';
            size_t cp = orbx_pic_fmt_str(cyc_buf, 0, sizeof(cyc_buf) - 1, "- ");
            cp = orbx_pic_fmt_str(cyc_buf, cp, sizeof(cyc_buf) - 1, r->badge);
            cp = orbx_pic_fmt_str(cyc_buf, cp, sizeof(cyc_buf) - 1, " +");
            cyc_buf[cp] = '\0';
            ui_draw_text(oc, cyc_buf, right_x, ry - row_h * 0.34f, bdg_s, 0, 2, right_x, br, bg, bb, 255);
        } else {
            ui_draw_text(oc, r->badge, right_x, ry - row_h * 0.34f, bdg_s, 0, 2, right_x, br, bg, bb, 255);
        }
    }

    /* 4. Contextual Help / Tooltip Box */
    float help_h = sub_h * 1.35f;
    float help_y = y0 + (float)vis * row_h + help_h * 0.5f - row_h * 0.5f + 0.002f;
    ui_draw_rect(oc, menu_x, help_y, width, help_h, 10, 14, 22, 238);
    ui_draw_rect(oc, menu_x, help_y - help_h * 0.5f + 0.001f, width, 0.002f, tr, tg, tb, 140);

    const char *desc = rows[cur].desc;
    if (desc && desc[0]) {
        ui_draw_text(oc, desc, left_x, help_y - help_h * 0.30f, bdg_s * 0.90f, 0, 0, right_x, 190, 215, 240, 255);
    }

    /* 5. Dynamic 2-Line Accessibility Footer Bar */
    char s_acc[16], s_can[16], s_prev[16], s_next[16], s_open[32], foot1[72], foot2[72];
    s_acc[0] = s_can[0] = s_prev[0] = s_next[0] = s_open[0] = '\0';
    size_t fp;
    fmt_single_btn(s_acc,  0, sizeof(s_acc)  - 1, oc->menu.bind_accept   ? oc->menu.bind_accept   : SCE_PAD_BTN_CROSS);
    fmt_single_btn(s_can,  0, sizeof(s_can)  - 1, oc->menu.bind_cancel   ? oc->menu.bind_cancel   : SCE_PAD_BTN_CIRCLE);
    fmt_single_btn(s_prev, 0, sizeof(s_prev) - 1, oc->menu.bind_tab_prev ? oc->menu.bind_tab_prev : SCE_PAD_BTN_L1);
    fmt_single_btn(s_next, 0, sizeof(s_next) - 1, oc->menu.bind_tab_next ? oc->menu.bind_tab_next : SCE_PAD_BTN_R1);
    fmt_combo_btn(s_open, sizeof(s_open), oc->menu.bind_open_mod, oc->menu.bind_open_btn ? oc->menu.bind_open_btn : SCE_PAD_BTN_DOWN);

    fp = orbx_pic_fmt_str(foot1, 0, sizeof(foot1) - 1, s_acc);
    fp = orbx_pic_fmt_str(foot1, fp, sizeof(foot1) - 1, " Select   ");
    fp = orbx_pic_fmt_str(foot1, fp, sizeof(foot1) - 1, s_can);
    fp = orbx_pic_fmt_str(foot1, fp, sizeof(foot1) - 1, " Back   L/R Value");
    foot1[fp] = '\0';

    fp = orbx_pic_fmt_str(foot2, 0, sizeof(foot2) - 1, s_prev);
    fp = orbx_pic_fmt_str(foot2, fp, sizeof(foot2) - 1, "/");
    fp = orbx_pic_fmt_str(foot2, fp, sizeof(foot2) - 1, s_next);
    fp = orbx_pic_fmt_str(foot2, fp, sizeof(foot2) - 1, " Tabs   Close: ");
    fp = orbx_pic_fmt_str(foot2, fp, sizeof(foot2) - 1, s_open);
    foot2[fp] = '\0';

    float foot_h = 0.040f;
    float foot_y = help_y + help_h * 0.5f + foot_h * 0.5f + 0.001f;
    ui_draw_rect(oc, menu_x, foot_y, width, foot_h, 10, 14, 22, 246);
    ui_draw_text(oc, foot1, menu_x, foot_y - 0.0165f, bdg_s * 0.85f, 0, 1, 1.0f, 195, 220, 245, 255);
    ui_draw_text(oc, foot2, menu_x, foot_y - 0.0005f, bdg_s * 0.82f, 0, 1, 1.0f, tr, tg, tb, 235);

    /* 6. Live Controller Keybind Capture Modal */
    if (oc->menu.keybind_capture_slot != 0u) {
        ui_draw_rect(oc, 0.500f, 0.480f, 0.340f, 0.110f, 8, 12, 20, 248);
        ui_draw_rect(oc, 0.500f, 0.427f, 0.340f, 0.004f, tr, tg, tb, 255);
        ui_draw_rect(oc, 0.500f, 0.533f, 0.340f, 0.004f, tr, tg, tb, 255);
        ui_draw_text(oc, "BIND CONTROLLER INPUT", 0.500f, 0.440f, 0.40f, 1, 1, 1.0f, tr, tg, tb, 255);
        const char *prompt = "Release all buttons first...";
        if (oc->menu.keybind_capture_step == 2u) {
            prompt = (oc->menu.keybind_capture_slot == KB_SLOT_OPEN)
                     ? "Tap 1 button OR hold 2-button combo"
                     : "Press new controller button now";
        } else if (oc->menu.keybind_capture_step == 3u) {
            prompt = "Press 2nd combo button or release for single";
        }
        ui_draw_text(oc, prompt, 0.500f, 0.488f, 0.30f, 0, 1, 1.0f, 245, 250, 255, 255);
    }
}

static void draw_afp_crosshair_if_active(OrbxNativeContext *oc) {
    if (!oc || !oc->menu.afp_crosshair || !oc->menu.afp_mode || !oc->menu.afp_cam_active) return;

    if (oc->menu.afp_crosshair == 2u) { /* Only When ADS */
        int32_t pl = (int32_t)nat0(oc, ORBX_NAT_PLAYER_ID);
        int aiming = (oc->menu.natives[ORBX_NAT_IS_PLAYER_FREE_AIMING] &&
                      nat1(oc, ORBX_NAT_IS_PLAYER_FREE_AIMING, (uint64_t)pl)) ? 1 : 0;
        if (!aiming && oc->menu.natives[ORBX_NAT_IS_DISABLED_CONTROL_PRESSED]) {
            if (nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_PRESSED, CTRL_PLAYER, IN_AIM)) aiming = 1;
        }
        if (!aiming) return;
    }

    /* Outer dark border (shadow) */
    ui_draw_rect(oc, 0.50f, 0.50f, 0.0032f, 0.0050f, 0, 0, 0, 220);
    /* Center bright dot */
    ui_draw_rect(oc, 0.50f, 0.50f, 0.0016f, 0.0026f, 255, 255, 255, 240);
}

static void draw_speedo_hud(OrbxNativeContext *oc) {
    if (oc->menu.gta6_hud_on) return; /* User selected GTA VI HUD over Forza dial */
    if (!oc->menu.speedo_mode || !oc->menu.natives[ORBX_NAT_GET_ENTITY_SPEED]) return;
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) return;

    /* Fixed-char arrays for XOM-safe PIC compliance */
    static const char kTxd[16]         = "speedo_forza";
    static const char kDial[8]         = "dial";
    static const char kNeedle[8]       = "needle";
    static const char kNumNames[10][6] = { "num_0", "num_1", "num_2", "num_3", "num_4",
                                           "num_5", "num_6", "num_7", "num_8", "num_9" };
    static const char kGears[8][2]     = { "1", "2", "3", "4", "5", "6", "7", "8" };

    float spd_ms = u2f(nat1(oc, ORBX_NAT_GET_ENTITY_SPEED, (uint64_t)veh));
    if (spd_ms < 0.0f) spd_ms = 0.0f;
    uint32_t is_kph = (oc->menu.speedo_mode == 2u) ? 1u : 0u;
    float spd_kph = spd_ms * 3.6f;
    uint32_t spd_val = is_kph ? (uint32_t)(spd_kph + 0.5f) : (uint32_t)(spd_ms * 2.23694f + 0.5f);

    /* Player vehicle inputs */
    int throttle_on = nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, 0, 71) ? 1 : 0; /* R2 / Accelerate */
    int brake_on    = nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, 0, 72) ? 1 : 0; /* L2 / Brake/Reverse */
    int hbrake_on   = nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, 0, 76) ? 1 : 0; /* R1 / Handbrake */

    /* Gear & RPM simulation matching Forza 6 power curves */
    int gear = 1;
    float target_rpm = 0.09f; /* Idle */
    int lc_active = 0;

    if (spd_kph < 1.0f) {
        if (throttle_on && (brake_on || hbrake_on)) {
            lc_active = 1;
            gear = 1;
            target_rpm = 0.68f; /* Launch control spool */
        } else if (brake_on && !throttle_on) {
            gear = -1; /* Reverse */
            target_rpm = 0.22f;
        } else if (throttle_on) {
            gear = 1;
            target_rpm = 0.45f;
        } else {
            gear = 0; /* Neutral / Standstill */
            target_rpm = 0.09f;
        }
    } else if (brake_on && !throttle_on && spd_kph < 15.0f && oc->menu.speedo_cur_gear == -1) {
        gear = -1;
        target_rpm = 0.20f + (spd_kph / 45.0f) * 0.70f;
    } else {
        /* Forward gear ratios (SVJ / Revuelto / Supercar curve) */
        float g_min = 0.0f, g_max = 62.0f;
        if (spd_kph < 62.0f) {
            gear = 1; g_min = 0.0f; g_max = 62.0f;
        } else if (spd_kph < 108.0f) {
            gear = 2; g_min = 58.0f; g_max = 108.0f;
        } else if (spd_kph < 158.0f) {
            gear = 3; g_min = 102.0f; g_max = 158.0f;
        } else if (spd_kph < 215.0f) {
            gear = 4; g_min = 152.0f; g_max = 215.0f;
        } else if (spd_kph < 280.0f) {
            gear = 5; g_min = 208.0f; g_max = 280.0f;
        } else if (spd_kph < 355.0f) {
            gear = 6; g_min = 272.0f; g_max = 355.0f;
        } else {
            gear = 7; g_min = 348.0f; g_max = 440.0f;
        }
        float frac = (spd_kph - g_min) / (g_max - g_min);
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        target_rpm = 0.22f + frac * 0.74f;
    }
    oc->menu.speedo_cur_gear = gear;

    /* 60 FPS smooth needle response with visceral drop on upshift */
    float cur_rpm = oc->menu.speedo_needle_rpm;
    float alpha_blend = (target_rpm < cur_rpm) ? 0.35f : 0.20f;
    cur_rpm += (target_rpm - cur_rpm) * alpha_blend;
    if (cur_rpm < 0.08f) cur_rpm = 0.08f;
    if (cur_rpm > 1.00f) cur_rpm = 1.00f;
    oc->menu.speedo_needle_rpm = cur_rpm;

    /* Gauge screen placement & 16:9 aspect correction */
    const float cx = 0.880f;
    const float cy = 0.835f;
    const float aspect = 0.5625f; /* (9.0f / 16.0f) ensures circular geometry */
    const float dial_h = 0.235f;
    const float dial_w = dial_h * aspect; /* 0.13219f */

    /* Needle angle sweep: -148.0 deg (0 RPM) to +90.0 deg (9 RPM) = 238.0 deg total */
    float needle_deg = -148.0f + cur_rpm * 238.0f;

    /* Dynamic number/shift colors */
    int spd_r = 255, spd_g = 255, spd_b = 255;
    if (cur_rpm > 0.86f) {
        /* Redline zone: Electric Magenta */
        spd_r = 255; spd_g = 20; spd_b = 140;
    } else if (cur_rpm > 0.70f) {
        /* High RPM: Ice Cyan */
        spd_r = 0; spd_g = 225; spd_b = 255;
    }

    uint32_t capped_spd = (spd_val > 999u) ? 999u : spd_val;
    int d100 = (int)((capped_spd / 100u) % 10u);
    int d10  = (int)((capped_spd / 10u) % 10u);
    int d1   = (int)(capped_spd % 10u);

    int a100 = 55, a10 = 55, a1 = 55;
    if (capped_spd == 0u) {
        /* Completely stopped: All numbers transparent/dimmed */
        a100 = 45; a10 = 45; a1 = 65;
    } else if (capped_spd < 10u) {
        /* Single digit speed: Hundreds and Tens transparent 0s; Ones bright */
        a100 = 50; a10 = 50; a1 = 255;
    } else if (capped_spd < 100u) {
        /* Two digit speed (e.g. 029): Hundreds transparent 0; Tens & Ones bright */
        a100 = 50; a10 = 255; a1 = 255;
    } else {
        /* Three digit speed: All solid bright */
        a100 = 255; a10 = 255; a1 = 255;
    }

    /* Request streamed texture dictionary if not loaded */
    if (!has_txd_loaded(oc, kTxd)) {
        request_txd(oc, kTxd);
        /* Fallback: sleek non-blocky minimalist HUD while streaming */
        ui_draw_rect(oc, cx, cy + 0.050f, 0.120f, 0.045f, 10, 14, 22, 210);
        char sbuf[16];
        size_t sp = orbx_pic_fmt_u64(sbuf, 0, sizeof(sbuf) - 1, (uint64_t)spd_val);
        sp = orbx_pic_fmt_str(sbuf, sp, sizeof(sbuf) - 1, is_kph ? " KM/H" : " MPH");
        sbuf[sp] = '\0';
        ui_draw_text(oc, sbuf, cx, cy + 0.038f, 0.40f, 1, 1, 1.0f, spd_r, spd_g, spd_b, 255);
        return;
    }

    /* === 1. HD FORZA 6 DIAL BASE (2048x2048 Bilinear Filtered Sprite) === */
    ui_draw_sprite(oc, kTxd, kDial, cx, cy, dial_w, dial_h, 0.0f, 255, 255, 255, 255);

    /* === 2. HD DYNAMIC ROTATING NEEDLE (Bilinear Filtered, 0-Wobble Pivot) === */
    ui_draw_sprite(oc, kTxd, kNeedle, cx, cy, dial_w, dial_h, needle_deg, 255, 255, 255, 255);

    /* === 3. LIVE CENTER GEAR DISPLAY === */
    int gr_txt_r = 255, gr_txt_g = 255, gr_txt_b = 255;
    if (cur_rpm > 0.86f) {
        gr_txt_r = 255; gr_txt_g = 20; gr_txt_b = 140; /* Shift flash */
    }
    if (gear == -1) {
        ui_draw_text(oc, "R", cx, cy - 0.022f, 0.52f, 1, 1, 1.0f, 255, 90, 90, 255);
    } else if (gear == 0) {
        ui_draw_text(oc, "N", cx, cy - 0.022f, 0.52f, 1, 1, 1.0f, 180, 215, 245, 240);
    } else {
        int gidx = (gear >= 1 && gear <= 8) ? (gear - 1) : 0;
        ui_draw_text(oc, kGears[gidx], cx, cy - 0.022f, 0.52f, 1, 1, 1.0f,
                     gr_txt_r, gr_txt_g, gr_txt_b, 255);
    }

    /* === 4. LAUNCH CONTROL (LC) BADGE === */
    if (lc_active) {
        const float lcx = cx;
        const float lcy = cy - dial_h * 0.28f;
        ui_draw_rect(oc, lcx, lcy, 0.026f, 0.012f, 8, 14, 24, 230);
        ui_draw_rect(oc, lcx, lcy - 0.0055f, 0.026f, 0.0014f, 0, 240, 255, 255);
        ui_draw_rect(oc, lcx, lcy + 0.0055f, 0.026f, 0.0014f, 0, 240, 255, 255);
        ui_draw_text(oc, "LC", lcx, lcy - 0.0075f, 0.22f, 0, 1, 1.0f, 0, 240, 255, 255);
    }

    /* === 5. SPEED UNIT (KM/H or MPH) === */
    const float unit_x = cx + dial_w * 0.28f;
    const float unit_y = cy + dial_h * 0.17f;
    ui_draw_text(oc, is_kph ? "KM/H" : "MPH", unit_x, unit_y, 0.26f, 1, 1, 1.0f, 220, 235, 250, 240);

    /* === 6. HD 3-DIGIT SPEED READOUT (Transparent Leading Zeros + Dynamic Colors) === */
    const float digit_w = 0.0140f;
    const float digit_h = 0.0420f;
    const float spd_y   = cy + dial_h * 0.29f;
    const float x_100   = cx - 0.003f;
    const float x_10    = cx + 0.013f;
    const float x_1     = cx + 0.029f;

    ui_draw_sprite(oc, kTxd, kNumNames[d100], x_100, spd_y, digit_w, digit_h, 0.0f, spd_r, spd_g, spd_b, a100);
    ui_draw_sprite(oc, kTxd, kNumNames[d10],  x_10,  spd_y, digit_w, digit_h, 0.0f, spd_r, spd_g, spd_b, a10);
    ui_draw_sprite(oc, kTxd, kNumNames[d1],   x_1,   spd_y, digit_w, digit_h, 0.0f, spd_r, spd_g, spd_b, a1);

    /* === 7. REALISTIC FUEL GAUGE BAR === */
    if (oc->menu.fuel_system_on) {
        float ffrac = oc->menu.fuel_level / 100.0f;
        if (ffrac < 0.0f) ffrac = 0.0f;
        if (ffrac > 1.0f) ffrac = 1.0f;
        float bar_w = 0.080f;
        float bar_h = 0.007f;
        float bar_y = cy + dial_h * 0.44f;
        ui_draw_rect(oc, cx, bar_y, bar_w, bar_h, 12, 16, 26, 220);
        int fr = 0, fg = 220, fb = 255;
        if (oc->menu.fuel_level <= 10.0f) {
            fr = 255; fg = 40; fb = 40; /* Red alert */
        } else if (oc->menu.fuel_level <= 25.0f) {
            fr = 255; fg = 180; fb = 20; /* Amber low fuel */
        }
        float fill_w = (bar_w - 0.004f) * ffrac;
        if (fill_w > 0.001f) {
            float fill_x = cx - ((bar_w - 0.004f) * 0.5f) + (fill_w * 0.5f);
            ui_draw_rect(oc, fill_x, bar_y, fill_w, bar_h - 0.002f, fr, fg, fb, 255);
        }
        char flbl[16];
        size_t flp = orbx_pic_fmt_str(flbl, 0, sizeof(flbl) - 1, "FUEL ");
        flp = orbx_pic_fmt_u64(flbl, flp, sizeof(flbl) - 1, (uint64_t)oc->menu.fuel_level);
        flp = orbx_pic_fmt_str(flbl, flp, sizeof(flbl) - 1, "%");
        flbl[flp] = '\0';
        ui_draw_text(oc, flbl, cx, bar_y - 0.016f, 0.22f, 0, 1, 1.0f, 220, 235, 250, 240);
    }
}

static void draw_fuel_standalone_if_active(OrbxNativeContext *oc) {
    if (oc->menu.gta6_hud_on) return; /* Handled by GTA VI vehicle card */
    if (!oc->menu.fuel_system_on || oc->menu.speedo_mode != 0u) return;
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) return;

    const float cx = 0.900f;
    const float cy = 0.935f;
    const float bar_w = 0.080f;
    const float bar_h = 0.007f;
    float ffrac = oc->menu.fuel_level / 100.0f;
    if (ffrac < 0.0f) ffrac = 0.0f;
    if (ffrac > 1.0f) ffrac = 1.0f;

    ui_draw_rect(oc, cx, cy, bar_w, bar_h, 12, 16, 26, 220);
    int fr = 0, fg = 220, fb = 255;
    if (oc->menu.fuel_level <= 10.0f) {
        fr = 255; fg = 40; fb = 40;
    } else if (oc->menu.fuel_level <= 25.0f) {
        fr = 255; fg = 180; fb = 20;
    }
    float fill_w = (bar_w - 0.004f) * ffrac;
    if (fill_w > 0.001f) {
        float fill_x = cx - ((bar_w - 0.004f) * 0.5f) + (fill_w * 0.5f);
        ui_draw_rect(oc, fill_x, cy, fill_w, bar_h - 0.002f, fr, fg, fb, 255);
    }
    char flbl[16];
    size_t flp = orbx_pic_fmt_str(flbl, 0, sizeof(flbl) - 1, "FUEL ");
    flp = orbx_pic_fmt_u64(flbl, flp, sizeof(flbl) - 1, (uint64_t)oc->menu.fuel_level);
    flp = orbx_pic_fmt_str(flbl, flp, sizeof(flbl) - 1, "%");
    flbl[flp] = '\0';
    ui_draw_text(oc, flbl, cx, cy - 0.016f, 0.22f, 0, 1, 1.0f, 220, 235, 250, 240);
}

/* ============================================================================
 * GTA VI IMMERSIVE HUD & WEAPON WHEEL IMPLEMENTATION
 * ============================================================================ */

static void draw_gtavi_hud(OrbxNativeContext *oc) {
    (void)oc;
}

static void draw_toast_if_active(OrbxNativeContext *oc) {
    if (oc->menu.toast_ticks > 0) {
        oc->menu.toast_ticks--;
        /* Clean floating notification text without background box */
        ui_draw_text(oc, oc->menu.toast_msg, 0.500f, 0.042f, 0.30f, 0, 1, 1.0f, 245, 250, 255, 255);
    } else if (!oc->menu.banner_ticks) {
        oc->menu.banner_ticks = 1u;
        char cbuf[24], msg[64];
        fmt_combo_btn(cbuf, sizeof(cbuf), oc->menu.bind_open_mod,
                      oc->menu.bind_open_btn ? oc->menu.bind_open_btn : SCE_PAD_BTN_DOWN);
        size_t mp = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, "modHEN Ready! (Open: ");
        mp = orbx_pic_fmt_str(msg, mp, sizeof(msg) - 1, cbuf);
        mp = orbx_pic_fmt_str(msg, mp, sizeof(msg) - 1, ")");
        msg[mp] = '\0';
        set_toast(oc, msg);
    }
}

static void update_fps_meter(OrbxNativeContext *oc, uint32_t now_ms) {
    if (!oc->menu.fps_sample_start_ms) {
        oc->menu.fps_sample_start_ms = now_ms;
        oc->menu.fps_sample_frames = 0;
        return;
    }
    oc->menu.fps_sample_frames++;
    uint32_t dt = now_ms - oc->menu.fps_sample_start_ms;
    if (dt >= 1000u) {
        oc->menu.fps_x10 = (oc->menu.fps_sample_frames * 10000u) / dt;
        oc->menu.fps_sample_start_ms = now_ms;
        oc->menu.fps_sample_frames = 0;
    }
}

static int is_single_pad_bit(uint32_t v) {
    return v != 0u && (v & (v - 1u)) == 0u;
}

static int sanitize_live_keybinds(OrbxNativeContext *oc) {
    int dirty = 0;
    const uint32_t DPAD = 0x000000F0u;

    if (!is_single_pad_bit(oc->menu.bind_accept) || (oc->menu.bind_accept & DPAD)) {
        oc->menu.bind_accept = SCE_PAD_BTN_CROSS;
        dirty = 1;
    }
    if (!is_single_pad_bit(oc->menu.bind_cancel) || (oc->menu.bind_cancel & DPAD) ||
        oc->menu.bind_cancel == oc->menu.bind_accept) {
        oc->menu.bind_cancel = (oc->menu.bind_accept != SCE_PAD_BTN_CIRCLE) ? SCE_PAD_BTN_CIRCLE : 0x00001000u;
        dirty = 1;
    }
    if (!is_single_pad_bit(oc->menu.bind_tab_prev) || (oc->menu.bind_tab_prev & DPAD) ||
        oc->menu.bind_tab_prev == oc->menu.bind_accept || oc->menu.bind_tab_prev == oc->menu.bind_cancel) {
        oc->menu.bind_tab_prev = SCE_PAD_BTN_L1;
        dirty = 1;
    }
    if (!is_single_pad_bit(oc->menu.bind_tab_next) || (oc->menu.bind_tab_next & DPAD) ||
        oc->menu.bind_tab_next == oc->menu.bind_accept || oc->menu.bind_tab_next == oc->menu.bind_cancel ||
        oc->menu.bind_tab_next == oc->menu.bind_tab_prev) {
        oc->menu.bind_tab_next = (oc->menu.bind_tab_prev != SCE_PAD_BTN_R1) ? SCE_PAD_BTN_R1 : SCE_PAD_BTN_L1;
        dirty = 1;
    }

    if (!is_single_pad_bit(oc->menu.bind_open_btn)) {
        oc->menu.bind_open_mod = SCE_PAD_BTN_R1;
        oc->menu.bind_open_btn = SCE_PAD_BTN_DOWN;
        dirty = 1;
    }
    if (oc->menu.bind_open_mod && !is_single_pad_bit(oc->menu.bind_open_mod)) {
        oc->menu.bind_open_mod = SCE_PAD_BTN_R1;
        dirty = 1;
    }
    if (oc->menu.bind_open_mod == oc->menu.bind_open_btn) {
        oc->menu.bind_open_mod = 0u;
        dirty = 1;
    }

    int bad_open = 0;
    if (oc->menu.bind_open_btn == oc->menu.bind_accept || oc->menu.bind_open_btn == oc->menu.bind_cancel) {
        bad_open = 1;
    } else if (oc->menu.bind_open_mod == 0u) {
        if ((oc->menu.bind_open_btn & DPAD) ||
            oc->menu.bind_open_btn == oc->menu.bind_tab_prev ||
            oc->menu.bind_open_btn == oc->menu.bind_tab_next) {
            bad_open = 1;
        }
    } else {
        if ((oc->menu.bind_open_mod & DPAD) ||
            oc->menu.bind_open_mod == oc->menu.bind_accept ||
            oc->menu.bind_open_mod == oc->menu.bind_cancel) {
            bad_open = 1;
        }
    }
    if (bad_open) {
        oc->menu.bind_open_mod = SCE_PAD_BTN_R1;
        oc->menu.bind_open_btn = SCE_PAD_BTN_DOWN;
        dirty = 1;
    }

    uint32_t nav_forbidden = DPAD | oc->menu.bind_accept | oc->menu.bind_cancel;
    if (oc->menu.bind_horn_boost   && (!is_single_pad_bit(oc->menu.bind_horn_boost)   || (oc->menu.bind_horn_boost   & nav_forbidden))) { oc->menu.bind_horn_boost   = 0u; dirty = 1; }
    if (oc->menu.bind_noclip       && (!is_single_pad_bit(oc->menu.bind_noclip)       || (oc->menu.bind_noclip       & nav_forbidden))) { oc->menu.bind_noclip       = 0u; dirty = 1; }
    if (oc->menu.bind_quick_repair && (!is_single_pad_bit(oc->menu.bind_quick_repair) || (oc->menu.bind_quick_repair & nav_forbidden))) { oc->menu.bind_quick_repair = 0u; dirty = 1; }
    if (oc->menu.bind_quick_heal   && (!is_single_pad_bit(oc->menu.bind_quick_heal)   || (oc->menu.bind_quick_heal   & nav_forbidden))) { oc->menu.bind_quick_heal   = 0u; dirty = 1; }
    if (oc->menu.bind_super_brake  && (!is_single_pad_bit(oc->menu.bind_super_brake)  || (oc->menu.bind_super_brake  & nav_forbidden))) { oc->menu.bind_super_brake  = 0u; dirty = 1; }

    recompute_suppress_mask(oc);
    return dirty;
}

static void apply_captured_keybind(OrbxNativeContext *oc, uint32_t slot,
                                   uint32_t mod, uint32_t btn, uint32_t now_ms) {
    const uint32_t DPAD = 0x000000F0u;
    const char *reject_msg = NULL;

    if (!is_single_pad_bit(btn)) {
        reject_msg = "Invalid Keybind";
    } else if (slot == KB_SLOT_OPEN) {
        uint32_t norm_mod = (mod != btn) ? mod : 0u;
        if (norm_mod && !is_single_pad_bit(norm_mod)) {
            reject_msg = "Invalid Open Modifier";
        } else if (btn == oc->menu.bind_accept || btn == oc->menu.bind_cancel) {
            reject_msg = "Cannot Use Accept/Cancel";
        } else if (norm_mod == 0u && ((btn & DPAD) || btn == oc->menu.bind_tab_prev || btn == oc->menu.bind_tab_next)) {
            reject_msg = "D-Pad/L1/R1 Needs Modifier";
        } else if (norm_mod != 0u && ((norm_mod & DPAD) || norm_mod == oc->menu.bind_accept || norm_mod == oc->menu.bind_cancel)) {
            reject_msg = "Modifier Cannot Be D-Pad/X/O";
        }
        mod = norm_mod;
    } else if (slot == KB_SLOT_ACCEPT) {
        if ((btn & DPAD) || btn == oc->menu.bind_cancel || btn == oc->menu.bind_tab_prev ||
            btn == oc->menu.bind_tab_next || btn == oc->menu.bind_open_btn ||
            (oc->menu.bind_open_mod && btn == oc->menu.bind_open_mod)) {
            reject_msg = "Conflicts With Menu Nav";
        }
    } else if (slot == KB_SLOT_CANCEL) {
        if ((btn & DPAD) || btn == oc->menu.bind_accept || btn == oc->menu.bind_tab_prev ||
            btn == oc->menu.bind_tab_next || btn == oc->menu.bind_open_btn ||
            (oc->menu.bind_open_mod && btn == oc->menu.bind_open_mod)) {
            reject_msg = "Conflicts With Menu Nav";
        }
    } else if (slot == KB_SLOT_TAB_PREV || slot == KB_SLOT_TAB_NEXT) {
        uint32_t other = (slot == KB_SLOT_TAB_PREV) ? oc->menu.bind_tab_next : oc->menu.bind_tab_prev;
        if ((btn & DPAD) || btn == oc->menu.bind_accept || btn == oc->menu.bind_cancel || btn == other) {
            reject_msg = "Conflicts With Menu Nav";
        }
    } else {
        if ((btn & DPAD) || btn == oc->menu.bind_accept || btn == oc->menu.bind_cancel) {
            reject_msg = "Cannot Bind D-Pad/Accept/Cancel";
        }
    }

    if (reject_msg) {
        oc->menu.keybind_capture_slot    = 0u;
        oc->menu.keybind_capture_step    = 0u;
        oc->menu.open_combo_wait_rel     = btn | mod;
        oc->menu.hotkey_lockout_until_ms = now_ms + 1200u;
        set_toast(oc, reject_msg);
        return;
    }

    if      (slot == KB_SLOT_OPEN)         { oc->menu.bind_open_mod = mod; oc->menu.bind_open_btn = btn; }
    else if (slot == KB_SLOT_ACCEPT)       oc->menu.bind_accept       = btn;
    else if (slot == KB_SLOT_CANCEL)       oc->menu.bind_cancel       = btn;
    else if (slot == KB_SLOT_TAB_PREV)     oc->menu.bind_tab_prev     = btn;
    else if (slot == KB_SLOT_TAB_NEXT)     oc->menu.bind_tab_next     = btn;
    else if (slot == KB_SLOT_HORN_BOOST)   oc->menu.bind_horn_boost   = btn;
    else if (slot == KB_SLOT_NOCLIP)       oc->menu.bind_noclip       = btn;
    else if (slot == KB_SLOT_QUICK_REPAIR) oc->menu.bind_quick_repair = btn;
    else if (slot == KB_SLOT_QUICK_HEAL)   oc->menu.bind_quick_heal   = btn;
    else if (slot == KB_SLOT_SUPER_BRAKE)  oc->menu.bind_super_brake  = btn;

    sanitize_live_keybinds(oc);
    oc->menu.keybind_capture_slot    = 0u;
    oc->menu.keybind_capture_step    = 0u;
    oc->menu.open_combo_wait_rel     = btn | mod;
    oc->menu.hotkey_lockout_until_ms = now_ms + 2000u;
    oc->menu.cfg_save_req            = 1u;

    char cbuf[24], msg[64];
    fmt_combo_btn(cbuf, sizeof(cbuf), (slot == KB_SLOT_OPEN) ? oc->menu.bind_open_mod : 0u, btn);
    size_t mp = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, "Keybind Saved: ");
    mp = orbx_pic_fmt_str(msg, mp, sizeof(msg) - 1, cbuf);
    msg[mp] = '\0';
    set_toast(oc, msg);
    play_ui_sound(oc, "SELECT");
}

/* ============================================================================
 * Main Script-Thread Tick Entry (Called via WAIT native handler slot)
 * ============================================================================ */

static void menu_tick(OrbxNativeContext *oc) {
    __atomic_add_fetch(&oc->menu.main_ticks, 1u, __ATOMIC_RELAXED);
    ensure_menu_initialized(oc);
    sanitize_menu_toggles(oc);

    if (oc->menu.quick_heal_req)   { oc->menu.quick_heal_req = 0u;   act_heal_armor(oc); }
    if (oc->menu.quick_repair_req) { oc->menu.quick_repair_req = 0u; act_fix_vehicle(oc); }

    uint32_t now_ms = (uint32_t)nat0(oc, ORBX_NAT_GET_GAME_TIMER);
    oc->menu.last_timer = now_ms;
    update_fps_meter(oc, now_ms);

    if ((oc->menu.main_ticks % 30u) == 0u && sanitize_live_keybinds(oc)) {
        oc->menu.cfg_save_req = 1u;
    }
    uint32_t raw_latch = 0u, raw_live = 0u;
    if (oc->menu.pad_calls > 0) {
        raw_latch = __atomic_exchange_n(&oc->menu.pad_buttons_latch, 0u, __ATOMIC_ACQ_REL);
        raw_live  = __atomic_load_n(&oc->menu.pad_buttons_live, __ATOMIC_ACQUIRE) | raw_latch;
    }
    uint32_t raw_prev = oc->menu.prev_live_btn_mask;
    uint32_t raw_edge = raw_live & ~raw_prev;

    /* Live Controller Keybind Capture Modal */
    if (oc->menu.keybind_capture_slot != 0u) {
        if (oc->menu.pad_calls == 0) {
            oc->menu.keybind_capture_slot = 0u;
            oc->menu.keybind_capture_step = 0u;
            set_toast(oc, "Controller pad hook inactive");
        } else {
            const uint32_t kBindable =
                SCE_PAD_BTN_CROSS | SCE_PAD_BTN_CIRCLE | SCE_PAD_BTN_SQUARE | SCE_PAD_BTN_TRIANGLE |
                SCE_PAD_BTN_L1 | SCE_PAD_BTN_R1 | SCE_PAD_BTN_L2 | SCE_PAD_BTN_R2 |
                SCE_PAD_BTN_L3 | SCE_PAD_BTN_R3 | SCE_PAD_BTN_UP | SCE_PAD_BTN_DOWN |
                SCE_PAD_BTN_LEFT | SCE_PAD_BTN_RIGHT | SCE_PAD_BTN_TOUCH_PAD | SCE_PAD_BTN_OPTIONS;
            uint32_t cap_held = raw_live & kBindable;

            if (oc->menu.keybind_capture_step == 1u) {
                if (cap_held == 0u) {
                    oc->menu.keybind_capture_step = 2u;
                    oc->menu.hold_btn_mask = 0u;
                }
            } else if (oc->menu.keybind_capture_step == 2u) {
                if (cap_held != 0u) {
                    uint32_t b1 = cap_held & (~cap_held + 1u);
                    if (oc->menu.keybind_capture_slot != KB_SLOT_OPEN) {
                        apply_captured_keybind(oc, oc->menu.keybind_capture_slot, 0u, b1, now_ms);
                    } else {
                        uint32_t rem = cap_held & ~b1;
                        if (rem != 0u) {
                            uint32_t b2 = rem & (~rem + 1u);
                            apply_captured_keybind(oc, KB_SLOT_OPEN, b1, b2, now_ms);
                        } else {
                            oc->menu.hold_btn_mask        = b1;
                            oc->menu.hold_start_ms        = now_ms;
                            oc->menu.keybind_capture_step = 3u;
                        }
                    }
                }
            } else if (oc->menu.keybind_capture_step == 3u) {
                uint32_t b1  = oc->menu.hold_btn_mask;
                uint32_t rem = cap_held & ~b1;
                if (rem != 0u) {
                    uint32_t b2 = rem & (~rem + 1u);
                    apply_captured_keybind(oc, KB_SLOT_OPEN, b1, b2, now_ms);
                } else if (cap_held == 0u || (now_ms - oc->menu.hold_start_ms) > 350u) {
                    apply_captured_keybind(oc, KB_SLOT_OPEN, 0u, b1, now_ms);
                }
            }
        }

        oc->menu.prev_live_btn_mask = raw_live;
        oc->menu.prev_btn_mask      = poll_button_mask_sampled(oc, raw_live);
        process_web_action(oc);
        apply_continuous_features(oc, now_ms);
        tick_async_requests(oc);
        draw_toast_if_active(oc);
        draw_speedo_hud(oc);
        draw_fuel_standalone_if_active(oc);
        draw_afp_crosshair_if_active(oc);
        draw_gtavi_hud(oc);
        disable_game_controls_while_open(oc);

        MenuRow *rows = (MenuRow *)(void *)oc->menu_rows_scratch;
        int row_count = 0;
        const char *title = "SETTINGS / KEYS";
        uint32_t sub = oc->menu.submenu;
        if (sub >= ORBX_SUB_MAX) sub = ORBX_SUB_MAIN;
        build_submenu_rows(oc, sub, rows, &row_count, &title);
        if (row_count > 0) {
            int32_t cur = oc->menu.cursor[sub];
            if (cur < 0 || cur >= row_count) cur = 0;
            draw_menu_hud(oc, title, rows, row_count, cur);
        }
        return;
    }

    uint32_t cur_mask = poll_button_mask_sampled(oc, raw_live);
    uint32_t trig     = compute_triggered_mask(oc, now_ms, cur_mask);

    uint32_t o_mod = 0u;
    uint32_t o_btn = 0u;
    if (oc->menu.pad_calls > 0) {
        o_mod = oc->menu.bind_open_mod;
        o_btn = oc->menu.bind_open_btn ? oc->menu.bind_open_btn : SCE_PAD_BTN_DOWN;
    } else {
        o_mod = BTN_RB;
        o_btn = BTN_DOWN;
    }

    /* Custom Open / Close Combo */
    int open_toggled = 0;
    if (oc->menu.pad_calls > 0) {
        if (o_mod != 0u && o_mod != o_btn) {
            if (((raw_live & o_mod) && (raw_edge & o_btn)) ||
                ((raw_live & o_btn) && (raw_edge & o_mod))) {
                open_toggled = 1;
            }
        } else if (raw_edge & o_btn) {
            open_toggled = 1;
        }
    } else {
        if (((cur_mask & BTN_RB) && (trig & BTN_DOWN)) ||
            ((cur_mask & BTN_DOWN) && (trig & BTN_RB))) {
            open_toggled = 1;
        }
    }

    if (open_toggled) {
        oc->menu.menu_open ^= 1u;
        oc->menu.open_combo_wait_rel = (o_mod != 0u && o_mod != o_btn) ? (o_mod | o_btn) : o_btn;
        oc->menu.tab_pend_dir        = 0;
        oc->menu.tab_pend_btn        = 0u;
        oc->menu.hold_btn_mask       = 0u;
        if (!oc->menu.menu_open) {
            oc->menu.hotkey_lockout_until_ms = now_ms + 350u;
        }
        play_ui_sound(oc, oc->menu.menu_open ? "SELECT" : "BACK");
    }

    uint32_t held_src = (oc->menu.pad_calls > 0) ? raw_live : cur_mask;
    if (oc->menu.open_combo_wait_rel != 0u) {
        oc->menu.open_combo_wait_rel &= held_src;
    }

    /* Quick In-Game Hotkeys (active only while menu is closed & combo released) */
    if (!oc->menu.menu_open && !open_toggled && !oc->menu.open_combo_wait_rel &&
        !oc->menu.kb_active && oc->menu.pad_calls > 0 &&
        now_ms >= oc->menu.hotkey_lockout_until_ms) {
        int mod_held = (oc->menu.bind_open_mod && (raw_live & oc->menu.bind_open_mod)) ? 1 : 0;
        if (!mod_held) {
            if (oc->menu.bind_noclip && (raw_edge & oc->menu.bind_noclip)) {
                oc->menu.noclip_on ^= 1u;
                set_toast(oc, oc->menu.noclip_on ? "Airbreak ON" : "Airbreak OFF");
            }
            if (oc->menu.bind_quick_repair && (raw_edge & oc->menu.bind_quick_repair)) {
                act_fix_vehicle(oc);
            }
            if (oc->menu.bind_quick_heal && (raw_edge & oc->menu.bind_quick_heal)) {
                act_heal_armor(oc);
            }
            if (oc->menu.bind_super_brake && (raw_live & oc->menu.bind_super_brake)) {
                int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
                int32_t veh = get_current_veh(oc, ped);
                if (veh) nat2(oc, ORBX_NAT_SET_VEHICLE_FORWARD_SPEED, (uint64_t)veh, f2u(0.0f));
            }
        }
    }

    oc->menu.prev_live_btn_mask = raw_live;

    process_web_action(oc);
    apply_continuous_features(oc, now_ms);
    tick_async_requests(oc);
    draw_toast_if_active(oc);
    draw_speedo_hud(oc);
    draw_fuel_standalone_if_active(oc);
    draw_afp_crosshair_if_active(oc);
    draw_gtavi_hud(oc);

    if (oc->menu.kb_active) {
        int32_t kb_st = (int32_t)nat0(oc, ORBX_NAT_UPDATE_ONSCREEN_KEYBOARD);
        if (kb_st == 0) return;
        uint32_t mode = oc->menu.kb_mode;
        oc->menu.kb_active = 0u;
        oc->menu.kb_mode   = 0u;
        oc->menu.prev_btn_mask = poll_button_mask(oc);
        if (kb_st == 1) {
            const char *typed = (const char *)(uintptr_t)nat0(oc, ORBX_NAT_GET_ONSCREEN_KEYBOARD_RESULT);
            char clean[36];
            size_t ci = 0;
            if (typed) {
                const char *s = typed;
                while (*s == ' ' || *s == '\t') s++;
                while (*s && *s != '\r' && *s != '\n' && ci + 1 < sizeof(clean)) {
                    if (mode == 0u && *s == ' ') break;
                    clean[ci++] = *s++;
                }
                while (ci > 0 && clean[ci - 1] == ' ') ci--;
            }
            clean[ci] = '\0';
            if (mode == 1u) {
                if (clean[0] != '\0') {
                    clean[8] = '\0';
                    act_plate_vehicle(oc, clean);
                } else {
                    set_toast(oc, "Empty plate text");
                }
            } else if (clean[0] != '\0') {
                uint32_t h = orbx_pic_joaat(clean);
                int already = 0;
                for (uint32_t ai = 0; ai < oc->menu.addon_car_count && ai < ORBX_MAX_ADDON_CARS; ai++) {
                    const char *a = oc->menu.addon_cars[ai].name;
                    const char *b = clean;
                    int match = 1;
                    while (*a && *b) {
                        char ca = *a++;
                        char cb = *b++;
                        if (ca >= 'A' && ca <= 'Z') ca += ('a' - 'A');
                        if (cb >= 'A' && cb <= 'Z') cb += ('a' - 'A');
                        if (ca != cb) { match = 0; break; }
                    }
                    if (match && *a == '\0' && *b == '\0') {
                        already = 1;
                        break;
                    }
                }
                if (!already && oc->menu.addon_car_count < ORBX_MAX_ADDON_CARS) {
                    uint32_t slot = oc->menu.addon_car_count;
                    copy_cstr(oc->menu.addon_cars[slot].name, sizeof(oc->menu.addon_cars[slot].name), clean);
                    copy_cstr(oc->menu.addon_cars[slot].label, sizeof(oc->menu.addon_cars[slot].label), clean);
                    oc->menu.addon_cars[slot].hash = h;
                    oc->menu.addon_car_count = slot + 1u;
                }
                start_vehicle_spawn(oc, h);
            } else {
                set_toast(oc, "Empty vehicle model name");
            }
        } else {
            set_toast(oc, (mode == 1u) ? "Plate edit cancelled" : "Spawn cancelled");
        }
        return;
    }

    if (!oc->menu.menu_open) return;

    disable_game_controls_while_open(oc);

    _Static_assert(sizeof(MenuRow) <= 128u, "MenuRow exceeds menu_rows_scratch slot size");
    MenuRow *rows = (MenuRow *)(void *)oc->menu_rows_scratch;
    int row_count = 0;
    const char *title = "HOME";
    uint32_t sub = oc->menu.submenu;
    if (sub >= ORBX_SUB_MAX) sub = ORBX_SUB_MAIN;
    build_submenu_rows(oc, sub, rows, &row_count, &title);
    if (row_count <= 0) return;

    int32_t cur = oc->menu.cursor[sub];
    if (cur < 0) cur = 0;
    if (cur >= row_count) cur = row_count - 1;
    for (int s = 0; s < row_count && rows[cur].kind == ROW_DIVIDER; s++) {
        cur = (cur + 1) % row_count;
    }

    /* Do not process menu navigation on the frame we toggled open, or until open combo is released */
    if (open_toggled || oc->menu.open_combo_wait_rel != 0u) {
        oc->menu.tab_pend_dir  = 0;
        oc->menu.tab_pend_btn  = 0u;
        oc->menu.hold_btn_mask = 0u;
        oc->menu.prev_btn_mask = cur_mask;
        oc->menu.cursor[sub]   = cur;
        draw_menu_hud(oc, title, rows, row_count, cur);
        return;
    }

    /* Smart tap-vs-combo tab switching when tab key overlaps open/close modifier (e.g. R1 vs R1+D-Down) */
    int tab_dir_req = 0;
    if (oc->menu.pad_calls > 0) {
        uint32_t b_prev = oc->menu.bind_tab_prev ? oc->menu.bind_tab_prev : SCE_PAD_BTN_L1;
        uint32_t b_next = oc->menu.bind_tab_next ? oc->menu.bind_tab_next : SCE_PAD_BTN_R1;
        int has_combo_open = (o_mod != 0u && o_mod != o_btn) ? 1 : 0;
        uint32_t open_combo_bits = has_combo_open ? (o_mod | o_btn) : o_btn;

        if (oc->menu.tab_pend_dir != 0) {
            if ((raw_edge & ~oc->menu.tab_pend_btn) != 0u) {
                oc->menu.tab_pend_dir = 0;
                oc->menu.tab_pend_btn = 0u;
            } else if ((raw_live & oc->menu.tab_pend_btn) == 0u) {
                if ((now_ms - oc->menu.tab_pend_start_ms) < 550u) {
                    tab_dir_req = oc->menu.tab_pend_dir;
                }
                oc->menu.tab_pend_dir = 0;
                oc->menu.tab_pend_btn = 0u;
            }
        } else if ((raw_edge & b_prev) && (b_prev != b_next || !(raw_edge & b_next))) {
            if (has_combo_open && (b_prev & open_combo_bits)) {
                oc->menu.tab_pend_dir      = -1;
                oc->menu.tab_pend_btn      = b_prev;
                oc->menu.tab_pend_start_ms = now_ms;
            } else {
                tab_dir_req = -1;
            }
        } else if (raw_edge & b_next) {
            if (has_combo_open && (b_next & open_combo_bits)) {
                oc->menu.tab_pend_dir      = 1;
                oc->menu.tab_pend_btn      = b_next;
                oc->menu.tab_pend_start_ms = now_ms;
            } else {
                tab_dir_req = 1;
            }
        }
    } else {
        if (oc->menu.tab_pend_dir != 0) {
            if ((trig & ~oc->menu.tab_pend_btn) != 0u) {
                oc->menu.tab_pend_dir = 0;
                oc->menu.tab_pend_btn = 0u;
            } else if ((cur_mask & oc->menu.tab_pend_btn) == 0u) {
                if ((now_ms - oc->menu.tab_pend_start_ms) < 550u) {
                    tab_dir_req = oc->menu.tab_pend_dir;
                }
                oc->menu.tab_pend_dir = 0;
                oc->menu.tab_pend_btn = 0u;
            }
        } else if (trig & BTN_LB) {
            tab_dir_req = -1;
        } else if (trig & BTN_RB) {
            oc->menu.tab_pend_dir      = 1;
            oc->menu.tab_pend_btn      = BTN_RB;
            oc->menu.tab_pend_start_ms = now_ms;
        }
    }

    uint32_t unbind_sq = (oc->menu.pad_calls > 0 &&
                          oc->menu.bind_accept   != SCE_PAD_BTN_SQUARE &&
                          oc->menu.bind_cancel   != SCE_PAD_BTN_SQUARE &&
                          oc->menu.bind_tab_prev != SCE_PAD_BTN_SQUARE &&
                          oc->menu.bind_tab_next != SCE_PAD_BTN_SQUARE &&
                          (raw_edge & SCE_PAD_BTN_SQUARE)) ? 1u : 0u;

    if (tab_dir_req != 0) {
        oc->menu.cursor[sub] = cur;
        jump_top_tab(oc, tab_dir_req);
        play_ui_sound(oc, "NAV_LEFT_RIGHT");
        sub = oc->menu.submenu;
        build_submenu_rows(oc, sub, rows, &row_count, &title);
        if (row_count <= 0) return;
        cur = oc->menu.cursor[sub];
        if (cur < 0 || cur >= row_count) cur = 0;
        for (int s = 0; s < row_count && rows[cur].kind == ROW_DIVIDER; s++) cur = (cur + 1) % row_count;
    } else if (trig & BTN_UP) {
        for (int s = 0; s < row_count; s++) {
            cur = (cur - 1 + row_count) % row_count;
            if (rows[cur].kind != ROW_DIVIDER) break;
        }
        play_ui_sound(oc, "NAV_UP_DOWN");
    } else if (trig & BTN_DOWN) {
        for (int s = 0; s < row_count; s++) {
            cur = (cur + 1) % row_count;
            if (rows[cur].kind != ROW_DIVIDER) break;
        }
        play_ui_sound(oc, "NAV_UP_DOWN");
    } else if (trig & BTN_CANCEL) {
        oc->menu.cursor[sub] = cur;
        play_ui_sound(oc, "BACK");
        pop_submenu(oc);
        if (oc->menu.menu_open) {
            sub = oc->menu.submenu;
            build_submenu_rows(oc, sub, rows, &row_count, &title);
            if (row_count <= 0) return;
            cur = oc->menu.cursor[sub];
            if (cur < 0 || cur >= row_count) cur = 0;
            for (int s = 0; s < row_count && rows[cur].kind == ROW_DIVIDER; s++) cur = (cur + 1) % row_count;
        }
    } else if ((trig & (BTN_ACCEPT | BTN_LEFT | BTN_RIGHT)) || unbind_sq) {
        const MenuRow *sel = &rows[cur];
        if (sel->kind == ROW_CYCLE && (trig & (BTN_LEFT | BTN_RIGHT | BTN_ACCEPT))) {
            int dir = (trig & BTN_LEFT) ? -1 : 1;
            play_ui_sound(oc, "NAV_LEFT_RIGHT");
            execute_row(oc, sel, dir);
            build_submenu_rows(oc, oc->menu.submenu, rows, &row_count, &title);
        } else if (sel->kind == ROW_TOGGLE && (trig & (BTN_ACCEPT | BTN_LEFT | BTN_RIGHT))) {
            play_ui_sound(oc, "SELECT");
            execute_row(oc, sel, 1);
            build_submenu_rows(oc, sub, rows, &row_count, &title);
        } else if (sel->kind == ROW_SUB && (trig & BTN_ACCEPT)) {
            oc->menu.cursor[sub] = cur;
            play_ui_sound(oc, "SELECT");
            execute_row(oc, sel, 1);
            sub = oc->menu.submenu;
            build_submenu_rows(oc, sub, rows, &row_count, &title);
            if (row_count > 0) {
                cur = oc->menu.cursor[sub];
                if (cur < 0 || cur >= row_count) cur = 0;
                for (int s = 0; s < row_count && rows[cur].kind == ROW_DIVIDER; s++) cur = (cur + 1) % row_count;
            }
        } else if (sel->id == ITEM_ACT_KEYBIND_SLOT && sel->arg >= KB_SLOT_HORN_BOOST &&
                   unbind_sq && !(trig & BTN_ACCEPT)) {
            play_ui_sound(oc, "BACK");
            unbind_quick_slot(oc, sel->arg);
            build_submenu_rows(oc, sub, rows, &row_count, &title);
        } else if (trig & BTN_ACCEPT) {
            play_ui_sound(oc, "SELECT");
            execute_row(oc, sel, 1);
            if (oc->menu.submenu == sub) {
                build_submenu_rows(oc, sub, rows, &row_count, &title);
            } else {
                oc->menu.cursor[sub] = cur;
                sub = oc->menu.submenu;
                build_submenu_rows(oc, sub, rows, &row_count, &title);
                if (row_count > 0) {
                    cur = oc->menu.cursor[sub];
                    if (cur < 0 || cur >= row_count) cur = 0;
                    for (int s = 0; s < row_count && rows[cur].kind == ROW_DIVIDER; s++) cur = (cur + 1) % row_count;
                }
            }
        }
    }

    if (row_count > 0 && cur >= row_count) cur = row_count - 1;
    oc->menu.cursor[sub] = cur;
    if (oc->menu.menu_open && row_count > 0) {
        draw_menu_hud(oc, title, rows, row_count, cur);
    }
}

__attribute__((used, visibility("hidden")))
void hook_menu_wait(void *nat_ctx_ptr) {
    OrbxNativeContext *oc = orbx_pic_get_ctx();
    if (!oc) return;
    uint64_t wait_addr = oc->orig_wait ? oc->orig_wait : oc->menu.natives[ORBX_NAT_WAIT];
    if (!wait_addr) return;

    __atomic_add_fetch(&oc->menu.wait_calls, 1u, __ATOMIC_RELAXED);

    if (oc->menu.armed && oc->menu.natives[ORBX_NAT_GET_HASH_OF_THIS_SCRIPT_NAME]) {
        uint32_t sh = (uint32_t)nat0(oc, ORBX_NAT_GET_HASH_OF_THIS_SCRIPT_NAME);
        oc->menu.last_script_hash = sh;
        int should_tick = 0;
        if (sh == SCR_HASH_MAIN_PERSISTENT) {
            oc->menu.mp_last_wait_call = oc->menu.wait_calls;
            should_tick = 1;
        } else if (sh == SCR_HASH_FREEMODE) {
            if ((oc->menu.wait_calls - oc->menu.mp_last_wait_call) > 200u) {
                should_tick = 1;
            }
        }
        if (should_tick) {
            if (!oc->menu.first_tick_logged) {
                oc->menu.first_tick_logged = 1u;
                char msg[ORBX_LOG_LINE_BYTES];
                size_t p = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, "[MENU] Script tick active on ");
                p = orbx_pic_fmt_hex32(msg, p, sizeof(msg) - 1, sh);
                if (oc->menu.natives[ORBX_NAT_GET_THIS_SCRIPT_NAME]) {
                    const char *sn = (const char *)(uintptr_t)nat0(oc, ORBX_NAT_GET_THIS_SCRIPT_NAME);
                    if (sn) {
                        p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, " (");
                        p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, sn);
                        p = orbx_pic_fmt_str(msg, p, sizeof(msg) - 1, ")");
                    }
                }
                msg[p] = '\0';
                orbx_pic_log_raw(oc, msg);
            }
            menu_tick(oc);
            if (oc->menu.first_tick_logged == 1u) {
                oc->menu.first_tick_logged = 2u;
                orbx_pic_log_raw(oc, "[MENU] First menu_tick completed OK");
            }
        }
    }

    ((GtaNativeFn)(uintptr_t)wait_addr)((GtaNativeCtx *)nat_ctx_ptr);
}
