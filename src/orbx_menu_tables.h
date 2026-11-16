#ifndef ORBX_MENU_TABLES_H
#define ORBX_MENU_TABLES_H

#include <stdint.h>
#include "orbx_menu_abi.h"

/*
 * XOM-Safe Static Constant Lookup Tables for ModHEN PS5 Native Menu.
 * Rules: Fixed-width char[] arrays ONLY — zero pointers (char*) in structs
 * so clang -fPIC places everything cleanly in .rodata (zero .data.rel.ro/.got).
 */

typedef struct {
    uint8_t sub_id;
    char    label[10];
    char    title[24];
} MenuTabDef;

static const MenuTabDef kTopTabs[8] = {
    { ORBX_SUB_MAIN,     "HOME",     "HOME & PRESETS" },
    { ORBX_SUB_PLAYER,   "SELF",     "SELF OPTIONS" },
    { ORBX_SUB_WEAPONS,  "WEAPONS",  "WEAPON OPTIONS" },
    { ORBX_SUB_VEHICLE,  "VEHICLE",  "VEHICLE OPTIONS" },
    { ORBX_SUB_SPAWN,    "SPAWN",    "VEHICLE SPAWNER" },
    { ORBX_SUB_WORLD,    "WORLD",    "WORLD & CLOUDS" },
    { ORBX_SUB_CAMERA,   "CAMERA",   "CAMERA & GFX" },
    { ORBX_SUB_SETTINGS, "SETTINGS", "SETTINGS & KEYS" }
};

typedef struct {
    char     name[24];
    char     label[20];
    uint32_t hash;
} SpawnVehEntry;

typedef struct {
    char    cat_name[22];
    char    cat_desc[48];
    uint8_t count;
    SpawnVehEntry items[10];
} SpawnCategory;

static const SpawnCategory kSpawnCategories[6] = {
    {
        "Super / Hyper", "Flagship high-speed supercars (v1.00)", 9,
        {
            { "adder",    "Adder",       0 },
            { "zentorno", "Zentorno",    0 },
            { "entityxf", "Entity XF",   0 },
            { "turismor", "Turismo R",   0 },
            { "infernus", "Infernus",    0 },
            { "cheetah",  "Cheetah",     0 },
            { "vacca",    "Vacca",       0 },
            { "bullet",   "Bullet",      0 },
            { "voltic",   "Voltic",      0 }
        }
    },
    {
        "Sports & JDM", "Street tuners, JDM & GT racecars", 10,
        {
            { "elegy2",       "Elegy RH8",        0 },
            { "jester",       "Jester",           0 },
            { "jester2",      "Jester Racecar",   0 },
            { "massacro",     "Massacro",         0 },
            { "massacro2",    "Massacro Racecar", 0 },
            { "feltzer2",     "Feltzer",          0 },
            { "banshee",      "Banshee",          0 },
            { "comet2",       "Comet",            0 },
            { "sultan",       "Sultan",           0 },
            { "futo",         "Futo (Drift)",     0 }
        }
    },
    {
        "Muscle & Classics", "Classic & modern V8 muscle icons", 9,
        {
            { "dukes2",    "Dukes O'Death",    0 },
            { "dukes",     "Imponte Dukes",    0 },
            { "dominator", "Dominator",        0 },
            { "gauntlet",  "Gauntlet",         0 },
            { "sabregt",   "Sabre Turbo",      0 },
            { "ruiner",    "Ruiner",           0 },
            { "monroe",    "Monroe",           0 },
            { "stinger",   "Stinger GT",       0 },
            { "ztype",     "Truffade Z-Type",  0 }
        }
    },
    {
        "Bikes & Off-Road", "Superbikes, dirtbikes & 4x4 trucks", 10,
        {
            { "bati",       "Bati 801",         0 },
            { "hakuchou",   "Hakuchou",         0 },
            { "akuma",      "Akuma",            0 },
            { "double",     "Double T",         0 },
            { "sanchez",    "Sanchez",          0 },
            { "innovation", "Innovation",       0 },
            { "dubsta3",    "Dubsta 6x6",       0 },
            { "sandking",   "Sandking XL",      0 },
            { "marshall",   "Marshall Monster", 0 },
            { "bifta",      "Bifta Buggy",      0 }
        }
    },
    {
        "Emergency & Military", "Tanks, police interceptors & heavy rig", 8,
        {
            { "rhino",     "Rhino Tank",         0 },
            { "police3",   "Police Interceptor", 0 },
            { "police2",   "Police Buffalo",     0 },
            { "sheriff2",  "Sheriff SUV",        0 },
            { "fbi2",      "FIB Granger",        0 },
            { "riot",      "Police Riot",        0 },
            { "firetruk",  "Fire Truck",         0 },
            { "barracks",  "Barracks Semi",      0 }
        }
    },
    {
        "Planes, Helis & Sea", "Fighter jets, attack choppers & subs", 10,
        {
            { "buzzard",      "Buzzard Attack",  0 },
            { "lazer",        "P-996 Lazer Jet", 0 },
            { "besra",        "Western Besra",   0 },
            { "vestra",       "Vestra",          0 },
            { "miljet",       "Miljet",          0 },
            { "titan",        "Titan Cargo",     0 },
            { "luxor",        "Luxor Jet",       0 },
            { "frogger",      "Frogger Heli",    0 },
            { "cargobob",     "Cargobob",        0 },
            { "submersible2", "Kraken Sub",      0 }
        }
    }
};

typedef struct {
    char  name[22];
    float x, y, z;
} TpEntry;

static const TpEntry kTeleports[14] = {
    { "Mount Chiliad",     499.0f,  5592.0f, 796.0f },
    { "Maze Bank Roof",    -75.0f,  -818.9f, 326.2f },
    { "LSIA Runway",     -1336.0f, -3044.0f,  14.0f },
    { "Fort Zancudo",    -2047.0f,  3132.0f,  33.0f },
    { "Sandy Airfield",   1747.0f,  3273.0f,  41.2f },
    { "Del Perro Pier",  -1850.0f, -1232.0f,  13.0f },
    { "Vinewood Sign",     711.4f,  1198.0f, 348.5f },
    { "Grove Street",       95.0f, -1935.0f,  20.8f },
    { "Michael's Mansion", -813.6f,  179.5f,  72.2f },
    { "Franklin's Hills",    7.1f,   536.6f, 176.0f },
    { "Trevor's Trailer", 1972.6f,  3816.8f,  33.4f },
    { "LS Customs",       -356.0f,  -135.0f,  39.0f },
    { "Paleto Bay",       -285.0f,  6226.0f,  31.5f },
    { "Altruist Camp",   -1170.0f,  4926.0f, 224.3f }
};

typedef struct {
    char  label[22];
    char  ipl[24];
    float x, y, z;
    uint8_t is_ny;
} IplEntry;

static const IplEntry kIplInteriors[8] = {
    { "North Yankton",       "prologue01",          3360.2f, -4849.7f, 111.8f, 1 },
    { "Simeon's Showroom",   "shr_int",              -45.3f, -1098.4f,  26.4f, 0 },
    { "FIB Top Lobby",       "FIBlobby",             135.5f,  -749.2f, 258.2f, 0 },
    { "IAA Office",          "",                     117.2f,  -620.0f, 206.0f, 0 },
    { "Vangelico Jewelry",   "jewel2fake",          -630.4f,  -236.7f,  38.0f, 0 },
    { "Pacific Standard",    "Post_Hiest_Lobby",     255.8f,   217.0f, 101.7f, 0 },
    { "Union Depository",    "",                       2.6f,  -669.0f,  16.1f, 0 },
    { "Humane Labs Tunnel",  "",                    3525.4f,  3705.3f,  20.9f, 0 }
};

typedef struct {
    char name[14];
    char label[14];
} WeatherEntry;

static const WeatherEntry kWeathers[15] = {
    { "CLEAR",      "Clear" },
    { "EXTRASUNNY", "Extra Sunny" },
    { "CLOUDS",     "Clouds" },
    { "OVERCAST",   "Overcast" },
    { "RAIN",       "Rain" },
    { "CLEARING",   "Clearing" },
    { "THUNDER",    "Thunderstorm" },
    { "SMOG",       "Smog" },
    { "FOGGY",      "Foggy" },
    { "XMAS",       "Christmas" },
    { "SNOWLIGHT",  "Snow Light" },
    { "BLIZZARD",   "Blizzard" },
    { "SNOW",       "Snow" },
    { "NEUTRAL",    "Neutral" },
    { "HALLOWEEN",  "Halloween" }
};

typedef struct {
    char name[18];
    char label[20];
} CloudHatEntry;

/* First 6 match NVE NVE_PS5_Ready/data/clouds.xml verbatim; remaining 10 are vanilla base hats */
static const CloudHatEntry kCloudHats[16] = {
    { "Cloudy 01",     "Cloudy 01 (NVE)" },
    { "Wispy",         "Wispy (NVE)" },
    { "RAIN",          "Rain Clouds (NVE)" },
    { "horizonband1",  "Horizon 1 (NVE)" },
    { "horizonband2",  "Horizon 2 (NVE)" },
    { "Stripey",       "Stripey (NVE)" },
    { "Clear 01",      "Clear Sky" },
    { "Puffy",         "Puffy White" },
    { "Contrails",     "Contrails" },
    { "Horizon",       "Horizon Mist" },
    { "horizonband3",  "Horizon Band 3" },
    { "Snowy 01",      "Snow Clouds" },
    { "Stormy 01",     "Storm Clouds" },
    { "altostratus",   "Altostratus" },
    { "cirrocumulus",  "Cirrocumulus" },
    { "stratocumulus", "Stratocumulus" }
};

typedef struct {
    char name[20];
    char label[18];
} CloudKeyframeEntry;

/* Exact presets from NVE_PS5_Ready/data/cloudkeyframes.xml */
static const CloudKeyframeEntry kCloudKeyframes[14] = {
    { "default",         "NVE Default" },
    { "SUNNYclouds",     "Sunny Clouds" },
    { "LIGHTclouds",     "Light Clouds" },
    { "MEDIUMclouds",    "Medium Clouds" },
    { "OVERCASTclouds",  "Overcast" },
    { "HEAVYclouds",     "Heavy Overcast" },
    { "CONTRAILSclouds", "Contrails" },
    { "RAINclouds",      "Rain Clouds" },
    { "STORMclouds",     "Storm Clouds" },
    { "POSTRAINclouds",  "Post-Rain" },
    { "FOGGYclouds",     "Foggy Sky" },
    { "SMOGclouds",      "Smoggy Sky" },
    { "HALLOWEENclouds", "Halloween" },
    { "SNOWclouds",      "Snow Clouds" }
};

typedef struct {
    char    mod_name[28];
    char    label[24];
    char    desc[52];
    uint8_t default_pct;
} GfxPresetEntry;

static const GfxPresetEntry kGfxPresets[15] = {
    { "OFF",                 "OFF (Original NVE)",  "Restores your installed NVE / original graphics",  100 },
    { "MODHEN_QUANTV_ENB",   "QuantV Photoreal ENB","QuantV 1:1 photoreal filmic, 500m refl & GI",      100 },
    { "MODHEN_QUANTV_SUN",   "QuantV California Sun","QuantV golden hour sun & rich sky bounce",         90 },
    { "MODHEN_QUANTV_NIGHT", "QuantV Midnight Neon","QuantV midnight neon gloss & car refl",             90 },
    { "MODHEN_NVE_ENB",      "Calibrated PS5 ENB",  "Crisp filmic curve, wide bloom & glossy paint",    100 },
    { "MODHEN_HDR_BLOOM",    "HDR Shadow Bloom",    "Wide dreamy light halos & glowing streetlamps",     85 },
    { "MODHEN_MIRROR_REFL",  "Mirror Car Reflect",  "Maximum dynamic cubemap sheen & crisp rim gloss",  100 },
    { "MODHEN_DARK_FILMIC",  "Dark Filmic Noir",    "Deep blacks, punchy highlights & moody contrast",   80 },
    { "MODHEN_STORM_MOOD",   "Storm & Rain Pop",    "Rich dark storm clouds, wet road glow & contrast",  90 },
    { "MODHEN_MIAMI_SUNSET", "Miami Vice Sunset",   "Warm amber sky tint, sunset bloom & mirror paint",  85 },
    { "MODHEN_VIVID_COLOR",  "Vivid Photoreal",     "Crisp clarity, clean sky punch & balanced color",   80 },
    { "MODHEN_SOFT_AO",      "Deep Shadow & AO",    "Enhanced ambient occlusion & crisp sun shadows",    85 },
    { "MODHEN_CLEAR_FOG",    "Clear Haze Remover",  "Cuts milky distant fog for razor-sharp horizon",    85 },
    { "MODHEN_COOL_FROST",   "Crisp Cool Tones",    "Clean glacial contrast with bright highlights",     75 },
    { "MODHEN_NATURAL_GI",   "Natural Ambient GI",  "Balanced sky bounce light & soft realistic fill",   85 }
};

typedef struct {
    char fx_name[24];
    char label[20];
} PostFxEntry;

static const PostFxEntry kPostFxPresets[7] = {
    { "OFF",             "None (Off)" },
    { "PPFilter",        "Cross-Process Lens" },
    { "MenuMGHeistTint", "Heist Gold Tint" },
    { "SwitchHUDIn",     "Chromatic Pulse" },
    { "DefaultFlash",    "Soft Bloom Flash" },
    { "ChopVision",      "High-Vis Contrast" },
    { "DMT_flight",      "Neon Dreamscape" }
};

enum ColorCategory {
    COLOR_CAT_CLASSIC = 0,
    COLOR_CAT_MATTE   = 1,
    COLOR_CAT_METALS  = 2,
    COLOR_CAT_PEARL   = 3,
    COLOR_CAT_MAX     = 4
};

static const char kColorCatNames[4][24] = {
    "Classic & Metallic",
    "Matte Finishes",
    "Metals & Chrome",
    "Pearlescent & Specials"
};

typedef struct {
    char    name[20];
    uint8_t cat;
    uint8_t id;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ColorPreset;

static const ColorPreset kColorPresets[52] = {
    /* Classic & Metallic */
    { "Metallic Black",    0,   0,  13,  17,  22 },
    { "Graphite",          0,   1,  50,  56,  61 },
    { "Dark Silver",       0,   3, 105, 110, 114 },
    { "Silver",            0,   4, 151, 154, 156 },
    { "Ice White",         0, 111, 240, 240, 240 },
    { "Formula Red",       0,  27, 200,  16,  16 },
    { "Torino Red",        0,  28, 160,  10,  10 },
    { "Sunset Orange",     0,  36, 245, 100,  20 },
    { "Bright Orange",     0,  38, 255, 130,   0 },
    { "Racing Yellow",     0,  88, 255, 215,   0 },
    { "Dew Yellow",        0,  89, 230, 220,  60 },
    { "Bright Green",      0,  53,  15, 160,  50 },
    { "Lime Green",        0,  55, 102, 204,  51 },
    { "Dark Blue",         0,  64,  20,  36,  65 },
    { "Diamond Blue",      0,  67, 212, 235, 242 },
    { "Ultra Blue",        0,  70,   0, 102, 230 },
    { "Racing Blue",       0,  73,  35,  82, 138 },
    { "Bright Purple",     0, 145, 130,  20, 180 },
    { "Hot Pink",          0, 135, 255,  60, 150 },
    { "Salmon Pink",       0, 136, 240, 130, 130 },

    /* Matte Finishes */
    { "Matte Black",       1,  12,  20,  20,  20 },
    { "Matte Gray",        1,  13,  60,  63,  65 },
    { "Matte Light Gray",  1,  14, 115, 118, 120 },
    { "Matte White",       1, 131, 230, 230, 230 },
    { "Matte Red",         1,  39, 170,  25,  25 },
    { "Matte Dark Red",    1,  40, 115,  20,  20 },
    { "Matte Orange",      1,  41, 240, 110,  20 },
    { "Matte Yellow",      1,  42, 255, 210,  30 },
    { "Matte Lime",        1,  55, 100, 190,  40 },
    { "Matte Green",       1, 128,  30,  85,  45 },
    { "Matte Forest",      1, 151,  35,  55,  40 },
    { "Matte Olive",       1, 152,  65,  70,  45 },
    { "Matte Dark Blue",   1,  82,  25,  35,  65 },
    { "Matte Blue",        1,  83,  40,  70, 140 },
    { "Matte Midnight",    1,  84,  18,  24,  45 },
    { "Matte Purple",      1, 148,  90,  30, 110 },
    { "Matte Desert Tan",  1, 153, 145, 130, 105 },

    /* Metals & Chrome */
    { "Chrome",            2, 120, 220, 225, 230 },
    { "Brushed Steel",     2, 117, 120, 125, 130 },
    { "Brushed BlackSteel",2, 118,  55,  60,  65 },
    { "Brushed Aluminium", 2, 119, 160, 165, 170 },
    { "Pure Gold",         2, 158, 225, 185,  85 },
    { "Brushed Gold",      2, 159, 190, 150,  65 },
    { "Bronze Gold",       2,  99, 170, 130,  45 },

    /* Pearlescent & Specials */
    { "Carbon Black",      3, 147,  26,  26,  26 },
    { "Lava Red",          3, 150, 180,  40,  20 },
    { "Midnight Purple",   3, 142,  35,  10,  50 },
    { "Lime Green Pearl",  3,  92, 120, 220,  20 },
    { "Ultra Blue Pearl",  3,  62,  15,  75, 180 },
    { "Yellow Pearl",      3,  91, 255, 230,  40 },
    { "Pink Pearl",        3, 137, 245, 100, 160 },
    { "Diamond Pearl",     3,  68, 180, 220, 240 }
};

typedef struct {
    char    name[16];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} NeonColorPreset;

static const NeonColorPreset kNeonColors[9] = {
    { "Pure White",    255, 255, 255 },
    { "Electric Blue",   0, 100, 255 },
    { "Mint Green",      0, 255, 140 },
    { "Lime Green",    100, 255,   0 },
    { "Golden Yellow", 255, 215,   0 },
    { "Sunset Orange", 255,  80,   0 },
    { "Hot Pink",      255,  20, 147 },
    { "Neon Purple",   160,   0, 255 },
    { "Blacklight UV",  50,   0, 180 }
};

typedef struct {
    char    name[16];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} SmokeColorPreset;

static const SmokeColorPreset kSmokeColors[9] = {
    { "White",  255, 255, 255 },
    { "Black",   20,  20,  20 },
    { "Blue",     0, 100, 255 },
    { "Yellow", 255, 220,   0 },
    { "Orange", 255, 100,   0 },
    { "Red",    230,  20,  20 },
    { "Green",   20, 220,  40 },
    { "Purple", 150,  20, 230 },
    { "Pink",   255,  50, 160 }
};

typedef struct {
    char    name[18];
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ThemePreset;

static const ThemePreset kThemePresets[6] = {
    { "Electric Cyan",   0, 215, 255 },
    { "Neon Purple",   190,  60, 255 },
    { "Emerald Green",  40, 235, 120 },
    { "Crimson Red",   255,  60,  75 },
    { "Sunset Gold",   255, 195,  30 },
    { "Ice White",     240, 245, 255 }
};

typedef struct {
    uint8_t mode_val;
    char    ped_label[16];
    char    veh_label[16];
} CamViewOption;

/* Valid GTA V eCamViewMode values only (mode 3 is cinematic/invalid, handled by SET_CINEMATIC_MODE_ACTIVE) */
static const CamViewOption kCamViewOptions[4] = {
    { 0, "Third Close",  "Third Close" },
    { 1, "Third Medium", "Third Medium" },
    { 2, "Third Far",    "Third Far" },
    { 4, "First Person", "First / Cockpit" }
};

typedef struct {
    uint8_t slot;
    char    name[16];
} LscSlotDef;

static const LscSlotDef kLscBodySlots[11] = {
    { 0,  "Spoiler" },
    { 1,  "Front Bumper" },
    { 2,  "Rear Bumper" },
    { 3,  "Side Skirt" },
    { 4,  "Exhaust" },
    { 5,  "Roll Cage" },
    { 6,  "Grille" },
    { 7,  "Hood" },
    { 8,  "Left Fender" },
    { 10, "Roof" },
    { 23, "Front Wheels" }
};

static const char kColorTargetNames[5][14] = { "Both", "Primary", "Secondary", "Pearlescent", "Wheels" };
static const char kWindowTintNames[6][14]  = { "Stock", "Pure Black", "Dark Smoke", "Light Smoke", "Stock Green", "Limo Black" };
static const char kEngineNames[5][12]      = { "Stock", "Level 1", "Level 2", "Level 3", "Level 4" };
static const char kBrakesNames[4][12]      = { "Stock", "Street", "Sports", "Race" };
static const char kTransNames[4][12]       = { "Stock", "Street", "Sports", "Race" };
static const char kSuspNames[5][14]        = { "Stock", "Lowered", "Street", "Sport", "Competition" };
static const char kArmorNames[6][12]       = { "Stock", "20%", "40%", "60%", "80%", "100%" };
static const char kMenuPosNames[3][14]     = { "Left", "Right", "Center" };
static const char kMenuScaleNames[3][10]   = { "Compact", "Normal", "Large" };
static const char kSpeedoNames[3][10]      = { "OFF", "MPH", "KPH" };

static const float kFovLevels[11]    = { 0.0f, 65.0f, 70.0f, 75.0f, 80.0f, 85.0f, 90.0f, 95.0f, 100.0f, 105.0f, 110.0f };
static const char  kFovLabels[11][12]= { "Default", "65 deg", "70 deg", "75 deg", "80 deg", "85 deg", "90 deg", "95 deg", "100 deg", "105 deg", "110 deg" };

static const float kMultSpeeds[3]    = { 1.0f, 1.25f, 1.49f };
static const char  kMultLabels[3][8] = { "1.0x", "1.25x", "1.49x" };

static const char  kAirbreakNames[4][10]= { "Slow", "Normal", "Fast", "Turbo" };
static const float kAirbreakSpeeds[4]   = { 0.35f, 1.15f, 3.00f, 7.50f };

static const char  kTimeScaleNames[4][10] = { "1.0x", "0.75x", "0.5x", "0.25x" };
static const float kTimeScales[4]         = { 1.0f, 0.75f, 0.5f, 0.25f };

static const float kTorqueMults[4]     = { 1.0f, 2.0f, 5.0f, 10.0f };
static const char  kTorqueLabels[4][8] = { "1.0x", "2.0x", "5.0x", "10.0x" };

static const uint32_t kWeaponHashes[15] = {
    0x1B06D571U, 0x5EF9FEC4U, 0x22D8FE39U, 0x2BE6766BU, 0xBFEFFF6DU,
    0x83BF0278U, 0xAF113F99U, 0x9D07F764U, 0x0C472FE2U, 0xB1CA77B1U,
    0x42BF8A85U, 0xA284510BU, 0x2C3731D9U, 0x99B507EAU, 0xFBAB5776U
};

/* ============================================================================
 * Alternate First Person (PC ASI Mod Port) Tables
 * ============================================================================ */
static const char kAfpModeNames[6][24] = {
    "OFF",
    "Alt First-Person",
    "Tactical Bodycam",
    "Shoulder GoPro",
    "Dash Cam",
    "Helmet Cam"
};

static const float kAfpFovValues[8] = {
    75.0f, 80.0f, 85.0f, 90.0f, 95.0f, 100.0f, 105.0f, 110.0f
};

static const char kAfpFovNames[8][12] = {
    "75 deg", "80 deg", "85 deg", "90 deg", "95 deg", "100 deg", "105 deg", "110 deg"
};

static const char kAfpBobNames[4][18] = {
    "OFF", "Subtle (0.5x)", "Normal (1.0x)", "Cinematic (1.8x)"
};

static const char kAfpDriftNames[4][18] = {
    "OFF", "Subtle (0.50)", "Bodycam (0.85)", "Heavy (0.92)"
};

static const char kAfpRecoilNames[4][18] = {
    "OFF", "Subtle (0.5x)", "Standard (1.0x)", "Cinematic (2.0x)"
};

static const char kAfpAdsNames[4][14] = {
    "OFF", "15 deg Zoom", "25 deg Zoom", "35 deg Zoom"
};

static const float kAfpAdsZoomDeg[4] = {
    0.0f, 15.0f, 25.0f, 35.0f
};

static const char kAfpCrosshairNames[3][16] = {
    "OFF", "Always", "Only When ADS"
};

#endif /* ORBX_MENU_TABLES_H */
