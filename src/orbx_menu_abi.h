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
    X(ORBX_NAT_GET_CONTROL_NORMAL,                     0xEC3C9B8D5327B563ULL, 0, "GET_CONTROL_NORMAL") \
    X(ORBX_NAT_GET_DISABLED_CONTROL_NORMAL,            0x11E65974A982637CULL, 0, "GET_DISABLED_CONTROL_NORMAL") \
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
    X(ORBX_NAT_GET_HASH_KEY,                           0xD24D37CC275948CCULL, 0, "GET_HASH_KEY") \
    /* Vehicle Customization & Colors */ \
    X(ORBX_NAT_SET_VEHICLE_COLOURS,                    0x4F1D4BE3A7F24601ULL, 0, "SET_VEHICLE_COLOURS") \
    X(ORBX_NAT_GET_VEHICLE_COLOURS,                    0xA19435F193E081ACULL, 0, "GET_VEHICLE_COLOURS") \
    X(ORBX_NAT_SET_VEHICLE_EXTRA_COLOURS,              0x2036F561ADD12E33ULL, 0, "SET_VEHICLE_EXTRA_COLOURS") \
    X(ORBX_NAT_GET_VEHICLE_EXTRA_COLOURS,              0x3BC4245933A166F7ULL, 0, "GET_VEHICLE_EXTRA_COLOURS") \
    X(ORBX_NAT_SET_VEHICLE_NEON_COLOUR,                0x8E0A582209A62695ULL, 0, "SET_VEHICLE_NEON_COLOUR") \
    X(ORBX_NAT_SET_VEHICLE_NEON_ENABLED,               0x2AA720E4287BF269ULL, 0, "SET_VEHICLE_NEON_ENABLED") \
    X(ORBX_NAT_SET_VEHICLE_TYRE_SMOKE_COLOR,           0xB5BA80F839791C0FULL, 0, "SET_VEHICLE_TYRE_SMOKE_COLOR") \
    X(ORBX_NAT_SET_VEHICLE_MOD_KIT,                    0x1F2AA07F00B3217AULL, 0, "SET_VEHICLE_MOD_KIT") \
    X(ORBX_NAT_SET_VEHICLE_MOD,                        0x6AF0636DDEDCB6DDULL, 0, "SET_VEHICLE_MOD") \
    X(ORBX_NAT_REMOVE_VEHICLE_MOD,                     0x92D4022DC33E044DULL, 0, "REMOVE_VEHICLE_MOD") \
    X(ORBX_NAT_TOGGLE_VEHICLE_MOD,                     0x2A1F4F37F95BAD08ULL, 0, "TOGGLE_VEHICLE_MOD") \
    X(ORBX_NAT_SET_VEHICLE_WINDOW_TINT,                0x57C51E6BAD752696ULL, 0, "SET_VEHICLE_WINDOW_TINT") \
    /* Weather, Clouds & Atmosphere */ \
    X(ORBX_NAT_SET_WEATHER_TYPE_OVERTIME_PERSIST,      0xFB5045B7C42B75BFULL, 0, "SET_WEATHER_TYPE_OVERTIME_PERSIST") \
    X(ORBX_NAT_CLEAR_WEATHER_TYPE_PERSIST,             0xCCC39339BEF76CF5ULL, 0, "CLEAR_WEATHER_TYPE_PERSIST") \
    X(ORBX_NAT_LOAD_CLOUD_HAT,                         0xFC4842A34657BFCBULL, 0, "LOAD_CLOUD_HAT") \
    X(ORBX_NAT_UNLOAD_CLOUD_HAT,                       0xA74802FB8D0B7814ULL, 0, "UNLOAD_CLOUD_HAT") \
    X(ORBX_NAT_UNLOAD_ALL_CLOUD_HATS,                  0x957E790EA1727B64ULL, 0, "UNLOAD_ALL_CLOUD_HATS") \
    X(ORBX_NAT_SET_CLOUDS_ALPHA,                       0xF36199225D6D8C86ULL, 0, "SET_CLOUDS_ALPHA") \
    X(ORBX_NAT_GET_CLOUDS_ALPHA,                       0x20AC25E781AE4A84ULL, 0, "GET_CLOUDS_ALPHA") \
    /* Camera & First Person POV */ \
    X(ORBX_NAT_SET_FOLLOW_PED_CAM_VIEW_MODE,           0x5A4F9EDF1673F704ULL, 0, "SET_FOLLOW_PED_CAM_VIEW_MODE") \
    X(ORBX_NAT_GET_FOLLOW_PED_CAM_VIEW_MODE,           0x8D4D46230B2C353AULL, 0, "GET_FOLLOW_PED_CAM_VIEW_MODE") \
    X(ORBX_NAT_SET_FOLLOW_VEHICLE_CAM_VIEW_MODE,       0xAC253D7842768F48ULL, 0, "SET_FOLLOW_VEHICLE_CAM_VIEW_MODE") \
    X(ORBX_NAT_GET_FOLLOW_VEHICLE_CAM_VIEW_MODE,       0xA4FF579AC0E3AAAEULL, 0, "GET_FOLLOW_VEHICLE_CAM_VIEW_MODE") \
    X(ORBX_NAT_SET_GAMEPLAY_HINT_FOV,                  0x513403FB9C56211FULL, 0, "SET_GAMEPLAY_HINT_FOV") \
    X(ORBX_NAT_SET_FIRST_PERSON_AIM_CAM_NEAR_CLIP,     0x0AF7B437918103B3ULL, 0, "SET_FIRST_PERSON_AIM_CAM_NEAR_CLIP_THIS_UPDATE") \
    X(ORBX_NAT_SET_FIRST_PERSON_AIM_CAM_ZOOM_FACTOR,   0x70894BD0915C5BCAULL, 0, "SET_FIRST_PERSON_AIM_CAM_ZOOM_FACTOR") \
    X(ORBX_NAT_SET_ENTITY_VISIBLE,                     0xEA1C610A04DB6BBBULL, 0, "SET_ENTITY_VISIBLE") \
    X(ORBX_NAT_RESET_ENTITY_ALPHA,                     0x9B1E824FFBB7027AULL, 0, "RESET_ENTITY_ALPHA") \
    X(ORBX_NAT_SET_ENTITY_LOCALLY_VISIBLE,             0x6D40366D4404A816ULL, 0, "SET_ENTITY_LOCALLY_VISIBLE") \
    X(ORBX_NAT_SET_VEHICLE_TYRES_CAN_BURST,            0xEB9DC3C7D8596C46ULL, 0, "SET_VEHICLE_TYRES_CAN_BURST") \
    X(ORBX_NAT_SET_ENTITY_PROOFS,                      0xFAEE099C6F890BB8ULL, 0, "SET_ENTITY_PROOFS") \
    X(ORBX_NAT_SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED,     0x4C7028F78FFD3681ULL, 0, "SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED") \
    X(ORBX_NAT_GET_NUM_VEHICLE_MODS,                   0xE38E9162A2500646ULL, 0, "GET_NUM_VEHICLE_MODS") \
    X(ORBX_NAT_GET_VEHICLE_MOD,                        0x772960298DA26FDBULL, 0, "GET_VEHICLE_MOD") \
    /* ENB / ReShade Live Graphics & Post-Processing */ \
    X(ORBX_NAT_SET_TIMECYCLE_MODIFIER,                 0x2C933ABF17A1DF41ULL, 0, "SET_TIMECYCLE_MODIFIER") \
    X(ORBX_NAT_SET_TIMECYCLE_MODIFIER_STRENGTH,        0x82E7FFCD5B2326B3ULL, 0, "SET_TIMECYCLE_MODIFIER_STRENGTH") \
    X(ORBX_NAT_SET_TRANSITION_TIMECYCLE_MODIFIER,      0x3BCF567485E1971CULL, 0, "SET_TRANSITION_TIMECYCLE_MODIFIER") \
    X(ORBX_NAT_CLEAR_TIMECYCLE_MODIFIER,               0x0F07E7745A236711ULL, 0, "CLEAR_TIMECYCLE_MODIFIER") \
    X(ORBX_NAT_PUSH_TIMECYCLE_MODIFIER,                0x58F735290861E6B4ULL, 0, "PUSH_TIMECYCLE_MODIFIER") \
    X(ORBX_NAT_SET_NOISEOVERIDE,                       0xE787BF1C5CF823C9ULL, 0, "SET_NOISEOVERIDE") \
    X(ORBX_NAT_SET_NOISINESSOVERIDE,                   0xCB6A7C3BB17A0C67ULL, 0, "SET_NOISINESSOVERIDE") \
    X(ORBX_NAT_ANIMPOSTFX_PLAY,                        0x2206BF9A37B7F724ULL, 0, "ANIMPOSTFX_PLAY") \
    X(ORBX_NAT_ANIMPOSTFX_STOP_ALL,                    0xB4EDDC19532BFB85ULL, 0, "ANIMPOSTFX_STOP_ALL") \
    /* Extra Timecycle Modifier & Cascaded Shadow Mapping (PC ENB Layer) */ \
    X(ORBX_NAT_SET_EXTRA_TIMECYCLE_MODIFIER,           0x5096FD9CCB49056DULL, 0, "SET_EXTRA_TIMECYCLE_MODIFIER") \
    X(ORBX_NAT_CLEAR_EXTRA_TIMECYCLE_MODIFIER,         0x92CCC17A7A2285DAULL, 0, "CLEAR_EXTRA_TIMECYCLE_MODIFIER") \
    X(ORBX_NAT_CASCADE_SHADOWS_SET_SHADOW_SAMPLE_TYPE, 0xB11D94BC55F41932ULL, 0, "CASCADE_SHADOWS_SET_SHADOW_SAMPLE_TYPE") \
    X(ORBX_NAT_CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE, 0x27CB772218215325ULL, 0, "CASCADE_SHADOWS_CLEAR_SHADOW_SAMPLE_TYPE") \
    X(ORBX_NAT_CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE, 0x5F0F3F56635809EFULL, 0, "CASCADE_SHADOWS_SET_CASCADE_BOUNDS_SCALE") \
    X(ORBX_NAT_CASCADE_SHADOWS_SET_AIRCRAFT_MODE,       0x6DDBF9DFFC4AC080ULL, 0, "CASCADE_SHADOWS_SET_AIRCRAFT_MODE") \
    X(ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE, 0xD39D13C9FEBF0511ULL, 0, "CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_MODE") \
    X(ORBX_NAT_CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_VALUE, 0x02AC28F3A01FA04AULL, 0, "CASCADE_SHADOWS_SET_DYNAMIC_DEPTH_VALUE") \
    X(ORBX_NAT_CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER,  0x80ECBC0C856D3B0BULL, 0, "CASCADE_SHADOWS_ENABLE_ENTITY_TRACKER") \
    X(ORBX_NAT_OVERRIDE_LODSCALE_THIS_FRAME,           0xA76359FC80B2438EULL, 0, "OVERRIDE_LODSCALE_THIS_FRAME") \
    /* Essential Clouds, Camera Context & Trainer Natives (b323) */ \
    X(ORBX_NAT_SET_CLOUD_SETTINGS_OVERRIDE,            0x02DEAAC8F8EA7FE7ULL, 0, "SET_CLOUD_SETTINGS_OVERRIDE") \
    X(ORBX_NAT_SET_OVERRIDE_WEATHER,                   0xA43D5C6FE51ADBEFULL, 0, "SET_OVERRIDE_WEATHER") \
    X(ORBX_NAT_CLEAR_OVERRIDE_WEATHER,                 0x338D2E3477711050ULL, 0, "CLEAR_OVERRIDE_WEATHER") \
    X(ORBX_NAT_SET_CAM_VIEW_MODE_FOR_CONTEXT,          0x2A2173E46DAECD12ULL, 0, "SET_CAM_VIEW_MODE_FOR_CONTEXT") \
    X(ORBX_NAT_GET_CAM_VIEW_MODE_FOR_CONTEXT,          0xEE778F8C7E1142E2ULL, 0, "GET_CAM_VIEW_MODE_FOR_CONTEXT") \
    X(ORBX_NAT_GET_CAM_ACTIVE_VIEW_MODE_CONTEXT,       0x19CAFA3C87F7C2FFULL, 0, "GET_CAM_ACTIVE_VIEW_MODE_CONTEXT") \
    X(ORBX_NAT_SET_CINEMATIC_MODE_ACTIVE,              0xCE7A90B160F75046ULL, 0, "SET_CINEMATIC_MODE_ACTIVE") \
    X(ORBX_NAT_GET_GAMEPLAY_CAM_ROT,                   0x837765A25378F0BBULL, 0, "GET_GAMEPLAY_CAM_ROT") \
    X(ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_HEADING,      0xB4EC2312F4E5B1F1ULL, 0, "SET_GAMEPLAY_CAM_RELATIVE_HEADING") \
    X(ORBX_NAT_SET_GAMEPLAY_CAM_RELATIVE_PITCH,        0x6D0858B8EDFD2B7DULL, 0, "SET_GAMEPLAY_CAM_RELATIVE_PITCH") \
    X(ORBX_NAT_STOP_GAMEPLAY_CAM_SHAKING,              0x0EF93E9F3D08C178ULL, 0, "STOP_GAMEPLAY_CAM_SHAKING") \
    X(ORBX_NAT_FREEZE_ENTITY_POSITION,                 0x428CA6DBD1094446ULL, 0, "FREEZE_ENTITY_POSITION") \
    X(ORBX_NAT_SET_ENTITY_COLLISION,                   0x1A9205C1B9EE827FULL, 0, "SET_ENTITY_COLLISION") \
    X(ORBX_NAT_SET_ENTITY_HEADING,                     0x8E2530AA8ADA980EULL, 0, "SET_ENTITY_HEADING") \
    X(ORBX_NAT_GET_ENTITY_SPEED,                       0xD5037BA82E12416FULL, 0, "GET_ENTITY_SPEED") \
    X(ORBX_NAT_SPECIAL_ABILITY_FILL_METER,             0x3DACA8DDC6FD4980ULL, 0, "SPECIAL_ABILITY_FILL_METER") \
    X(ORBX_NAT_GET_PLAYER_WANTED_LEVEL,                0xE28E54788CE8F12DULL, 0, "GET_PLAYER_WANTED_LEVEL") \
    X(ORBX_NAT_SET_PLAYER_WANTED_LEVEL,                0x39FF19C64EF7DA5BULL, 0, "SET_PLAYER_WANTED_LEVEL") \
    X(ORBX_NAT_SET_PLAYER_WANTED_LEVEL_NOW,            0xE0A7D1E497FFCD6FULL, 0, "SET_PLAYER_WANTED_LEVEL_NOW") \
    X(ORBX_NAT_SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE,     0x7A6535691B477C48ULL, 0, "SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE") \
    X(ORBX_NAT_SET_PED_CONFIG_FLAG,                    0x1913FE4CBF41C463ULL, 0, "SET_PED_CONFIG_FLAG") \
    X(ORBX_NAT_REMOVE_ALL_PED_WEAPONS,                 0xF25DF915FA38C5F3ULL, 0, "REMOVE_ALL_PED_WEAPONS") \
    X(ORBX_NAT_SET_VEHICLE_EXTRA,                      0x7EE3A3C5E4A40CC9ULL, 0, "SET_VEHICLE_EXTRA") \
    X(ORBX_NAT_SET_VEHICLE_DOOR_OPEN,                  0x7C65DAC73C35C862ULL, 0, "SET_VEHICLE_DOOR_OPEN") \
    X(ORBX_NAT_SET_VEHICLE_DOORS_SHUT,                 0x781B3D62BB013EF5ULL, 0, "SET_VEHICLE_DOORS_SHUT") \
    X(ORBX_NAT_SET_VEHICLE_DOORS_LOCKED,               0xB664292EAECF7FA6ULL, 0, "SET_VEHICLE_DOORS_LOCKED") \
    X(ORBX_NAT_SET_VEHICLE_REDUCE_GRIP,                0x222FF6A823D122E2ULL, 0, "SET_VEHICLE_REDUCE_GRIP") \
    X(ORBX_NAT_SET_VEHICLE_CHEAT_POWER_INCREASE,       0xB59E4BD37AE292DBULL, 0, "SET_VEHICLE_CHEAT_POWER_INCREASE") \
    X(ORBX_NAT_GET_CLOSEST_VEHICLE,                    0xF73EB622C4F1689BULL, 0, "GET_CLOSEST_VEHICLE") \
    X(ORBX_NAT_SET_TIME_SCALE,                         0x1D408577D440E81EULL, 0, "SET_TIME_SCALE") \
    X(ORBX_NAT_PAUSE_CLOCK,                            0x4D1A590C92BF377EULL, 0, "PAUSE_CLOCK") \
    X(ORBX_NAT_NETWORK_CLEAR_CLOCK_TIME_OVERRIDE,      0xD9720528D30EC3A7ULL, 0, "NETWORK_CLEAR_CLOCK_TIME_OVERRIDE") \
    X(ORBX_NAT_GET_CLOCK_HOURS,                        0x25223CA6B4D20B7FULL, 0, "GET_CLOCK_HOURS") \
    X(ORBX_NAT_GET_CLOCK_MINUTES,                      0x13D2B8ADD79640F2ULL, 0, "GET_CLOCK_MINUTES") \
    X(ORBX_NAT_SET_GRAVITY_LEVEL,                      0x740E14FAD5842351ULL, 0, "SET_GRAVITY_LEVEL") \
    X(ORBX_NAT_REQUEST_IPL,                            0x41B4893843BBDB74ULL, 0, "REQUEST_IPL") \
    X(ORBX_NAT_REMOVE_IPL,                             0xEE6C5AD3ECE0A82DULL, 0, "REMOVE_IPL") \
    X(ORBX_NAT_CLEAR_AREA_OF_PEDS,                     0x04F8FC8FCF58F88DULL, 0, "CLEAR_AREA_OF_PEDS") \
    X(ORBX_NAT_CLEAR_AREA_OF_VEHICLES,                 0x11DB3500F042A8AAULL, 0, "CLEAR_AREA_OF_VEHICLES") \
    X(ORBX_NAT_SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY, 0x3B988190C0AA6C0BULL, 0, "SET_MOBILE_RADIO_ENABLED_DURING_GAMEPLAY") \
    /* Scripted First-Person POV Camera (b323) */ \
    X(ORBX_NAT_CREATE_CAM,                             0xC3981DCE61D9E13FULL, 0, "CREATE_CAM") \
    X(ORBX_NAT_DESTROY_CAM,                            0x865908C81A2C22E9ULL, 0, "DESTROY_CAM") \
    X(ORBX_NAT_DOES_CAM_EXIST,                         0xA7A932170592B50EULL, 0, "DOES_CAM_EXIST") \
    X(ORBX_NAT_SET_CAM_ACTIVE,                         0x026FB97D0A425F84ULL, 0, "SET_CAM_ACTIVE") \
    X(ORBX_NAT_SET_CAM_COORD,                          0x4D41783FB745E42EULL, 0, "SET_CAM_COORD") \
    X(ORBX_NAT_SET_CAM_ROT,                            0x85973643155D0B07ULL, 0, "SET_CAM_ROT") \
    X(ORBX_NAT_SET_CAM_FOV,                            0xB13C14F66A00D047ULL, 0, "SET_CAM_FOV") \
    X(ORBX_NAT_SET_CAM_NEAR_CLIP,                      0xC7848EFCCC545182ULL, 0, "SET_CAM_NEAR_CLIP") \
    X(ORBX_NAT_RENDER_SCRIPT_CAMS,                     0x07E5B515DB0636FCULL, 0, "RENDER_SCRIPT_CAMS") \
    X(ORBX_NAT_GET_PED_BONE_COORDS,                    0x17C07FC640E86B4EULL, 0, "GET_PED_BONE_COORDS") \
    X(ORBX_NAT_IS_PLAYER_FREE_AIMING,                  0x2E397FD2ECD37C87ULL, 0, "IS_PLAYER_FREE_AIMING") \
    X(ORBX_NAT_IS_PAUSE_MENU_ACTIVE,                   0xB0034A223497FFCBULL, 0, "IS_PAUSE_MENU_ACTIVE") \
    X(ORBX_NAT_IS_PED_ARMED,                           0x475768A975D5AD17ULL, 0, "IS_PED_ARMED") \
    X(ORBX_NAT_IS_PED_SHOOTING,                        0x34616828CD07F1A1ULL, 0, "IS_PED_SHOOTING") \
    X(ORBX_NAT_GET_PED_STEALTH_MOVEMENT,               0x7C2AC9CA66575FBFULL, 0, "GET_PED_STEALTH_MOVEMENT") \
    X(ORBX_NAT_ATTACH_CAM_TO_ENTITY,                   0xFEDB7D269E8C60E3ULL, 0, "ATTACH_CAM_TO_ENTITY") \
    X(ORBX_NAT_ATTACH_CAM_TO_PED_BONE,                 0x61A3DBA14AB7F411ULL, 0, "ATTACH_CAM_TO_PED_BONE") \
    X(ORBX_NAT_DETACH_CAM,                             0xA2FABBE87F4BAD82ULL, 0, "DETACH_CAM") \
    X(ORBX_NAT_GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS, 0x2274BC1C4885E333ULL, 0, "GET_OFFSET_FROM_ENTITY_GIVEN_WORLD_COORDS") \
    /* Realistic Fuel System & Physics Rope / Nozzle (b323) */ \
    X(ORBX_NAT_ADD_ROPE,                               0xE832D760399EB220ULL, 0, "ADD_ROPE") \
    X(ORBX_NAT_DELETE_ROPE,                            0x52B4829281364649ULL, 0, "DELETE_ROPE") \
    X(ORBX_NAT_ATTACH_ENTITIES_TO_ROPE,                0x3D95EC8B6D940AC3ULL, 0, "ATTACH_ENTITIES_TO_ROPE") \
    X(ORBX_NAT_ROPE_LOAD_TEXTURES,                     0x9B9039DBF2D258C1ULL, 0, "ROPE_LOAD_TEXTURES") \
    X(ORBX_NAT_ROPE_ARE_TEXTURES_LOADED,               0xF2D0E6A75CC05597ULL, 0, "ROPE_ARE_TEXTURES_LOADED") \
    X(ORBX_NAT_ROPE_UNLOAD_TEXTURES,                   0x6CE36C35C1AC8163ULL, 0, "ROPE_UNLOAD_TEXTURES") \
    X(ORBX_NAT_CREATE_OBJECT,                          0x509D5878EB39E842ULL, 0, "CREATE_OBJECT") \
    X(ORBX_NAT_DELETE_OBJECT,                          0x539E0AE3E6634B9FULL, 0, "DELETE_OBJECT") \
    X(ORBX_NAT_ATTACH_ENTITY_TO_ENTITY,                0x6B9BBD38AB0796DFULL, 0, "ATTACH_ENTITY_TO_ENTITY") \
    X(ORBX_NAT_DETACH_ENTITY,                          0x961AC54BF0613F5DULL, 0, "DETACH_ENTITY") \
    X(ORBX_NAT_GET_CLOSEST_OBJECT_OF_TYPE,             0xE143FA2249364369ULL, 0, "GET_CLOSEST_OBJECT_OF_TYPE") \
    X(ORBX_NAT_GET_PED_BONE_INDEX,                     0x3F428D08BE5AAE31ULL, 0, "GET_PED_BONE_INDEX") \
    X(ORBX_NAT_SET_CURRENT_PED_WEAPON,                 0xADF692B254977C0CULL, 0, "SET_CURRENT_PED_WEAPON") \
    X(ORBX_NAT_SET_VEHICLE_ENGINE_ON,                  0x2497C4717C8B881EULL, 0, "SET_VEHICLE_ENGINE_ON") \
    X(ORBX_NAT_SET_VEHICLE_UNDRIVEABLE,                0x8ABA6AF54B942B95ULL, 0, "SET_VEHICLE_UNDRIVEABLE") \
    /* Calmer Police Dispatch & Control (b323) */ \
    X(ORBX_NAT_SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER, 0x48838ED9937A15D1ULL, 0, "SET_DISPATCH_TIME_BETWEEN_SPAWN_ATTEMPTS_MULTIPLIER") \
    X(ORBX_NAT_SET_DISPATCH_IDEAL_SPAWN_DISTANCE,      0x6FE601A64180D423ULL, 0, "SET_DISPATCH_IDEAL_SPAWN_DISTANCE") \
    X(ORBX_NAT_RESET_DISPATCH_IDEAL_SPAWN_DISTANCE,    0x77A84429DD9F0A15ULL, 0, "RESET_DISPATCH_IDEAL_SPAWN_DISTANCE") \
    /* Texture streaming & sprite drawing (b323) */ \
    X(ORBX_NAT_REQUEST_STREAMED_TEXTURE_DICT,          0xDFA2EF8E04127DD5ULL, 0, "REQUEST_STREAMED_TEXTURE_DICT") \
    X(ORBX_NAT_HAS_STREAMED_TEXTURE_DICT_LOADED,        0x0145F696AAAAD2E4ULL, 0, "HAS_STREAMED_TEXTURE_DICT_LOADED") \
    X(ORBX_NAT_DRAW_SPRITE,                            0xE7FFAE5EBF23D890ULL, 0, "DRAW_SPRITE") \
    /* GTA VI HUD & Weapon Wheel Natives (b323) */ \
    X(ORBX_NAT_HIDE_HUD_COMPONENT_THIS_FRAME,          0x4EB223432F8FA0A0ULL, 0, "HIDE_HUD_COMPONENT_THIS_FRAME") \
    X(ORBX_NAT_GET_CURRENT_PED_WEAPON,                 0x23B29877D0BE9547ULL, 0, "GET_CURRENT_PED_WEAPON") \
    X(ORBX_NAT_GET_AMMO_IN_PED_WEAPON,                 0x1149D67DB429787AULL, 0, "GET_AMMO_IN_PED_WEAPON") \
    X(ORBX_NAT_GET_AMMO_IN_CLIP,                       0x0227CB6AD5598268ULL, 0, "GET_AMMO_IN_CLIP") \
    X(ORBX_NAT_HAS_PED_GOT_WEAPON,                     0x66B90BA528CFEBCEULL, 0, "HAS_PED_GOT_WEAPON") \
    X(ORBX_NAT_STAT_GET_INT,                           0xDF7F16323520B858ULL, 0, "STAT_GET_INT") \
    X(ORBX_NAT_GET_ENTITY_MODEL,                       0x4B423FAA24E8ABF0ULL, 0, "GET_ENTITY_MODEL") \
    X(ORBX_NAT_GET_VEHICLE_ENGINE_HEALTH,              0x4C7724D572378B05ULL, 0, "GET_VEHICLE_ENGINE_HEALTH") \
    /* Stage 1 High-Detail Streaming Area & ENB DOF / Shadow Culling (b323) */ \
    X(ORBX_NAT_SET_HD_AREA,                            0xB85F26619073E775ULL, 0, "SET_HD_AREA") \
    X(ORBX_NAT_CLEAR_HD_AREA,                          0xCE58B1CFB9290813ULL, 0, "CLEAR_HD_AREA") \
    X(ORBX_NAT_SET_BACKFACECULLING,                    0x23BA6B0C2AD7B0D3ULL, 0, "SET_BACKFACECULLING") \
    X(ORBX_NAT_SET_USE_HI_DOF,                         0xA13B0222F3D94A94ULL, 0, "SET_USE_HI_DOF") \
    X(ORBX_NAT_SET_HIDOF_OVERRIDE,                     0xBA3D65906822BED5ULL, 0, "SET_HIDOF_OVERRIDE")

enum OrbxMenuNativeId {
#define ORBX_NAT_ENUM_ENTRY(id, hash, req, name) id,
    ORBX_MENU_NATIVE_LIST(ORBX_NAT_ENUM_ENTRY)
#undef ORBX_NAT_ENUM_ENTRY
    ORBX_NAT_COUNT
};

#define ORBX_MAX_NATIVES  256u
#define ORBX_MAX_SUBMENUS 32u

enum OrbxMenuSubmenu {
    ORBX_SUB_MAIN = 0,
    ORBX_SUB_PLAYER = 1,
    ORBX_SUB_WEAPONS = 2,
    ORBX_SUB_VEHICLE = 3,
    ORBX_SUB_SPAWN = 4,
    ORBX_SUB_TELEPORT = 5,
    ORBX_SUB_WORLD = 6,
    ORBX_SUB_ADDON_SPAWN = 7,
    ORBX_SUB_VEH_CUSTOM = 8,
    ORBX_SUB_VEH_COLORS = 9,
    ORBX_SUB_VEH_LIGHTS = 10,
    ORBX_SUB_CAMERA = 11,
    ORBX_SUB_CLOUDS = 12,
    ORBX_SUB_SETTINGS = 13,
    ORBX_SUB_GRAPHICS = 14,
    ORBX_SUB_KEYBINDS = 15,
    ORBX_SUB_VEH_DOORS = 16,
    ORBX_SUB_SPAWN_CAT = 17,
    ORBX_SUB_IPL_INTERIORS = 18,
    ORBX_SUB_MAX = 19
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
    ORBX_ACT_SPAWN_VEH = 11,
    ORBX_ACT_SET_GFX_PRESET = 12,
    ORBX_ACT_SET_GFX_STRENGTH = 13,
    ORBX_ACT_SET_POSTFX = 14,
    ORBX_ACT_APPLY_CLOUD_HAT = 15,
    ORBX_ACT_APPLY_CLOUD_KEYFRAME = 16,
    ORBX_ACT_CLEAR_CLOUD_HATS = 17,
    ORBX_ACT_RESET_GFX = 18,
    ORBX_ACT_APPLY_CAM_VIEW = 19,
    ORBX_ACT_RESET_CAM_BEHIND = 20,
    ORBX_ACT_MAX_VEH = 21,
    ORBX_ACT_WARP_NEAREST_VEH = 22,
    ORBX_ACT_SET_TIMESCALE = 23,
    ORBX_ACT_CLEAR_AREA = 24,
    ORBX_ACT_RESET_KEYBINDS = 25,
    ORBX_ACT_SAVE_CONFIG = 26,
    ORBX_ACT_RESET_ALL_MODS = 27,
    ORBX_ACT_TP_PRESET = 29,
    ORBX_ACT_TOGGLE_REALISTIC_FUEL = 30,
    ORBX_ACT_REFUEL_VEHICLE = 31,
    ORBX_ACT_TOGGLE_CALMER_POLICE = 32,
    ORBX_ACT_TOGGLE_GTA6_HUD = 33,
    ORBX_ACT_TOGGLE_GTA6_WHEEL = 34,
    ORBX_ACT_SET_ENB_EXTRA = 35,
    ORBX_ACT_SET_SHADOW_QUALITY = 36,
    ORBX_ACT_SET_VEH_MOD = 37,
    ORBX_ACT_SET_WINDOW_TINT = 38
};

typedef struct {
    uint64_t natives[ORBX_MAX_NATIVES];

    volatile uint32_t armed;
    volatile uint32_t wait_calls;
    volatile uint32_t main_ticks;
    volatile uint32_t last_script_hash;
    volatile uint32_t last_timer;
    volatile uint32_t menu_open;
    volatile uint32_t submenu;
    int32_t  cursor[ORBX_MAX_SUBMENUS];

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
    volatile int32_t  weather_idx;      /* 0..14 weather index when applied */

    /* UI Styling, Settings & Preferences */
    volatile uint32_t initialized;
    volatile uint32_t menu_position;     /* 0 = Left (0.160f), 1 = Right (0.835f) */
    volatile uint32_t theme_accent;      /* 0=Cyan, 1=Purple, 2=Green, 3=Red, 4=Gold, 5=White */
    volatile uint32_t sound_enabled;     /* 1 = On, 0 = Off */

    /* Vehicle Customization & Color state */
    volatile uint32_t veh_color_target;   /* 0=Both, 1=Primary, 2=Secondary, 3=Pearl, 4=Wheels */
    volatile uint32_t veh_color_preset;   /* 0..preset index */
    volatile uint32_t veh_color_cat;      /* 0=Classic/Metallic, 1=Matte, 2=Metals, 3=Pearlescent */
    volatile uint32_t veh_neon_on;        /* Neon underglow toggle */
    volatile uint32_t veh_neon_front;
    volatile uint32_t veh_neon_back;
    volatile uint32_t veh_neon_left;
    volatile uint32_t veh_neon_right;
    volatile uint32_t veh_neon_color_idx; /* Neon color preset index */
    volatile uint32_t veh_smoke_color_idx;/* Tire smoke color preset index */
    volatile uint32_t veh_tint_idx;       /* Window tint index 0..5 */
    volatile uint32_t veh_engine_lvl;     /* Engine tuning level 0..4 */
    volatile uint32_t veh_brake_lvl;      /* Brakes level 0..3 */
    volatile uint32_t veh_trans_lvl;      /* Transmission level 0..3 */
    volatile uint32_t veh_susp_lvl;       /* Suspension level 0..4 */
    volatile uint32_t veh_armor_lvl;      /* Armor level 0..5 */
    volatile uint32_t veh_bulletproof_tyres;
    volatile uint32_t weather_persist;

    /* Cloud & Weather Atmosphere state */
    volatile int32_t  cloud_hat_idx;      /* 0..15 cloud hat preset index */
    volatile uint32_t cloud_alpha_pct;    /* 0..100 cloud opacity percentage */

    /* Camera & First Person POV state */
    volatile uint32_t fp_fov_idx;         /* 0=Default, 1=65 deg .. 10=110 deg */
    volatile uint32_t fp_clip_fix;        /* Near-clip fix to prevent weapon camera clipping */
    volatile uint32_t cam_ped_view_mode;  /* Follow ped view mode (0=close, 1=mid, 2=far, 4=first person) */
    volatile uint32_t cam_veh_view_mode;  /* Follow veh view mode (0=close, 1=mid, 2=far, 4=cockpit) */
    volatile uint32_t invisible;          /* Player invisibility toggle */

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

    /* ENB / ReShade Live Graphics Enhancer state (non-destructive overlay) */
    volatile uint32_t gfx_preset_idx;     /* 0=OFF (Original), 1..14=Preset modifier */
    volatile uint32_t gfx_strength_pct;   /* 10..200 filter intensity % (default 100) */
    volatile uint32_t gfx_film_grain;     /* 0..100 ReShade film grain / sharpness noise % */
    volatile uint32_t gfx_postfx_idx;     /* 0=OFF, 1..6 screen lens post-FX overlay */
    char              gfx_custom_name[32];/* Custom modifier name (default AmbientPUSH) */
    volatile uint32_t enb_extra_active;   /* Stage 1: Anti-Aliasing (Smooth) level 0..5 */
    volatile uint32_t enb_shadow_soft;    /* Stage 2: Dynamic Shadows & Building AO level 0..5 */
    volatile uint32_t enb_shadow_scale;   /* Stage 1: LOD Draw Distance x10 (0/10=1.0x, 15=1.5x, 20=2.0x, 25=2.5x, 35=3.5x, 50=5.0x) */
    volatile uint32_t enb_ssao_boost;     /* Stage 2: ENB Scene Darkness level 0..5 */
    volatile uint32_t enb_bloom_lvl;      /* Stage 2: ENB Bloom & Glow level 0..5 */
    volatile uint32_t enb_refl_lvl;       /* Stage 2: ENB Mirror Reflections level 0..5 */
    volatile uint32_t enb_dof_lvl;        /* Stage 1: ENB Depth of Field (Bokeh DOF) level 0..5 */

    /* Flight-Recorder Crash Probe & Native Diagnostics */
    volatile uint32_t probe_stage;        /* 0=IDLE, 1=PRE_NATIVE, 2=IN_GFX, 3=IN_SPAWN, 4=IN_HUD */
    volatile uint32_t probe_last_native_id;
    volatile uint32_t probe_last_tick;
    volatile uint32_t probe_resolved_count;
    char              probe_last_arg[32];

    /* Phase 1: Navigation Stack, Accessibility & UI Scale */
    uint32_t          sub_stack[8];
    int32_t           cur_stack[8];
    uint32_t          sub_depth;
    volatile uint32_t menu_scale;         /* 0=Compact, 1=Normal, 2=Large */
    volatile uint32_t keep_steering;      /* 1=Keep drive/steer L-Stick+L2/R2 active while open */
    volatile uint32_t nav_stick;          /* 1=Analog stick mirrors D-Pad in menu */
    volatile uint32_t pad_suppress_mask;  /* Dynamic ScePad bitmask suppressed in orbx_native.c */
    volatile uint32_t spawn_cat_idx;      /* Category index 0..5 for ORBX_SUB_SPAWN_CAT */

    /* Phase 1: Custom Keybinds, Live Controller Capture & Config Sync */
    volatile uint32_t bind_open_mod;      /* Modifier bitmask (0 = single button) */
    volatile uint32_t bind_open_btn;      /* Primary open/close trigger bitmask */
    volatile uint32_t bind_accept;        /* Menu select / apply button */
    volatile uint32_t bind_cancel;        /* Menu back / close button */
    volatile uint32_t bind_tab_prev;      /* Top tab previous button */
    volatile uint32_t bind_tab_next;      /* Top tab next button */
    volatile uint32_t bind_horn_boost;    /* Closed-menu quick hotkey: horn boost */
    volatile uint32_t bind_noclip;        /* Closed-menu quick hotkey: airbreak toggle */
    volatile uint32_t bind_quick_repair;  /* Closed-menu quick hotkey: repair/wash */
    volatile uint32_t bind_quick_heal;    /* Closed-menu quick hotkey: heal + armor */
    volatile uint32_t bind_super_brake;   /* Closed-menu quick hotkey: instant stop */
    volatile uint32_t keybind_capture_slot; /* 0=Idle, 1..11=Awaiting controller input */
    volatile uint32_t keybind_capture_step; /* 0=idle, 1=wait release, 2=capture press */
    uint32_t          hotkey_lockout_until_ms;
    uint32_t          prev_live_btn_mask;
    volatile uint32_t cfg_save_req;       /* Set 1 to request host modhen_config_save */

    /* Cloud Keyframes & Camera / POV Fixes */
    volatile int32_t  cloud_keyframe_idx; /* -1=Weather Default, 0..13=NVE cloudkeyframes preset */
    volatile uint32_t cloud_override_weather_on;
    volatile uint32_t fp_lock;            /* Lock view mode to First Person */
    volatile uint32_t cinematic_cam;      /* Separate Cinematic Camera toggle */
    volatile uint32_t cam_anti_shake;     /* Suppress gameplay camera shake */
    int32_t           lsc_slot_idx;
    uint32_t          last_weather_apply_tick;

    /* Core Trainer States */
    volatile uint32_t noclip_on;          /* Airbreak / No-Clip flight */
    volatile uint32_t noclip_speed_idx;   /* 0=Slow, 1=Normal, 2=Fast, 3=Turbo */
    float             noclip_x;
    float             noclip_y;
    float             noclip_z;
    uint32_t          noclip_pos_valid;
    int32_t           noclip_prev_ent;
    volatile uint32_t sprint_mult_idx;    /* 0=1.0x, 1=1.25x, 2=1.49x */
    volatile uint32_t swim_mult_idx;      /* 0=1.0x, 1=1.25x, 2=1.49x */
    volatile uint32_t wanted_cap_idx;     /* 0=Normal (5), 1..5 = Lock wanted cap */
    volatile uint32_t spec_ability_inf;   /* Infinite Special Ability meter */
    volatile uint32_t seatbelt_on;        /* Cannot be ejected/knocked off */
    volatile uint32_t veh_drift_mode;     /* Reduce tire grip */
    volatile uint32_t veh_torque_idx;     /* 0=1x, 1=2x, 2=5x, 3=10x torque multiplier */
    volatile uint32_t speedo_mode;        /* 0=OFF, 1=MPH, 2=KPH */
    volatile uint32_t veh_doors_locked;   /* 0=Unlocked, 1=Locked */
    volatile uint32_t time_scale_idx;     /* 0=1.0x, 1=0.75x, 2=0.5x, 3=0.25x */
    volatile uint32_t clock_paused;       /* Freeze in-game clock */
    volatile uint32_t low_gravity;        /* Moon gravity */
    volatile uint32_t mobile_radio;       /* Listen to radio on foot */
    volatile uint32_t kb_mode;            /* 0=Spawn Vehicle Name, 1=Custom License Plate */
    volatile uint32_t quick_heal_req;     /* Triggered by quick hotkey or action */
    volatile uint32_t quick_repair_req;   /* Triggered by quick hotkey or action */
    uint32_t          open_combo_wait_rel;/* Held open/close combo buttons awaiting release */
    int32_t           tab_pend_dir;       /* Deferred tab switch when tab key overlaps open mod */
    uint32_t          tab_pend_btn;       /* Button mask being held for deferred tab switch */
    uint32_t          tab_pend_start_ms;  /* Press timestamp for tap-vs-combo disambiguation */
    uint32_t          nav_lockout_until_ms;/* Brief grace lockout right after menu opens */
    volatile uint32_t veh_turbo_on;       /* Real tracked toggle state for Turbo Tuning */
    volatile uint32_t veh_xenon_on;       /* Real tracked toggle state for Xenon Headlights */
    int32_t           last_applied_ped;
    int32_t           last_applied_veh;
    uint32_t          last_want_cap_applied;
    uint32_t          last_density_pct;
    uint32_t          mp_last_wait_call;
    uint32_t          last_lock_hour_tick;

    /* Scripted Wide-FOV First-Person Camera runtime state */
    int32_t           fp_cam_handle;
    uint32_t          fp_cam_active;
    float             fp_cam_yaw;
    float             fp_cam_pitch;
    float             fp_prev_veh_hdg;
    uint32_t          fp_yield_until_ms;
    int32_t           fp_prev_veh;

    /* Forza 6 HUD Speedometer runtime state */
    float             speedo_needle_rpm;  /* Smooth filtered RPM for pointer (0.0 to 1.0) */
    uint32_t          speedo_last_ms;     /* Last update timestamp for delta smoothing */
    int32_t           speedo_cur_gear;    /* Tracked gear: -1=R, 0=N, 1..7=Forward */

    /* Alternate First Person & Camera Modes (PC ASI Mod Port) */
    volatile uint32_t afp_mode;          /* 0=OFF, 1=Alt FP, 2=Bodycam, 3=Shoulder, 4=Dashcam, 5=Helmet */
    volatile uint32_t afp_fov_idx;       /* 0=75, 1=80, 2=85, 3=90, 4=95, 5=100, 6=105, 7=110 */
    volatile uint32_t afp_headbob;       /* 0=OFF, 1=Subtle, 2=Normal, 3=Cinematic */
    volatile uint32_t afp_smooth_follow; /* 0=OFF, 1=Subtle, 2=Bodycam (0.85), 3=Heavy (0.92) */
    volatile uint32_t afp_recoil;        /* 0=OFF, 1=Subtle, 2=Standard, 3=Cinematic */
    volatile uint32_t afp_inertia;       /* 0=OFF, 1=ON */
    volatile uint32_t afp_crouch_lower;  /* 0=OFF, 1=ON */
    volatile uint32_t afp_ads_zoom;      /* 0=OFF, 1=15 deg, 2=25 deg, 3=35 deg */
    volatile uint32_t afp_crosshair;     /* 0=OFF, 1=Always, 2=When Aiming */
    volatile uint32_t afp_autoturn;      /* 0=OFF, 1=ON */
    volatile uint32_t afp_clip_fix;      /* 0=OFF, 1=ON */

    /* Alternate First Person runtime internal state */
    int32_t           afp_cam_handle;
    uint32_t          afp_cam_active;
    float             afp_cur_yaw;
    float             afp_cur_pitch;
    float             afp_cur_fov;
    float             afp_bob_phase;
    float             afp_recoil_pitch;
    float             afp_recoil_roll;
    float             afp_inertia_pitch;
    float             afp_prev_speed;
    uint32_t          afp_prev_ms;
    int32_t           afp_attached_entity; /* Entity handle currently attached to (0 if detached) */
    uint32_t          afp_attached_mode;   /* Mode for which attachment was made */
    int32_t           afp_attached_bone;   /* Bone index currently attached to (-1 if detached) */
    float             afp_smooth_z;        /* Smoothed vertical head position for on-foot */
    float             afp_veh_off_x;       /* Cached vehicle attachment offset X */
    float             afp_veh_off_y;       /* Cached vehicle attachment offset Y */
    float             afp_veh_off_z;       /* Cached vehicle attachment offset Z */

    /* Realistic Fuel System & Physics Rope / Nozzle runtime state */
    volatile uint32_t fuel_system_on;       /* 0=OFF, 1=ON */
    volatile float    fuel_level;           /* 0.0f to 100.0f (%) */
    int32_t           fuel_veh_handle;      /* Current tracked vehicle */
    uint32_t          fuel_last_burn_ms;    /* Timer for delta burn */
    int32_t           fuel_nozzle_obj;      /* Prop handle for prop_cs_fuel_nozle */
    int32_t           fuel_rope_id;         /* Rope handle from ADD_ROPE */
    int32_t           fuel_pump_obj;        /* Nearest gas pump object handle */
    uint32_t          fuel_stage;           /* 0=Off/Idle, 1=NozzleInHand, 2=NozzleInCar */
    uint32_t          fuel_refuel_tick;     /* Tick counter for refuel rate */
    uint32_t          fuel_last_stall_ms;   /* Last stall notification timestamp */
    uint32_t          fuel_cooldown_until_ms; /* Stage transition debounce / lockout timestamp */

    /* Calmer Police AI runtime state */
    volatile uint32_t calmer_police_on;     /* 0=OFF, 1=ON */
    uint32_t          calmer_last_wanted;   /* Previous wanted level */

    /* GTA VI Immersive HUD & Weapon Wheel runtime state */
    volatile uint32_t gta6_hud_on;          /* 0=OFF, 1=ON (Vehicle card, cash badges, minimap style) */
    volatile uint32_t gta6_wheel_on;        /* 0=OFF, 1=ON (GTA VI Weapon Wheel on L1 hold) */
    uint32_t          gta6_wheel_active;    /* 1 when L1 held */
    int32_t           gta6_wheel_slot;      /* Selected slot 0..7 */
    uint32_t          gta6_cash_show_until; /* Cash HUD fade timestamp */
    int32_t           gta6_last_wallet_cash;
    int32_t           gta6_last_bank_cash;
    volatile uint32_t enb_motion_blur;      /* Stage 1: NVE Motion Blur level 0..5 */

    /* Large 3.5 KB add-on car array kept at the end so telemetry polls skip it */
    OrbxAddonCarEntry addon_cars[ORBX_MAX_ADDON_CARS];
} OrbxMenuState;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert((uint32_t)ORBX_NAT_COUNT <= ORBX_MAX_NATIVES, "ORBX_MAX_NATIVES exceeded");
_Static_assert((uint32_t)ORBX_SUB_MAX <= ORBX_MAX_SUBMENUS, "ORBX_MAX_SUBMENUS exceeded");
#endif

#ifdef __cplusplus
}
#endif

#endif /* ORBX_MENU_ABI_H */

