#ifndef ORBX_MENU_ABI_H
#define ORBX_MENU_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Single source of truth for GTA V v1.00 (CUSA00419 / b323) runtime natives
 * resolved via s_NativeRegistrations @ eboot_base + 0x3244a28.
 * Format: X(ENUM_ID, B323_HASH_U64, REQUIRED_FLAG, NAME_STR)
 */
#define ORBX_MENU_NATIVE_LIST(X) \
    X(ORBX_NAT_WAIT,                                   0x4EDE34FBADD967A6ULL, 1, "WAIT") \
    X(ORBX_NAT_GET_HASH_OF_THIS_SCRIPT_NAME,           0x8A1C8B1738FFE87EULL, 1, "GET_HASH_OF_THIS_SCRIPT_NAME") \
    X(ORBX_NAT_GET_THIS_SCRIPT_NAME,                   0x442E0A7EDE4A738AULL, 0, "GET_THIS_SCRIPT_NAME") \
    X(ORBX_NAT_GET_GAME_TIMER,                         0x9CD27B0045628463ULL, 1, "GET_GAME_TIMER") \
    X(ORBX_NAT_DRAW_RECT,                              0x3A618A217E5154F0ULL, 1, "DRAW_RECT") \
    X(ORBX_NAT_SET_TEXT_FONT,                          0x66E0276CC5F6B9DAULL, 1, "SET_TEXT_FONT") \
    X(ORBX_NAT_SET_TEXT_SCALE,                         0x07C837F9A01C34C9ULL, 1, "SET_TEXT_SCALE") \
    X(ORBX_NAT_SET_TEXT_COLOUR,                        0xBE6B23FFA53FB442ULL, 1, "SET_TEXT_COLOUR") \
    X(ORBX_NAT_SET_TEXT_WRAP,                          0x63145D9C883A1A70ULL, 1, "SET_TEXT_WRAP") \
    X(ORBX_NAT_SET_TEXT_CENTRE,                        0xC02F4DBFB51D988BULL, 1, "SET_TEXT_CENTRE") \
    X(ORBX_NAT_SET_TEXT_RIGHT_JUSTIFY,                 0x6B3C4650BC8BEE47ULL, 1, "SET_TEXT_RIGHT_JUSTIFY") \
    X(ORBX_NAT_SET_TEXT_DROPSHADOW,                    0x465C84BC39F1C351ULL, 1, "SET_TEXT_DROPSHADOW") \
    X(ORBX_NAT_SET_TEXT_OUTLINE,                       0x2513DFB0FB8400FEULL, 0, "SET_TEXT_OUTLINE") \
    X(ORBX_NAT_BEGIN_TEXT_COMMAND_DISPLAY_TEXT,        0x25FBB336DF1804CBULL, 1, "BEGIN_TEXT_COMMAND_DISPLAY_TEXT") \
    X(ORBX_NAT_ADD_TEXT_COMPONENT_SUBSTRING_PLAYER,    0x6C188BE134E074AAULL, 1, "ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME") \
    X(ORBX_NAT_END_TEXT_COMMAND_DISPLAY_TEXT,          0xCD015E5BB0D96A57ULL, 1, "END_TEXT_COMMAND_DISPLAY_TEXT") \
    X(ORBX_NAT_BEGIN_TEXT_COMMAND_THEFEED_POST,        0x202709F4C58A0424ULL, 0, "BEGIN_TEXT_COMMAND_THEFEED_POST") \
    X(ORBX_NAT_END_TEXT_COMMAND_THEFEED_POST_TICKER,   0x2ED7843F8F801023ULL, 0, "END_TEXT_COMMAND_THEFEED_POST_TICKER") \
    X(ORBX_NAT_PLAY_SOUND_FRONTEND,                    0x67C540AA08E4A6F5ULL, 0, "PLAY_SOUND_FRONTEND") \
    X(ORBX_NAT_IS_CONTROL_PRESSED,                     0xF3A21BCD95725A4AULL, 1, "IS_CONTROL_PRESSED") \
    X(ORBX_NAT_IS_DISABLED_CONTROL_PRESSED,            0xE2587F8CBBD87B1DULL, 1, "IS_DISABLED_CONTROL_PRESSED") \
    X(ORBX_NAT_IS_DISABLED_CONTROL_JUST_PRESSED,       0x91AEF906BCA88877ULL, 0, "IS_DISABLED_CONTROL_JUST_PRESSED") \
    X(ORBX_NAT_DISABLE_CONTROL_ACTION,                 0xFE99B66D079CF6BCULL, 1, "DISABLE_CONTROL_ACTION") \
    X(ORBX_NAT_PLAYER_ID,                              0x4F8644AF03D0E0D6ULL, 1, "PLAYER_ID") \
    X(ORBX_NAT_PLAYER_PED_ID,                          0xD80958FC74E988A6ULL, 1, "PLAYER_PED_ID") \
    X(ORBX_NAT_SET_PLAYER_INVINCIBLE,                  0x239528EACDC3E7DEULL, 1, "SET_PLAYER_INVINCIBLE") \
    X(ORBX_NAT_SET_ENTITY_INVINCIBLE,                  0x3882114BDE571AD4ULL, 1, "SET_ENTITY_INVINCIBLE") \
    X(ORBX_NAT_CLEAR_PLAYER_WANTED_LEVEL,              0xB302540597885499ULL, 1, "CLEAR_PLAYER_WANTED_LEVEL") \
    X(ORBX_NAT_SET_MAX_WANTED_LEVEL,                   0xAA5F02DB48D704B9ULL, 0, "SET_MAX_WANTED_LEVEL") \
    X(ORBX_NAT_SET_POLICE_IGNORE_PLAYER,               0x32C62AA929C2DA6AULL, 0, "SET_POLICE_IGNORE_PLAYER") \
    X(ORBX_NAT_GET_ENTITY_MAX_HEALTH,                  0x15D757606D170C3CULL, 0, "GET_ENTITY_MAX_HEALTH") \
    X(ORBX_NAT_SET_ENTITY_HEALTH,                      0x6B76DC1F3AE6E6A3ULL, 0, "SET_ENTITY_HEALTH") \
    X(ORBX_NAT_SET_PED_ARMOUR,                         0xCEA04D83135264CCULL, 0, "SET_PED_ARMOUR") \
    X(ORBX_NAT_SET_SUPER_JUMP_THIS_FRAME,              0x57FFF03E423A4C0BULL, 0, "SET_SUPER_JUMP_THIS_FRAME") \
    X(ORBX_NAT_SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER,   0x6DB47AA77FD94E09ULL, 0, "SET_RUN_SPRINT_MULTIPLIER_FOR_PLAYER") \
    X(ORBX_NAT_SET_SWIM_MULTIPLIER_FOR_PLAYER,         0xA91C6F0FF7D16A13ULL, 0, "SET_SWIM_MULTIPLIER_FOR_PLAYER") \
    X(ORBX_NAT_SET_PED_CAN_RAGDOLL,                    0xB128377056A54E2AULL, 0, "SET_PED_CAN_RAGDOLL") \
    X(ORBX_NAT_SET_EXPLOSIVE_AMMO_THIS_FRAME,          0xA66C71C98D5F2CFBULL, 0, "SET_EXPLOSIVE_AMMO_THIS_FRAME") \
    X(ORBX_NAT_SET_FIRE_AMMO_THIS_FRAME,               0x11879CDD803D30F4ULL, 0, "SET_FIRE_AMMO_THIS_FRAME") \
    X(ORBX_NAT_SET_EXPLOSIVE_MELEE_THIS_FRAME,         0xFF1BED81BFDC0FE0ULL, 0, "SET_EXPLOSIVE_MELEE_THIS_FRAME") \
    X(ORBX_NAT_SET_PED_INFINITE_AMMO_CLIP,             0x183DADC6AA953186ULL, 0, "SET_PED_INFINITE_AMMO_CLIP") \
    X(ORBX_NAT_GIVE_WEAPON_TO_PED,                     0xBF0FD6E56C964FCBULL, 0, "GIVE_WEAPON_TO_PED") \
    X(ORBX_NAT_DOES_ENTITY_EXIST,                      0x7239B21A38F536BAULL, 0, "DOES_ENTITY_EXIST") \
    X(ORBX_NAT_GET_ENTITY_COORDS,                      0x3FEF770D40960D5AULL, 0, "GET_ENTITY_COORDS") \
    X(ORBX_NAT_GET_ENTITY_HEADING,                     0xE83D4F9BA2A38914ULL, 0, "GET_ENTITY_HEADING") \
    X(ORBX_NAT_GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS, 0x1899F328B0E12848ULL, 0, "GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS") \
    X(ORBX_NAT_SET_ENTITY_COORDS_NO_OFFSET,            0x239A3351AC1DA385ULL, 0, "SET_ENTITY_COORDS_NO_OFFSET") \
    X(ORBX_NAT_IS_WAYPOINT_ACTIVE,                     0x1DD1F58F493F1DA5ULL, 0, "IS_WAYPOINT_ACTIVE") \
    X(ORBX_NAT_GET_FIRST_BLIP_INFO_ID,                 0x1BEDE233E6CD2A1FULL, 0, "GET_FIRST_BLIP_INFO_ID") \
    X(ORBX_NAT_DOES_BLIP_EXIST,                        0xA6DB27D19ECBB7DAULL, 0, "DOES_BLIP_EXIST") \
    X(ORBX_NAT_GET_BLIP_COORDS,                        0x586AFE3FF72D996EULL, 0, "GET_BLIP_COORDS") \
    X(ORBX_NAT_GET_GROUND_Z_FOR_3D_COORD,              0xC906A7DAB05C8D2BULL, 0, "GET_GROUND_Z_FOR_3D_COORD") \
    X(ORBX_NAT_IS_PED_IN_ANY_VEHICLE,                  0x997ABD671D25CA0BULL, 0, "IS_PED_IN_ANY_VEHICLE") \
    X(ORBX_NAT_GET_VEHICLE_PED_IS_IN,                  0x9A9112A0FE9A4713ULL, 0, "GET_VEHICLE_PED_IS_IN") \
    X(ORBX_NAT_SET_VEHICLE_FIXED,                      0x115722B1B9C14C1CULL, 0, "SET_VEHICLE_FIXED") \
    X(ORBX_NAT_SET_VEHICLE_DIRT_LEVEL,                 0x79D3B596FE44EE8BULL, 0, "SET_VEHICLE_DIRT_LEVEL") \
    X(ORBX_NAT_SET_VEHICLE_FORWARD_SPEED,              0xAB54A438726D25D5ULL, 0, "SET_VEHICLE_FORWARD_SPEED") \
    X(ORBX_NAT_SET_VEHICLE_ON_GROUND_PROPERLY,         0x49733E92263139D1ULL, 0, "SET_VEHICLE_ON_GROUND_PROPERLY") \
    X(ORBX_NAT_SET_VEHICLE_CUSTOM_PRIMARY_COLOUR,      0x7141766F91D15BEAULL, 0, "SET_VEHICLE_CUSTOM_PRIMARY_COLOUR") \
    X(ORBX_NAT_SET_VEHICLE_CUSTOM_SECONDARY_COLOUR,    0x36CED73BFED89754ULL, 0, "SET_VEHICLE_CUSTOM_SECONDARY_COLOUR") \
    X(ORBX_NAT_SET_VEHICLE_NUMBER_PLATE_TEXT,          0x95A88F0B409CDA47ULL, 0, "SET_VEHICLE_NUMBER_PLATE_TEXT") \
    X(ORBX_NAT_IS_MODEL_IN_CDIMAGE,                    0x35B9E0803292B641ULL, 0, "IS_MODEL_IN_CDIMAGE") \
    X(ORBX_NAT_IS_MODEL_VALID,                         0xC0296A2EDF545E92ULL, 0, "IS_MODEL_VALID") \
    X(ORBX_NAT_REQUEST_MODEL,                          0x963D27A58DF860ACULL, 0, "REQUEST_MODEL") \
    X(ORBX_NAT_HAS_MODEL_LOADED,                       0x98A4EB5D89A0C952ULL, 0, "HAS_MODEL_LOADED") \
    X(ORBX_NAT_CREATE_VEHICLE,                         0xAF35D0D2583051B0ULL, 0, "CREATE_VEHICLE") \
    X(ORBX_NAT_SET_PED_INTO_VEHICLE,                   0xF75B0D629E1C063DULL, 0, "SET_PED_INTO_VEHICLE") \
    X(ORBX_NAT_SET_MODEL_AS_NO_LONGER_NEEDED,          0xE532F5D78798DAABULL, 0, "SET_MODEL_AS_NO_LONGER_NEEDED") \
    X(ORBX_NAT_SET_ENTITY_AS_NO_LONGER_NEEDED,         0xB736A491E64A32CFULL, 0, "SET_ENTITY_AS_NO_LONGER_NEEDED") \
    X(ORBX_NAT_SET_WEATHER_TYPE_NOW_PERSIST,           0xED712CA327900C8AULL, 0, "SET_WEATHER_TYPE_NOW_PERSIST") \
    X(ORBX_NAT_NETWORK_OVERRIDE_CLOCK_TIME,            0xE679E3E06E363892ULL, 0, "NETWORK_OVERRIDE_CLOCK_TIME") \
    X(ORBX_NAT_SET_PED_DENSITY_MULTIPLIER_THIS_FRAME,  0x95E3D6257B166CF2ULL, 0, "SET_PED_DENSITY_MULTIPLIER_THIS_FRAME") \
    X(ORBX_NAT_SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME, 0x245A6883D966D537ULL, 0, "SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME") \
    X(ORBX_NAT_DISPLAY_ONSCREEN_KEYBOARD,              0x00DC833F2568DBF6ULL, 0, "DISPLAY_ONSCREEN_KEYBOARD") \
    X(ORBX_NAT_UPDATE_ONSCREEN_KEYBOARD,               0x0CF2B696BBF945AEULL, 0, "UPDATE_ONSCREEN_KEYBOARD") \
    X(ORBX_NAT_GET_ONSCREEN_KEYBOARD_RESULT,           0x8362B09B91893647ULL, 0, "GET_ONSCREEN_KEYBOARD_RESULT") \
    X(ORBX_NAT_GET_HASH_KEY,                           0xD24D37CC275948CCULL, 0, "GET_HASH_KEY")

enum OrbxMenuNativeId {
#define ORBX_NAT_ENUM_ENTRY(id, hash, req, name) id,
    ORBX_MENU_NATIVE_LIST(ORBX_NAT_ENUM_ENTRY)
#undef ORBX_NAT_ENUM_ENTRY
    ORBX_NAT_COUNT
};

enum OrbxMenuSubmenu {
    ORBX_SUB_MAIN = 0,
    ORBX_SUB_PLAYER = 1,
    ORBX_SUB_WEAPONS = 2,
    ORBX_SUB_VEHICLE = 3,
    ORBX_SUB_SPAWN = 4,
    ORBX_SUB_TELEPORT = 5,
    ORBX_SUB_WORLD = 6,
    ORBX_SUB_ADDON_SPAWN = 7,
    ORBX_SUB_MAX = 8
};

#define ORBX_MAX_ADDON_CARS 64u

typedef struct {
    char     name[24];
    char     label[28];
    uint32_t hash;
} OrbxAddonCarEntry;

enum OrbxMenuWebAction {
    ORBX_ACT_NONE = 0,
    ORBX_ACT_HEAL_ARMOR = 1,
    ORBX_ACT_CLEAR_WANTED = 2,
    ORBX_ACT_GIVE_WEAPONS = 3,
    ORBX_ACT_FIX_VEHICLE = 4,
    ORBX_ACT_FLIP_VEHICLE = 5,
    ORBX_ACT_BOOST_VEHICLE = 6,
    ORBX_ACT_COLOR_VEHICLE = 7,
    ORBX_ACT_TP_WAYPOINT = 8,
    ORBX_ACT_SET_TIME = 9,
    ORBX_ACT_SET_WEATHER = 10,
    ORBX_ACT_SPAWN_VEH = 11
};

typedef struct {
    uint64_t natives[ORBX_NAT_COUNT];

    volatile uint32_t armed;
    volatile uint32_t wait_calls;
    volatile uint32_t main_ticks;
    volatile uint32_t last_script_hash;
    volatile uint32_t last_timer;
    volatile uint32_t menu_open;
    volatile uint32_t submenu;
    int32_t  cursor[ORBX_SUB_MAX];

    uint32_t prev_btn_mask;
    uint32_t hold_btn_mask;
    uint32_t hold_start_ms;
    uint32_t hold_repeat_ms;
    uint32_t applied_flags;
    uint32_t color_rng;

    volatile uint32_t first_tick_logged;
    volatile uint32_t banner_ticks;
    volatile uint32_t toast_ticks;
    char     toast_msg[64];

    /* Persistent feature toggles (shared between in-game HUD and Web Dashboard) */
    volatile uint32_t godmode;
    volatile uint32_t never_wanted;
    volatile uint32_t super_jump;
    volatile uint32_t fast_run;
    volatile uint32_t no_ragdoll;
    volatile uint32_t inf_ammo;
    volatile uint32_t explosive_ammo;
    volatile uint32_t fire_ammo;
    volatile uint32_t explosive_melee;
    volatile uint32_t veh_god;
    volatile uint32_t horn_boost;
    volatile uint32_t density_pct;      /* 100 = default/popcycle, 75, 50, 25, 0 */
    volatile int32_t  lock_hour;        /* -1 = normal clock, 0..23 = locked hour */
    volatile int32_t  weather_idx;      /* 0..7 weather index when applied */

    /* Pending async vehicle spawn & waypoint teleport state */
    volatile uint32_t pending_spawn_hash;
    volatile uint32_t pending_spawn_ticks;
    volatile uint32_t pending_tp_wp_ticks;
    float    pending_tp_x;
    float    pending_tp_y;

    /* Web Dashboard command slot processed on main_persistent tick */
    volatile uint32_t web_action;
    volatile int32_t  web_arg0;
    volatile uint32_t web_spawn_hash;

    /* Live measured script FPS x10 for HUD header */
    uint32_t fps_sample_start_ms;
    uint32_t fps_sample_frames;
    volatile uint32_t fps_x10;

    /* Hardware scePadReadState / scePadRead state (libScePad GOT hooks) */
    volatile uint32_t pad_calls;
    volatile uint32_t pad_buttons_live;
    volatile uint32_t pad_buttons_latch;
    volatile uint32_t pad_raw_seen;
    volatile int32_t  pad_active_handle;

    /* Dynamic add-on vehicle spawner list (loaded from /data/orbx/gta/addon_cars.txt) + IME keyboard */
    volatile uint32_t kb_active;
    volatile uint32_t addon_car_count;
    OrbxAddonCarEntry addon_cars[ORBX_MAX_ADDON_CARS];
} OrbxMenuState;

#ifdef __cplusplus
}
#endif

#endif /* ORBX_MENU_ABI_H */

