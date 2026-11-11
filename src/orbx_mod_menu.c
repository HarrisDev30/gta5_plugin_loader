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

#define SCR_HASH_MAIN_PERSISTENT 0x5700179CU
#define SCR_HASH_FREEMODE        0xC875557DU

/* PS4/PS5 GTA V control IDs */
#define CTRL_PLAYER   0
#define CTRL_FRONTEND 2

#define IN_CHAR_WHEEL      19
#define IN_SPRINT          21
#define IN_JUMP            22
#define IN_PHONE           27
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

/* Sony Orbis / PS5 ScePadData::buttons hardware bitmasks */
#define SCE_PAD_BTN_L3      0x00000002u
#define SCE_PAD_BTN_UP      0x00000010u
#define SCE_PAD_BTN_RIGHT   0x00000020u
#define SCE_PAD_BTN_DOWN    0x00000040u
#define SCE_PAD_BTN_LEFT    0x00000080u
#define SCE_PAD_BTN_L1      0x00000400u
#define SCE_PAD_BTN_R1      0x00000800u
#define SCE_PAD_BTN_CIRCLE  0x00002000u
#define SCE_PAD_BTN_CROSS   0x00004000u

/* Debounced button bitmask */
#define BTN_RB      (1u << 0)
#define BTN_LB      (1u << 1)
#define BTN_UP      (1u << 2)
#define BTN_DOWN    (1u << 3)
#define BTN_LEFT    (1u << 4)
#define BTN_RIGHT   (1u << 5)
#define BTN_ACCEPT  (1u << 6)
#define BTN_CANCEL  (1u << 7)

/* Applied transition flags so reverting a toggle off runs cleanly once */
#define APP_GOD       (1u << 0)
#define APP_WANTED    (1u << 1)
#define APP_FASTRUN   (1u << 2)
#define APP_NORAGDOLL (1u << 3)
#define APP_INF_AMMO  (1u << 4)
#define APP_VEH_GOD   (1u << 5)

#define MAX_VISIBLE_ROWS 10
#define MAX_MENU_ROWS    72

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

typedef void (*GtaNativeFn)(GtaNativeCtx *ctx);

enum RowKind {
    ROW_SUB = 0,
    ROW_TOGGLE = 1,
    ROW_ACTION = 2,
    ROW_CYCLE = 3
};

enum MenuItemId {
    ITEM_NONE = 0,
    /* Main submenu targets */
    ITEM_SUB_PLAYER,
    ITEM_SUB_WEAPONS,
    ITEM_SUB_VEHICLE,
    ITEM_SUB_SPAWN,
    ITEM_SUB_TELEPORT,
    ITEM_SUB_WORLD,
    /* Player options */
    ITEM_TOG_GODMODE,
    ITEM_TOG_NEVER_WANTED,
    ITEM_TOG_SUPER_JUMP,
    ITEM_TOG_FAST_RUN,
    ITEM_TOG_NO_RAGDOLL,
    ITEM_ACT_HEAL_ARMOR,
    ITEM_ACT_CLEAR_WANTED,
    /* Weapon options */
    ITEM_ACT_GIVE_WEAPONS,
    ITEM_TOG_INF_AMMO,
    ITEM_TOG_EXP_AMMO,
    ITEM_TOG_FIRE_AMMO,
    ITEM_TOG_EXP_MELEE,
    /* Vehicle options */
    ITEM_TOG_VEH_GOD,
    ITEM_TOG_HORN_BOOST,
    ITEM_ACT_FIX_VEH,
    ITEM_ACT_FLIP_VEH,
    ITEM_ACT_BOOST_VEH,
    ITEM_ACT_COLOR_VEH,
    ITEM_ACT_PLATE_VEH,
    /* Spawn / Teleport / World entries carry index in arg */
    ITEM_SUB_ADDON_SPAWN,
    ITEM_SPAWN_KEYBOARD,
    ITEM_ADDON_IDX,
    ITEM_SPAWN_IDX,
    ITEM_TP_WP,
    ITEM_TP_IDX,
    ITEM_CYC_DENSITY,
    ITEM_CYC_WEATHER,
    ITEM_CYC_TIME
};

typedef struct {
    char     label[24];
    char     badge[14];
    uint8_t  kind;
    uint8_t  id;
    uint8_t  arg;
    uint8_t  on;
} MenuRow;

typedef struct {
    char     name[20];
    uint32_t hash;
} SpawnVehEntry;

typedef struct {
    char  name[20];
    float x, y, z;
} TpEntry;

typedef struct {
    char name[12];
} WeatherEntry;

/* All static tables strictly avoid pointers so zero .data.rel.ro relocations emit */
static const SpawnVehEntry kSpawnVehicles[12] = {
    { "Adder",         0xB779A091U },
    { "Zentorno",      0xAC5DF515U },
    { "Entity XF",     0xB2FE5CF9U },
    { "Turismo R",     0x185484E1U },
    { "Elegy RH8",     0xDE3D9D22U },
    { "Dukes O'Death", 0xEC8F7094U },
    { "Bati 801",      0xF9300CC5U },
    { "Sanchez",       0x2EF89E46U },
    { "Rhino Tank",    0x2EA68690U },
    { "Buzzard",       0x2F03547BU },
    { "P-996 Lazer",   0xB39B0AE6U },
    { "Police Cruiser",0x71FA16EAU }
};

static const TpEntry kTeleports[6] = {
    { "Mount Chiliad",   499.0f,  5592.0f, 796.0f },
    { "LSIA Runway",   -1336.0f, -3044.0f,  14.0f },
    { "Maze Bank Roof",  -75.0f,  -818.9f, 326.2f },
    { "Fort Zancudo",  -2047.0f,  3132.0f,  33.0f },
    { "Sandy Airfield", 1747.0f,  3273.0f,  41.2f },
    { "Del Perro Pier",-1850.0f, -1232.0f,  13.0f }
};

static const WeatherEntry kWeathers[8] = {
    { "CLEAR" },
    { "EXTRASUNNY" },
    { "CLOUDS" },
    { "OVERCAST" },
    { "RAIN" },
    { "THUNDER" },
    { "FOGGY" },
    { "XMAS" }
};

static const uint32_t kWeaponHashes[15] = {
    0x1B06D571U, /* weapon_pistol */
    0x5EF9FEC4U, /* weapon_combatpistol */
    0x22D8FE39U, /* weapon_appistol */
    0x2BE6766BU, /* weapon_smg */
    0xBFEFFF6DU, /* weapon_assaultrifle */
    0x83BF0278U, /* weapon_carbinerifle */
    0xAF113F99U, /* weapon_advancedrifle */
    0x9D07F764U, /* weapon_mg */
    0x0C472FE2U, /* weapon_heavysniper */
    0xB1CA77B1U, /* weapon_rpg */
    0x42BF8A85U, /* weapon_minigun */
    0xA284510BU, /* weapon_grenadelauncher */
    0x2C3731D9U, /* weapon_stickybomb */
    0x99B507EAU, /* weapon_knife */
    0xFBAB5776U  /* gadget_parachute */
};

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
        __asm__ volatile("" ::: "memory");
    }
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

static inline void set_toast(OrbxNativeContext *oc, const char *msg) {
    copy_cstr(oc->menu.toast_msg, sizeof(oc->menu.toast_msg), msg);
    oc->menu.toast_ticks = 180u; /* ~3s at 60 FPS */
}

static inline void play_ui_sound(OrbxNativeContext *oc, const char *name) {
    nat4(oc, ORBX_NAT_PLAY_SOUND_FRONTEND, (uint64_t)(int64_t)-1,
         ptr2u(name), ptr2u("HUD_FRONTEND_DEFAULT_SOUNDSET"), 0);
}

static void ui_draw_rect(OrbxNativeContext *oc, float x, float y, float w, float h,
                         int r, int g, int b, int a) {
    uint64_t args[9] = {
        f2u(x), f2u(y), f2u(w), f2u(h),
        (uint64_t)r, (uint64_t)g, (uint64_t)b, (uint64_t)a, 0
    };
    nat_invoke(oc, ORBX_NAT_DRAW_RECT, args, 9, NULL);
}

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
    } else {
        nat2(oc, ORBX_NAT_SET_TEXT_WRAP, f2u(0.0f), f2u(1.0f));
    }
    nat1(oc, ORBX_NAT_BEGIN_TEXT_COMMAND_DISPLAY_TEXT, ptr2u("STRING"));
    nat1(oc, ORBX_NAT_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER, ptr2u(txt));
    nat3(oc, ORBX_NAT_END_TEXT_COMMAND_DISPLAY_TEXT, f2u(x), f2u(y), 0);
}

/* ============================================================================
 * Feature Actions & Continuous Per-Frame Mods
 * ============================================================================ */

static void act_heal_armor(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t max_hp = (int32_t)nat1(oc, ORBX_NAT_GET_ENTITY_MAX_HEALTH, (uint64_t)ped);
    if (max_hp < 200) max_hp = 200;
    nat3(oc, ORBX_NAT_SET_ENTITY_HEALTH, (uint64_t)ped, (uint64_t)max_hp, 0);
    nat2(oc, ORBX_NAT_SET_PED_ARMOUR, (uint64_t)ped, 100);
    set_toast(oc, "Health & Armor Restored");
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
    set_toast(oc, "Vehicle Repaired & Cleaned");
}

static void act_flip_vehicle(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    nat2(oc, ORBX_NAT_SET_VEHICLE_ON_GROUND_PROPERLY, (uint64_t)veh, f2u(5.0f));
    set_toast(oc, "Vehicle Placed Upright");
}

static void act_boost_vehicle(OrbxNativeContext *oc, float speed) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { if (speed > 60.0f) set_toast(oc, "Not in a vehicle"); return; }
    nat2(oc, ORBX_NAT_SET_VEHICLE_FORWARD_SPEED, (uint64_t)veh, f2u(speed));
}

static void act_color_vehicle(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    uint32_t x = oc->menu.color_rng * 1664525u + 1013904223u;
    oc->menu.color_rng = x ? x : 0x13572468u;
    int r1 = (int)(x & 0xffu), g1 = (int)((x >> 8) & 0xffu), b1 = (int)((x >> 16) & 0xffu);
    x = x * 1664525u + 1013904223u;
    int r2 = (int)(x & 0xffu), g2 = (int)((x >> 8) & 0xffu), b2 = (int)((x >> 16) & 0xffu);
    nat4(oc, ORBX_NAT_SET_VEHICLE_CUSTOM_PRIMARY_COLOUR, (uint64_t)veh, r1, g1, b1);
    nat4(oc, ORBX_NAT_SET_VEHICLE_CUSTOM_SECONDARY_COLOUR, (uint64_t)veh, r2, g2, b2);
    set_toast(oc, "Random Paint Applied");
}

static void act_plate_vehicle(OrbxNativeContext *oc) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    if (!veh) { set_toast(oc, "Not in a vehicle"); return; }
    nat2(oc, ORBX_NAT_SET_VEHICLE_NUMBER_PLATE_TEXT, (uint64_t)veh, ptr2u("MODHEN"));
    set_toast(oc, "Plate Set: MODHEN");
}

static void teleport_player_entity(OrbxNativeContext *oc, float x, float y, float z) {
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);
    int32_t ent = veh ? veh : ped;
    uint64_t a[7] = { (uint64_t)ent, f2u(x), f2u(y), f2u(z), 0, 0, 0 };
    nat_invoke(oc, ORBX_NAT_SET_ENTITY_COORDS_NO_OFFSET, a, 7, NULL);
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
        set_toast(oc, "Set a Waypoint on Map First");
        return;
    }
    int32_t blip = (int32_t)nat1(oc, ORBX_NAT_GET_FIRST_BLIP_INFO_ID, 8);
    if (!blip || !nat1(oc, ORBX_NAT_DOES_BLIP_EXIST, (uint64_t)blip)) {
        set_toast(oc, "Waypoint Blip Not Found");
        return;
    }
    Vec3 wp;
    zero_bytes(&wp, sizeof(wp));
    uint64_t ba[1] = { (uint64_t)blip };
    nat_invoke(oc, ORBX_NAT_GET_BLIP_COORDS, ba, 1, &wp);

    float gz = 140.0f;
    if (probe_ground_z(oc, wp.x, wp.y, &gz)) {
        teleport_player_entity(oc, wp.x, wp.y, gz);
        oc->menu.pending_tp_wp_ticks = 0;
        set_toast(oc, "Teleported to Waypoint");
    } else {
        /* Stream terrain at (wx, wy) over next few frames and snap cleanly onto ground */
        teleport_player_entity(oc, wp.x, wp.y, 160.0f);
        oc->menu.pending_tp_x = wp.x;
        oc->menu.pending_tp_y = wp.y;
        oc->menu.pending_tp_wp_ticks = 30u;
        set_toast(oc, "Streaming Waypoint Ground...");
    }
}

static void log_hash_event(OrbxNativeContext *oc, const char *prefix, uint32_t h) {
    char msg[96];
    size_t p = orbx_pic_fmt_str(msg, 0, sizeof(msg) - 1, prefix);
    p = orbx_pic_fmt_hex32(msg, p, sizeof(msg) - 1, h);
    msg[p] = '\0';
    orbx_pic_log_raw(oc, msg);
}

static void start_vehicle_spawn(OrbxNativeContext *oc, uint32_t model_hash) {
    if (!model_hash) return;
    if (oc->menu.natives[ORBX_NAT_IS_MODEL_IN_CDIMAGE] &&
        !nat1(oc, ORBX_NAT_IS_MODEL_IN_CDIMAGE, (uint64_t)model_hash)) {
        log_hash_event(oc, "[MENU] Spawn miss (not in cdimage) hash=", model_hash);
        set_toast(oc, "Model not found in game image");
        return;
    }
    if (oc->menu.natives[ORBX_NAT_IS_MODEL_VALID] &&
        !nat1(oc, ORBX_NAT_IS_MODEL_VALID, (uint64_t)model_hash)) {
        log_hash_event(oc, "[MENU] Spawn invalid model hash=", model_hash);
        set_toast(oc, "Invalid vehicle model");
        return;
    }
    log_hash_event(oc, "[MENU] Requesting vehicle model hash=", model_hash);
    nat1(oc, ORBX_NAT_REQUEST_MODEL, (uint64_t)model_hash);
    oc->menu.pending_spawn_hash  = model_hash;
    oc->menu.pending_spawn_ticks = 900u; /* ~15 s: large add-on cars stream slowly from the SSD */
}

static void tick_async_requests(OrbxNativeContext *oc) {
    if (oc->menu.pending_tp_wp_ticks > 0) {
        oc->menu.pending_tp_wp_ticks--;
        float gz = 0.0f;
        if (probe_ground_z(oc, oc->menu.pending_tp_x, oc->menu.pending_tp_y, &gz)) {
            teleport_player_entity(oc, oc->menu.pending_tp_x, oc->menu.pending_tp_y, gz);
            oc->menu.pending_tp_wp_ticks = 0;
            set_toast(oc, "Teleported to Waypoint");
        }
    }

    uint32_t mhash = oc->menu.pending_spawn_hash;
    if (!mhash) return;

    if (nat1(oc, ORBX_NAT_HAS_MODEL_LOADED, (uint64_t)mhash)) {
        log_hash_event(oc, "[MENU] Model has loaded! Spawning veh hash=", mhash);
        int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
        Vec3 pos;
        zero_bytes(&pos, sizeof(pos));
        uint64_t oa[4] = { (uint64_t)ped, f2u(0.0f), f2u(4.5f), f2u(0.6f) };
        nat_invoke(oc, ORBX_NAT_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, oa, 4, &pos);
        uint64_t hr = nat1(oc, ORBX_NAT_GET_ENTITY_HEADING, (uint64_t)ped);
        float heading = u2f(hr);

        uint64_t ca[8] = {
            (uint64_t)mhash, f2u(pos.x), f2u(pos.y), f2u(pos.z), f2u(heading), 0, 0, 0
        };
        int32_t veh = (int32_t)nat_invoke(oc, ORBX_NAT_CREATE_VEHICLE, ca, 8, NULL);
        log_hash_event(oc, "[MENU] CREATE_VEHICLE returned veh handle=", (uint32_t)veh);
        if (veh && nat1(oc, ORBX_NAT_DOES_ENTITY_EXIST, (uint64_t)veh)) {
            nat2(oc, ORBX_NAT_SET_VEHICLE_ON_GROUND_PROPERLY, (uint64_t)veh, f2u(5.0f));
            nat3(oc, ORBX_NAT_SET_PED_INTO_VEHICLE, (uint64_t)ped, (uint64_t)veh, (uint64_t)(int64_t)-1);
            nat2(oc, ORBX_NAT_SET_VEHICLE_NUMBER_PLATE_TEXT, (uint64_t)veh, ptr2u("MODHEN"));
            nat1(oc, ORBX_NAT_SET_ENTITY_AS_NO_LONGER_NEEDED, ptr2u(&veh));
            set_toast(oc, "Vehicle Spawned");
        } else {
            set_toast(oc, "CreateVehicle Failed");
        }
        nat1(oc, ORBX_NAT_SET_MODEL_AS_NO_LONGER_NEEDED, (uint64_t)mhash);
        oc->menu.pending_spawn_hash  = 0;
        oc->menu.pending_spawn_ticks = 0;
    } else if (oc->menu.pending_spawn_ticks > 0) {
        oc->menu.pending_spawn_ticks--;
        nat1(oc, ORBX_NAT_REQUEST_MODEL, (uint64_t)mhash);
        if (oc->menu.pending_spawn_ticks == 0) {
            nat1(oc, ORBX_NAT_SET_MODEL_AS_NO_LONGER_NEEDED, (uint64_t)mhash);
            log_hash_event(oc, "[MENU] Model load timeout (streaming never finished) hash=", mhash);
            oc->menu.pending_spawn_hash = 0;
            set_toast(oc, "Vehicle Model Load Timeout");
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
    case ORBX_ACT_BOOST_VEHICLE: act_boost_vehicle(oc, 85.0f); set_toast(oc, "Super Boost!"); break;
    case ORBX_ACT_COLOR_VEHICLE: act_color_vehicle(oc); break;
    case ORBX_ACT_TP_WAYPOINT:   act_tp_waypoint(oc); break;
    case ORBX_ACT_SET_TIME:
        if (arg0 >= 0 && arg0 <= 23) {
            nat3(oc, ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME, (uint64_t)arg0, 0, 0);
            set_toast(oc, "Clock Time Updated");
        }
        break;
    case ORBX_ACT_SET_WEATHER:
        if (arg0 >= 0 && arg0 < 8) {
            oc->menu.weather_idx = arg0;
            nat1(oc, ORBX_NAT_SET_WEATHER_TYPE_NOW_PERSIST, ptr2u(kWeathers[arg0].name));
            set_toast(oc, kWeathers[arg0].name);
        }
        break;
    case ORBX_ACT_SPAWN_VEH:
        start_vehicle_spawn(oc, shash);
        break;
    default:
        break;
    }
}

static void apply_continuous_features(OrbxNativeContext *oc) {
    int32_t pl  = (int32_t)nat0(oc, ORBX_NAT_PLAYER_ID);
    int32_t ped = (int32_t)nat0(oc, ORBX_NAT_PLAYER_PED_ID);
    int32_t veh = get_current_veh(oc, ped);

    if (oc->menu.godmode) {
        nat2(oc, ORBX_NAT_SET_PLAYER_INVINCIBLE, (uint64_t)pl, 1);
        nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)ped, 1);
        oc->menu.applied_flags |= APP_GOD;
    } else if (oc->menu.applied_flags & APP_GOD) {
        nat2(oc, ORBX_NAT_SET_PLAYER_INVINCIBLE, (uint64_t)pl, 0);
        nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)ped, 0);
        oc->menu.applied_flags &= ~APP_GOD;
    }

    if (oc->menu.never_wanted) {
        nat1(oc, ORBX_NAT_CLEAR_PLAYER_WANTED_LEVEL, (uint64_t)pl);
        nat1(oc, ORBX_NAT_SET_MAX_WANTED_LEVEL, 0);
        nat2(oc, ORBX_NAT_SET_POLICE_IGNORE_PLAYER, (uint64_t)pl, 1);
        oc->menu.applied_flags |= APP_WANTED;
    } else if (oc->menu.applied_flags & APP_WANTED) {
        nat1(oc, ORBX_NAT_SET_MAX_WANTED_LEVEL, 5);
        nat2(oc, ORBX_NAT_SET_POLICE_IGNORE_PLAYER, (uint64_t)pl, 0);
        oc->menu.applied_flags &= ~APP_WANTED;
    }

    if (oc->menu.super_jump) {
        nat1(oc, ORBX_NAT_SET_SUPER_JUMP_THIS_FRAME, (uint64_t)pl);
    }

    if (oc->menu.fast_run) {
        nat2(oc, ORBX_NAT_SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(1.49f));
        nat2(oc, ORBX_NAT_SET_SWIM_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(1.49f));
        oc->menu.applied_flags |= APP_FASTRUN;
    } else if (oc->menu.applied_flags & APP_FASTRUN) {
        nat2(oc, ORBX_NAT_SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(1.0f));
        nat2(oc, ORBX_NAT_SET_SWIM_MULTIPLIER_FOR_PLAYER, (uint64_t)pl, f2u(1.0f));
        oc->menu.applied_flags &= ~APP_FASTRUN;
    }

    if (oc->menu.no_ragdoll) {
        nat2(oc, ORBX_NAT_SET_PED_CAN_RAGDOLL, (uint64_t)ped, 0);
        oc->menu.applied_flags |= APP_NORAGDOLL;
    } else if (oc->menu.applied_flags & APP_NORAGDOLL) {
        nat2(oc, ORBX_NAT_SET_PED_CAN_RAGDOLL, (uint64_t)ped, 1);
        oc->menu.applied_flags &= ~APP_NORAGDOLL;
    }

    if (oc->menu.inf_ammo) {
        nat2(oc, ORBX_NAT_SET_PED_INFINITE_AMMO_CLIP, (uint64_t)ped, 1);
        oc->menu.applied_flags |= APP_INF_AMMO;
    } else if (oc->menu.applied_flags & APP_INF_AMMO) {
        nat2(oc, ORBX_NAT_SET_PED_INFINITE_AMMO_CLIP, (uint64_t)ped, 0);
        oc->menu.applied_flags &= ~APP_INF_AMMO;
    }

    if (oc->menu.explosive_ammo)  nat1(oc, ORBX_NAT_SET_EXPLOSIVE_AMMO_THIS_FRAME, (uint64_t)pl);
    if (oc->menu.fire_ammo)       nat1(oc, ORBX_NAT_SET_FIRE_AMMO_THIS_FRAME, (uint64_t)pl);
    if (oc->menu.explosive_melee) nat1(oc, ORBX_NAT_SET_EXPLOSIVE_MELEE_THIS_FRAME, (uint64_t)pl);

    if (veh) {
        if (oc->menu.veh_god) {
            nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)veh, 1);
            oc->menu.applied_flags |= APP_VEH_GOD;
        } else if (oc->menu.applied_flags & APP_VEH_GOD) {
            nat2(oc, ORBX_NAT_SET_ENTITY_INVINCIBLE, (uint64_t)veh, 0);
            oc->menu.applied_flags &= ~APP_VEH_GOD;
        }
        if (oc->menu.horn_boost) {
            if ((oc->menu.pad_buttons_live & SCE_PAD_BTN_L3) ||
                nat2(oc, ORBX_NAT_IS_CONTROL_PRESSED, CTRL_PLAYER, IN_VEH_HORN) ||
                nat2(oc, ORBX_NAT_IS_DISABLED_CONTROL_PRESSED, CTRL_PLAYER, IN_VEH_HORN)) {
                nat2(oc, ORBX_NAT_SET_VEHICLE_FORWARD_SPEED, (uint64_t)veh, f2u(55.0f));
            }
        }
    }

    if (oc->menu.density_pct < 100u) {
        float mul = (float)oc->menu.density_pct * 0.01f;
        nat1(oc, ORBX_NAT_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME, f2u(mul));
        nat1(oc, ORBX_NAT_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, f2u(mul));
    }

    if (oc->menu.lock_hour >= 0 && oc->menu.lock_hour <= 23) {
        nat3(oc, ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME, (uint64_t)oc->menu.lock_hour, 0, 0);
    }
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

static uint32_t poll_button_mask(OrbxNativeContext *oc) {
    if (oc->menu.pad_calls > 0) {
        uint32_t latch = __atomic_exchange_n(&oc->menu.pad_buttons_latch, 0u, __ATOMIC_ACQ_REL);
        uint32_t raw   = __atomic_load_n(&oc->menu.pad_buttons_live, __ATOMIC_ACQUIRE) | latch;
        uint32_t m = 0;
        if (raw & SCE_PAD_BTN_R1)     m |= BTN_RB;
        if (raw & SCE_PAD_BTN_L1)     m |= BTN_LB;
        if (raw & SCE_PAD_BTN_UP)     m |= BTN_UP;
        if (raw & SCE_PAD_BTN_DOWN)   m |= BTN_DOWN;
        if (raw & SCE_PAD_BTN_LEFT)   m |= BTN_LEFT;
        if (raw & SCE_PAD_BTN_RIGHT)  m |= BTN_RIGHT;
        if (raw & SCE_PAD_BTN_CROSS)  m |= BTN_ACCEPT;
        if (raw & SCE_PAD_BTN_CIRCLE) m |= BTN_CANCEL;
        return m;
    }

    uint32_t m = 0;
    if (btn_down(oc, IN_FE_RB))                             m |= BTN_RB;
    if (btn_down(oc, IN_FE_LB))                             m |= BTN_LB;
    if (btn_down(oc, IN_SPAD_UP)    || btn_down(oc, IN_FE_UP))    m |= BTN_UP;
    if (btn_down(oc, IN_SPAD_DOWN)  || btn_down(oc, IN_FE_DOWN))  m |= BTN_DOWN;
    if (btn_down(oc, IN_SPAD_LEFT)  || btn_down(oc, IN_FE_LEFT))  m |= BTN_LEFT;
    if (btn_down(oc, IN_SPAD_RIGHT) || btn_down(oc, IN_FE_RIGHT)) m |= BTN_RIGHT;
    if (btn_down(oc, IN_FE_ACCEPT))                         m |= BTN_ACCEPT;
    if (btn_down(oc, IN_FE_CANCEL))                         m |= BTN_CANCEL;
    return m;
}

static uint32_t compute_triggered_mask(OrbxNativeContext *oc, uint32_t now_ms, uint32_t cur_mask) {
    uint32_t edge = cur_mask & ~oc->menu.prev_btn_mask;
    oc->menu.prev_btn_mask = cur_mask;

    uint32_t rep_dirs = cur_mask & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
    uint32_t trig = edge;

    if (rep_dirs == 0) {
        oc->menu.hold_btn_mask = 0;
    } else if (rep_dirs != oc->menu.hold_btn_mask) {
        oc->menu.hold_btn_mask = rep_dirs;
        oc->menu.hold_start_ms = now_ms;
        oc->menu.hold_repeat_ms = now_ms;
    } else if (now_ms - oc->menu.hold_start_ms >= 280u &&
               now_ms - oc->menu.hold_repeat_ms >= 90u) {
        oc->menu.hold_repeat_ms = now_ms;
        trig |= rep_dirs;
    }
    return trig;
}

static void disable_game_controls_while_open(OrbxNativeContext *oc) {
    static const int kDisControls[14] = {
        IN_PHONE, IN_CHAR_WHEEL, IN_SPRINT, IN_JUMP, IN_COVER, IN_RELOAD,
        IN_VEH_CIN_CAM, IN_VEH_NEXT_WPN, IN_VEH_PREV_WPN,
        IN_FE_ACCEPT, IN_FE_CANCEL, IN_SPAD_UP, IN_SPAD_DOWN, IN_SPAD_LEFT
    };
    for (size_t i = 0; i < (sizeof(kDisControls) / sizeof(kDisControls[0])); i++) {
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, (uint64_t)kDisControls[i], 1);
        nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_FRONTEND, (uint64_t)kDisControls[i], 1);
    }
    nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_PLAYER, IN_SPAD_RIGHT, 1);
    nat3(oc, ORBX_NAT_DISABLE_CONTROL_ACTION, CTRL_FRONTEND, IN_SPAD_RIGHT, 1);
}

static void add_row(MenuRow *rows, int *cnt, int max_rows,
                    const char *label, const char *badge,
                    uint8_t kind, uint8_t id, uint8_t arg, uint8_t on) {
    if (*cnt >= max_rows) return;
    MenuRow *r = &rows[(*cnt)++];
    copy_cstr(r->label, sizeof(r->label), label);
    copy_cstr(r->badge, sizeof(r->badge), badge);
    r->kind = kind;
    r->id   = id;
    r->arg  = arg;
    r->on   = on;
}

static void add_tog(MenuRow *rows, int *cnt, const char *label, uint8_t id, uint32_t val) {
    add_row(rows, cnt, MAX_MENU_ROWS, label, val ? "ON" : "OFF",
            ROW_TOGGLE, id, 0, val ? 1u : 0u);
}

static void build_submenu_rows(OrbxNativeContext *oc, uint32_t sub,
                               MenuRow *rows, int *out_cnt, const char **out_title) {
    int n = 0;
    switch (sub) {
    case ORBX_SUB_PLAYER:
        *out_title = "SELF OPTIONS";
        add_tog(rows, &n, "God Mode",            ITEM_TOG_GODMODE,      oc->menu.godmode);
        add_tog(rows, &n, "Never Wanted",        ITEM_TOG_NEVER_WANTED, oc->menu.never_wanted);
        add_tog(rows, &n, "Super Jump",          ITEM_TOG_SUPER_JUMP,   oc->menu.super_jump);
        add_tog(rows, &n, "Fast Run & Swim",     ITEM_TOG_FAST_RUN,     oc->menu.fast_run);
        add_tog(rows, &n, "No Ragdoll",          ITEM_TOG_NO_RAGDOLL,   oc->menu.no_ragdoll);
        add_row(rows, &n, MAX_MENU_ROWS, "Heal & Full Armor",  "RUN", ROW_ACTION, ITEM_ACT_HEAL_ARMOR,  0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Clear Wanted Level", "RUN", ROW_ACTION, ITEM_ACT_CLEAR_WANTED, 0, 0);
        break;
    case ORBX_SUB_WEAPONS:
        *out_title = "WEAPON OPTIONS";
        add_row(rows, &n, MAX_MENU_ROWS, "Give All Weapons",   "RUN", ROW_ACTION, ITEM_ACT_GIVE_WEAPONS, 0, 0);
        add_tog(rows, &n, "Infinite Clip Ammo",  ITEM_TOG_INF_AMMO,  oc->menu.inf_ammo);
        add_tog(rows, &n, "Explosive Ammo",      ITEM_TOG_EXP_AMMO,  oc->menu.explosive_ammo);
        add_tog(rows, &n, "Fire Ammo",           ITEM_TOG_FIRE_AMMO, oc->menu.fire_ammo);
        add_tog(rows, &n, "Explosive Melee",     ITEM_TOG_EXP_MELEE, oc->menu.explosive_melee);
        break;
    case ORBX_SUB_VEHICLE:
        *out_title = "VEHICLE OPTIONS";
        add_tog(rows, &n, "Vehicle God Mode",    ITEM_TOG_VEH_GOD,    oc->menu.veh_god);
        add_tog(rows, &n, "Horn Boost (L3)",     ITEM_TOG_HORN_BOOST, oc->menu.horn_boost);
        add_row(rows, &n, MAX_MENU_ROWS, "Repair & Clean",     "RUN", ROW_ACTION, ITEM_ACT_FIX_VEH,   0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Flip Upright",       "RUN", ROW_ACTION, ITEM_ACT_FLIP_VEH,  0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Super Boost Forward","RUN", ROW_ACTION, ITEM_ACT_BOOST_VEH, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Random Paint",       "RUN", ROW_ACTION, ITEM_ACT_COLOR_VEH, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "MODHEN Plate",       "RUN", ROW_ACTION, ITEM_ACT_PLATE_VEH, 0, 0);
        break;
    case ORBX_SUB_SPAWN:
        *out_title = "VEHICLE SPAWNER";
        add_row(rows, &n, MAX_MENU_ROWS, "Type Model Name...", "TYPE", ROW_ACTION, ITEM_SPAWN_KEYBOARD, 0, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "Add-On DLC Cars",    ">",    ROW_SUB,    ITEM_SUB_ADDON_SPAWN, ORBX_SUB_ADDON_SPAWN, 0);
        for (size_t i = 0; i < (sizeof(kSpawnVehicles) / sizeof(kSpawnVehicles[0])); i++) {
            add_row(rows, &n, MAX_MENU_ROWS, kSpawnVehicles[i].name, "SPAWN",
                    ROW_ACTION, ITEM_SPAWN_IDX, (uint8_t)i, 0);
        }
        break;
    case ORBX_SUB_ADDON_SPAWN: {
        *out_title = "ADD-ON DLC CARS";
        add_row(rows, &n, MAX_MENU_ROWS, "Type Model Name...", "TYPE", ROW_ACTION, ITEM_SPAWN_KEYBOARD, 0, 0);
        uint32_t cnt = oc->menu.addon_car_count;
        if (cnt > ORBX_MAX_ADDON_CARS) cnt = ORBX_MAX_ADDON_CARS;
        for (uint32_t i = 0; i < cnt; i++) {
            const char *lbl = oc->menu.addon_cars[i].label[0] ? oc->menu.addon_cars[i].label : oc->menu.addon_cars[i].name;
            const char *bdg = oc->menu.addon_cars[i].name[0]  ? oc->menu.addon_cars[i].name  : "SPAWN";
            add_row(rows, &n, MAX_MENU_ROWS, lbl, bdg, ROW_ACTION, ITEM_ADDON_IDX, (uint8_t)i, 0);
        }
        break;
    }
    case ORBX_SUB_TELEPORT:
        *out_title = "TELEPORT";
        add_row(rows, &n, MAX_MENU_ROWS, "To Map Waypoint", "TP", ROW_ACTION, ITEM_TP_WP, 0, 0);
        for (size_t i = 0; i < (sizeof(kTeleports) / sizeof(kTeleports[0])); i++) {
            add_row(rows, &n, MAX_MENU_ROWS, kTeleports[i].name, "TP",
                    ROW_ACTION, ITEM_TP_IDX, (uint8_t)i, 0);
        }
        break;
    case ORBX_SUB_WORLD: {
        *out_title = "WORLD & FPS";
        char buf[16];
        size_t pos = orbx_pic_fmt_u64(buf, 0, sizeof(buf) - 2, oc->menu.density_pct);
        buf[pos++] = '%';
        buf[pos] = '\0';
        add_row(rows, &n, MAX_MENU_ROWS, "Traffic / Ped", buf, ROW_CYCLE, ITEM_CYC_DENSITY, 0, 0);

        int32_t wi = oc->menu.weather_idx;
        if (wi < 0 || wi >= 8) wi = 0;
        add_row(rows, &n, MAX_MENU_ROWS, "Weather", kWeathers[wi].name, ROW_CYCLE, ITEM_CYC_WEATHER, 0, 0);

        if (oc->menu.lock_hour < 0) {
            copy_cstr(buf, sizeof(buf), "NORMAL");
        } else {
            pos = orbx_pic_fmt_u64(buf, 0, sizeof(buf) - 4, (uint64_t)oc->menu.lock_hour);
            pos = orbx_pic_fmt_str(buf, pos, sizeof(buf) - 1, ":00");
            buf[pos] = '\0';
        }
        add_row(rows, &n, MAX_MENU_ROWS, "Clock Time", buf, ROW_CYCLE, ITEM_CYC_TIME, 0, 0);
        break;
    }
    case ORBX_SUB_MAIN:
    default:
        *out_title = "MAIN MENU";
        add_row(rows, &n, MAX_MENU_ROWS, "Self Options",    ">", ROW_SUB, ITEM_SUB_PLAYER,   ORBX_SUB_PLAYER,   0);
        add_row(rows, &n, MAX_MENU_ROWS, "Weapon Options",  ">", ROW_SUB, ITEM_SUB_WEAPONS,  ORBX_SUB_WEAPONS,  0);
        add_row(rows, &n, MAX_MENU_ROWS, "Vehicle Options", ">", ROW_SUB, ITEM_SUB_VEHICLE,  ORBX_SUB_VEHICLE,  0);
        add_row(rows, &n, MAX_MENU_ROWS, "Vehicle Spawner", ">", ROW_SUB, ITEM_SUB_SPAWN,    ORBX_SUB_SPAWN,    0);
        add_row(rows, &n, MAX_MENU_ROWS, "Teleport",        ">", ROW_SUB, ITEM_SUB_TELEPORT, ORBX_SUB_TELEPORT, 0);
        add_row(rows, &n, MAX_MENU_ROWS, "World & FPS",     ">", ROW_SUB, ITEM_SUB_WORLD,    ORBX_SUB_WORLD,    0);
        break;
    }
    *out_cnt = n;
}

static void execute_row(OrbxNativeContext *oc, const MenuRow *r, int dir) {
    switch (r->id) {
    case ITEM_SUB_PLAYER:
    case ITEM_SUB_WEAPONS:
    case ITEM_SUB_VEHICLE:
    case ITEM_SUB_SPAWN:
    case ITEM_SUB_ADDON_SPAWN:
    case ITEM_SUB_TELEPORT:
    case ITEM_SUB_WORLD:
        oc->menu.submenu = r->arg;
        break;
    case ITEM_TOG_GODMODE:      oc->menu.godmode ^= 1u; break;
    case ITEM_TOG_NEVER_WANTED: oc->menu.never_wanted ^= 1u; break;
    case ITEM_TOG_SUPER_JUMP:   oc->menu.super_jump ^= 1u; break;
    case ITEM_TOG_FAST_RUN:     oc->menu.fast_run ^= 1u; break;
    case ITEM_TOG_NO_RAGDOLL:   oc->menu.no_ragdoll ^= 1u; break;
    case ITEM_TOG_INF_AMMO:     oc->menu.inf_ammo ^= 1u; break;
    case ITEM_TOG_EXP_AMMO:     oc->menu.explosive_ammo ^= 1u; break;
    case ITEM_TOG_FIRE_AMMO:    oc->menu.fire_ammo ^= 1u; break;
    case ITEM_TOG_EXP_MELEE:    oc->menu.explosive_melee ^= 1u; break;
    case ITEM_TOG_VEH_GOD:      oc->menu.veh_god ^= 1u; break;
    case ITEM_TOG_HORN_BOOST:   oc->menu.horn_boost ^= 1u; break;
    case ITEM_ACT_HEAL_ARMOR:   act_heal_armor(oc); break;
    case ITEM_ACT_CLEAR_WANTED: act_clear_wanted(oc); break;
    case ITEM_ACT_GIVE_WEAPONS: act_give_weapons(oc); break;
    case ITEM_ACT_FIX_VEH:      act_fix_vehicle(oc); break;
    case ITEM_ACT_FLIP_VEH:     act_flip_vehicle(oc); break;
    case ITEM_ACT_BOOST_VEH:    act_boost_vehicle(oc, 85.0f); break;
    case ITEM_ACT_COLOR_VEH:    act_color_vehicle(oc); break;
    case ITEM_ACT_PLATE_VEH:    act_plate_vehicle(oc); break;
    case ITEM_SPAWN_KEYBOARD:
        if (oc->menu.natives[ORBX_NAT_DISPLAY_ONSCREEN_KEYBOARD] &&
            oc->menu.natives[ORBX_NAT_UPDATE_ONSCREEN_KEYBOARD] &&
            oc->menu.natives[ORBX_NAT_GET_ONSCREEN_KEYBOARD_RESULT]) {
            uint64_t ka[8] = {
                0, ptr2u("FMMC_KEY_TIP8"), ptr2u(""), ptr2u(""),
                ptr2u(""), ptr2u(""), ptr2u(""), 32u
            };
            nat_invoke(oc, ORBX_NAT_DISPLAY_ONSCREEN_KEYBOARD, ka, 8, NULL);
            oc->menu.kb_active = 1u;
        } else {
            set_toast(oc, "Onscreen keyboard native unavailable");
        }
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
    case ITEM_SPAWN_IDX:
        if (r->arg < (uint8_t)(sizeof(kSpawnVehicles) / sizeof(kSpawnVehicles[0]))) {
            start_vehicle_spawn(oc, kSpawnVehicles[r->arg].hash);
        }
        break;
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
    case ITEM_CYC_DENSITY: {
        int pct = (int)oc->menu.density_pct + (dir < 0 ? -25 : 25);
        if (pct > 100) pct = 0;
        if (pct < 0)   pct = 100;
        oc->menu.density_pct = (uint32_t)pct;
        break;
    }
    case ITEM_CYC_WEATHER: {
        int wi = (int)oc->menu.weather_idx + (dir < 0 ? -1 : 1);
        if (wi >= 8) wi = 0;
        if (wi < 0)  wi = 7;
        oc->menu.weather_idx = wi;
        nat1(oc, ORBX_NAT_SET_WEATHER_TYPE_NOW_PERSIST, ptr2u(kWeathers[wi].name));
        break;
    }
    case ITEM_CYC_TIME: {
        static const int32_t kHours[5] = { -1, 12, 19, 0, 6 };
        int cur = 0;
        for (int i = 0; i < 5; i++) {
            if (oc->menu.lock_hour == kHours[i]) { cur = i; break; }
        }
        cur = (cur + (dir < 0 ? 4 : 1)) % 5;
        oc->menu.lock_hour = kHours[cur];
        if (oc->menu.lock_hour >= 0) {
            nat3(oc, ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME, (uint64_t)oc->menu.lock_hour, 0, 0);
        }
        break;
    }
    default:
        break;
    }
}

/* ============================================================================
 * HUD Drawing
 * ============================================================================ */

static void draw_menu_hud(OrbxNativeContext *oc, const char *subtitle,
                          const MenuRow *rows, int row_count, int cur) {
    const float menu_x = 0.835f;
    const float width  = 0.230f;
    const float left_x = menu_x - width * 0.5f + 0.010f;
    const float right_x = menu_x + width * 0.5f - 0.010f;

    /* 1. Header Banner */
    const float hdr_y = 0.115f;
    const float hdr_h = 0.068f;
    ui_draw_rect(oc, menu_x, hdr_y, width, hdr_h, 14, 92, 200, 235);
    ui_draw_rect(oc, menu_x, hdr_y + hdr_h * 0.5f - 0.0015f, width, 0.003f, 80, 210, 255, 255);
    ui_draw_text(oc, "modHEN MENU", menu_x, hdr_y - 0.021f, 0.58f, 1, 1, 1.0f, 255, 255, 255, 255);

    /* 2. Subtitle + Counter Bar */
    const float sub_y = hdr_y + hdr_h * 0.5f + 0.015f;
    const float sub_h = 0.030f;
    ui_draw_rect(oc, menu_x, sub_y, width, sub_h, 10, 14, 22, 240);
    ui_draw_text(oc, subtitle, left_x, sub_y - 0.011f, 0.30f, 0, 0, 1.0f, 110, 210, 255, 255);

    char cnt_buf[24];
    size_t pos = orbx_pic_fmt_u64(cnt_buf, 0, sizeof(cnt_buf) - 1, (uint64_t)(cur + 1));
    pos = orbx_pic_fmt_str(cnt_buf, pos, sizeof(cnt_buf) - 1, "/");
    pos = orbx_pic_fmt_u64(cnt_buf, pos, sizeof(cnt_buf) - 1, (uint64_t)row_count);
    if (oc->menu.fps_x10 >= 10u) {
        pos = orbx_pic_fmt_str(cnt_buf, pos, sizeof(cnt_buf) - 1, " | ");
        pos = orbx_pic_fmt_u64(cnt_buf, pos, sizeof(cnt_buf) - 1, oc->menu.fps_x10 / 10u);
        pos = orbx_pic_fmt_str(cnt_buf, pos, sizeof(cnt_buf) - 1, "FPS");
    }
    cnt_buf[pos] = '\0';
    ui_draw_text(oc, cnt_buf, right_x, sub_y - 0.011f, 0.28f, 0, 2, right_x, 200, 210, 230, 255);

    /* 3. Option Rows (scrolling window of up to MAX_VISIBLE_ROWS) */
    int vis = (row_count < MAX_VISIBLE_ROWS) ? row_count : MAX_VISIBLE_ROWS;
    int start = 0;
    if (cur >= MAX_VISIBLE_ROWS) {
        start = cur - MAX_VISIBLE_ROWS + 1;
    }

    const float row_h = 0.032f;
    const float y0 = sub_y + sub_h * 0.5f + row_h * 0.5f;

    for (int i = 0; i < vis; i++) {
        int idx = start + i;
        const MenuRow *r = &rows[idx];
        float ry = y0 + (float)i * row_h;
        int sel = (idx == cur);

        if (sel) {
            ui_draw_rect(oc, menu_x, ry, width, row_h, 240, 246, 255, 240);
            ui_draw_rect(oc, left_x - 0.008f, ry, 0.0035f, row_h, 30, 145, 255, 255);
        } else {
            ui_draw_rect(oc, menu_x, ry, width, row_h, 14, 17, 25, 205);
        }

        int tr = sel ? 15 : 235;
        int tg = sel ? 20 : 240;
        int tb = sel ? 30 : 248;
        ui_draw_text(oc, r->label, left_x, ry - 0.0115f, 0.32f, 0, 0, 1.0f, tr, tg, tb, 255);

        int br = tr, bg = tg, bb = tb;
        if (r->kind == ROW_TOGGLE) {
            if (r->on) { br = sel ? 16 : 90; bg = sel ? 140 : 235; bb = sel ? 60 : 120; }
            else       { br = sel ? 130 : 155; bg = sel ? 45 : 165; bb = sel ? 45 : 175; }
        } else if (r->kind == ROW_CYCLE) {
            br = sel ? 18 : 110; bg = sel ? 105 : 205; bb = 255;
        }
        ui_draw_text(oc, r->badge, right_x, ry - 0.011f, 0.29f, 0, 2, right_x, br, bg, bb, 255);
    }

    /* 4. Footer Bar */
    float foot_y = y0 + (float)vis * row_h;
    ui_draw_rect(oc, menu_x, foot_y, width, 0.026f, 10, 14, 22, 235);
    ui_draw_text(oc, "X Select   O Back   R1+Left Close", menu_x, foot_y - 0.010f,
                 0.26f, 0, 1, 1.0f, 170, 195, 225, 255);
}

static void draw_toast_if_active(OrbxNativeContext *oc) {
    if (oc->menu.toast_ticks > 0) {
        oc->menu.toast_ticks--;
        ui_draw_rect(oc, 0.500f, 0.055f, 0.280f, 0.036f, 14, 20, 32, 220);
        ui_draw_rect(oc, 0.500f, 0.072f, 0.280f, 0.0025f, 45, 175, 255, 255);
        ui_draw_text(oc, oc->menu.toast_msg, 0.500f, 0.043f, 0.31f, 0, 1, 1.0f, 240, 248, 255, 255);
    } else if (!oc->menu.banner_ticks) {
        oc->menu.banner_ticks = 1u;
        set_toast(oc, "modHEN Menu Ready  (Press R1 + D-Pad Left)");
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

/* ============================================================================
 * Main Script-Thread Tick Entry (Called via WAIT native handler slot)
 * ============================================================================ */

static void menu_tick(OrbxNativeContext *oc) {
    __atomic_add_fetch(&oc->menu.main_ticks, 1u, __ATOMIC_RELAXED);
    uint32_t now_ms = (uint32_t)nat0(oc, ORBX_NAT_GET_GAME_TIMER);
    oc->menu.last_timer = now_ms;
    update_fps_meter(oc, now_ms);

    uint32_t cur_mask = poll_button_mask(oc);
    uint32_t trig     = compute_triggered_mask(oc, now_ms, cur_mask);

    /* Open / close on R1 + D-Pad Left (either edge order) */
    if (((cur_mask & BTN_RB) && (trig & BTN_LEFT)) ||
        ((cur_mask & BTN_LEFT) && (trig & BTN_RB))) {
        oc->menu.menu_open ^= 1u;
        play_ui_sound(oc, oc->menu.menu_open ? "SELECT" : "BACK");
    }

    process_web_action(oc);
    apply_continuous_features(oc);
    tick_async_requests(oc);
    draw_toast_if_active(oc);

    if (oc->menu.kb_active) {
        int32_t kb_st = (int32_t)nat0(oc, ORBX_NAT_UPDATE_ONSCREEN_KEYBOARD);
        if (kb_st == 0) {
            return;
        }
        oc->menu.kb_active = 0u;
        oc->menu.prev_btn_mask = poll_button_mask(oc);
        if (kb_st == 1) {
            const char *typed = (const char *)(uintptr_t)nat0(oc, ORBX_NAT_GET_ONSCREEN_KEYBOARD_RESULT);
            char clean[36];
            size_t ci = 0;
            if (typed) {
                const char *s = typed;
                while (*s == ' ' || *s == '\t') s++;
                while (*s && *s != ' ' && *s != '\r' && *s != '\n' && ci + 1 < sizeof(clean)) {
                    clean[ci++] = *s++;
                }
            }
            clean[ci] = '\0';
            if (clean[0] != '\0') {
                uint32_t h = orbx_pic_joaat(clean);
                /* Add dynamically to in-game addon_cars list if not already present */
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
            set_toast(oc, "Spawn cancelled");
        }
        return;
    }

    if (!oc->menu.menu_open) return;

    disable_game_controls_while_open(oc);

    MenuRow rows[MAX_MENU_ROWS];
    int row_count = 0;
    const char *title = "MAIN MENU";
    uint32_t sub = oc->menu.submenu;
    if (sub >= ORBX_SUB_MAX) sub = ORBX_SUB_MAIN;
    build_submenu_rows(oc, sub, rows, &row_count, &title);
    if (row_count <= 0) return;

    int32_t cur = oc->menu.cursor[sub];
    if (cur < 0) cur = 0;
    if (cur >= row_count) cur = row_count - 1;

    if (trig & BTN_UP) {
        cur = (cur - 1 + row_count) % row_count;
        play_ui_sound(oc, "NAV_UP_DOWN");
    } else if (trig & BTN_DOWN) {
        cur = (cur + 1) % row_count;
        play_ui_sound(oc, "NAV_UP_DOWN");
    } else if (trig & BTN_CANCEL) {
        play_ui_sound(oc, "BACK");
        if (sub == ORBX_SUB_MAIN) {
            oc->menu.menu_open = 0;
        } else if (sub == ORBX_SUB_ADDON_SPAWN) {
            oc->menu.submenu = ORBX_SUB_SPAWN;
        } else {
            oc->menu.submenu = ORBX_SUB_MAIN;
        }
    } else if (trig & (BTN_ACCEPT | BTN_LEFT | BTN_RIGHT)) {
        const MenuRow *sel = &rows[cur];
        if (sel->kind == ROW_CYCLE && (trig & (BTN_LEFT | BTN_RIGHT | BTN_ACCEPT))) {
            int dir = (trig & BTN_LEFT) ? -1 : 1;
            play_ui_sound(oc, "NAV_LEFT_RIGHT");
            execute_row(oc, sel, dir);
            build_submenu_rows(oc, oc->menu.submenu, rows, &row_count, &title);
        } else if (trig & BTN_ACCEPT) {
            play_ui_sound(oc, "SELECT");
            execute_row(oc, sel, 1);
            if (oc->menu.submenu == sub) {
                build_submenu_rows(oc, sub, rows, &row_count, &title);
            } else {
                sub = oc->menu.submenu;
                build_submenu_rows(oc, sub, rows, &row_count, &title);
                cur = oc->menu.cursor[sub];
                if (cur < 0 || cur >= row_count) cur = 0;
            }
        }
    }

    oc->menu.cursor[sub] = cur;
    if (oc->menu.menu_open) {
        draw_menu_hud(oc, title, rows, row_count, cur);
    }
}

__attribute__((used, visibility("hidden")))
void hook_menu_wait(void *nat_ctx_ptr) {
    OrbxNativeContext *oc = orbx_pic_get_ctx();
    if (!oc) return;
    uint64_t wait_addr = oc->menu.natives[ORBX_NAT_WAIT];
    if (!wait_addr) return;

    __atomic_add_fetch(&oc->menu.wait_calls, 1u, __ATOMIC_RELAXED);

    if (oc->menu.armed && oc->menu.natives[ORBX_NAT_GET_HASH_OF_THIS_SCRIPT_NAME]) {
        uint32_t sh = (uint32_t)nat0(oc, ORBX_NAT_GET_HASH_OF_THIS_SCRIPT_NAME);
        oc->menu.last_script_hash = sh;
        if (sh == SCR_HASH_MAIN_PERSISTENT || sh == SCR_HASH_FREEMODE) {
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
        }
    }

    ((GtaNativeFn)(uintptr_t)wait_addr)((GtaNativeCtx *)nat_ctx_ptr);
}
