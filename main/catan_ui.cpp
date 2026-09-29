#include <lvgl.h>
#include <cstdio>

namespace {

constexpr unsigned RED   = 0x921B24;
constexpr unsigned GOLD  = 0xE8BB63;
constexpr unsigned CREAM = 0xFFF3D6;

enum class SetupStage {
    Welcome,
    BoardSelection
};

SetupStage stage = SetupStage::Welcome;

lv_obj_t* welcomeScreen = nullptr;
lv_obj_t* boardScreen = nullptr;

// Uses a larger font if it is enabled in lv_conf.h.
const lv_font_t* headingFont()
{
#if LV_FONT_MONTSERRAT_48
    return &lv_font_montserrat_48;
#elif LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#else
    return LV_FONT_DEFAULT;
#endif
}

lv_obj_t* makeScreen()
{
    lv_obj_t* screen = lv_obj_create(nullptr);

    lv_obj_set_style_bg_color(screen, lv_color_hex(RED), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    return screen;
}

lv_obj_t* addLabel(
    lv_obj_t* parent,
    const char* text,
    int y,
    unsigned color,
    const lv_font_t* font = LV_FONT_DEFAULT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);

    lv_obj_set_width(label, 720);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);

    return label;
}

void startClicked(lv_event_t*)
{
    if (stage != SetupStage::Welcome) {
        return;
    }

    stage = SetupStage::BoardSelection;
    lv_screen_load(boardScreen);
    std::puts("START: entering board selection.");
}

void backClicked(lv_event_t*)
{
    stage = SetupStage::Welcome;
    lv_screen_load(welcomeScreen);
    std::puts("BACK: returning to welcome.");
}

void addButton(
    lv_obj_t* parent,
    const char* text,
    int y,
    lv_event_cb_t callback)
{
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_size(button, 300, 76);
    lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);

    lv_obj_set_style_bg_color(button, lv_color_hex(GOLD), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);

    lv_obj_set_style_bg_color(
        button, lv_color_hex(0xC99840), LV_STATE_PRESSED);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(RED), 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(
        button, callback, LV_EVENT_CLICKED, nullptr);
}

} // namespace

// C linkage allows main.c to call this C++ function.
extern "C" void catan_ui_init(void)
{
    // Create both screens once so navigation doesn't keep allocating them.
    if (welcomeScreen != nullptr) {
        stage = SetupStage::Welcome;
        lv_screen_load(welcomeScreen);
        return;
    }

    welcomeScreen = makeScreen();
    boardScreen = makeScreen();

    addLabel(welcomeScreen, "TEAM 27  |  DIGITAL TRADING", 35, GOLD);
    addLabel(welcomeScreen, "CATAN", 115, GOLD, headingFont());
    addLabel(welcomeScreen, "Welcome to game night.", 195, CREAM);
    addLabel(welcomeScreen, "Build. Trade. Settle.", 235, CREAM);

    addButton(welcomeScreen, "START GAME", 300, startClicked);

    addLabel(
        welcomeScreen,
        "3-4 players  |  Next: choose your board",
        415,
        CREAM);

    addLabel(boardScreen, "INITIAL GAME SETUP", 35, GOLD);
    addLabel(
        boardScreen,
        "Choose your board",
        120,
        GOLD,
        headingFont());

    addLabel(
        boardScreen,
        "Board selection will be added here.",
        215,
        CREAM);

    addLabel(
        boardScreen,
        "No board loaded yet.",
        250,
        CREAM);

    addButton(boardScreen, "BACK", 320, backClicked);

    lv_screen_load(welcomeScreen);
}