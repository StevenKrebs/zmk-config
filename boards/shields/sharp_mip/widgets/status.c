/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include "../os_mode.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#include "../caps_word_signal.h"
#include "../smart_shift.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static bool caps_word_active;
static bool caps_lock_active;
static bool globe_active;
static bool smart_shift_disarmed;

#define MODIFIER_OUTPUT_WIDTH MODIFIER_SOURCE_HEIGHT

extern const uint8_t sharp_mip_control_bitmap[28];
extern const uint8_t sharp_mip_shift_bitmap[28];
extern const uint8_t sharp_mip_command_bitmap[28];
extern const uint8_t sharp_mip_option_bitmap[28];
extern const uint8_t sharp_mip_alt_bitmap[28];
extern const uint8_t sharp_mip_windows_bitmap[28];
extern const lv_font_t sharp_mip_caps_lock_font;

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_label_dsc_t profile_label_dsc;
    init_label_dsc(&profile_label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14,
                   LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t profile_active_label_dsc;
    init_label_dsc(&profile_active_label_dsc, LVGL_BACKGROUND, &lv_font_montserrat_14,
                   LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    char output_text[10] = {};

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            if (state->active_profile_connected) {
                strcat(output_text, LV_SYMBOL_WIFI);
            } else {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    // Draw the five Bluetooth profiles below the connection status.
    for (int i = 0; i < 5; i++) {
        int x = 4 + i * 12;
        bool active = i == state->active_profile_index;
        if (active) {
            lv_canvas_draw_rect(canvas, x, 24, 12, 16, &rect_white_dsc);
        }
        char profile_text[2] = {};
        snprintf(profile_text, sizeof(profile_text), "%d", i + 1);
        lv_canvas_draw_text(canvas, x, 24, 12,
                            active ? &profile_active_label_dsc : &profile_label_dsc,
                            profile_text);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void rotate_modifier_canvas(struct zmk_widget_status *widget) {
    for (uint16_t y = 0; y < MODIFIER_SOURCE_HEIGHT; y++) {
        for (uint16_t x = 0; x < CANVAS_SIZE; x++) {
#ifdef CONFIG_SHARP_MIP_ROTATE_180
            widget->cbuf3[(CANVAS_SIZE - 1 - x) * MODIFIER_OUTPUT_WIDTH + y] =
                widget->cbuf2[y * CANVAS_SIZE + x];
#else
            widget->cbuf3[x * MODIFIER_OUTPUT_WIDTH + MODIFIER_OUTPUT_WIDTH - 1 - y] =
                widget->cbuf2[y * CANVAS_SIZE + x];
#endif
        }
    }
    lv_obj_invalidate(widget->modifier_canvas);
}

static void draw_caps_lock_glyph(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y,
                                 lv_color_t color) {
    lv_font_glyph_dsc_t glyph;
    if (!lv_font_get_glyph_dsc(&sharp_mip_caps_lock_font, &glyph, 0x21EA, 0)) {
        return;
    }

    const uint8_t *bitmap = lv_font_get_glyph_bitmap(&sharp_mip_caps_lock_font, 0x21EA);
    if (bitmap == NULL) {
        return;
    }

    lv_draw_rect_dsc_t pixel_dsc;
    init_rect_dsc(&pixel_dsc, color);
    for (uint8_t row = 0; row < glyph.box_h; row++) {
        for (uint8_t col = 0; col < glyph.box_w; col++) {
            if (bitmap[row * glyph.box_w + col] != 0) {
                lv_canvas_draw_rect(canvas, x + col, y + row, 1, 1, &pixel_dsc);
            }
        }
    }
}

static void draw_middle(struct zmk_widget_status *widget, const struct status_state *state) {
    lv_obj_t *canvas = widget->modifier_source;

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, MODIFIER_SOURCE_HEIGHT, &rect_black_dsc);

    // Draw the active layer at the top of the middle canvas.
    if (state->layer_label == NULL || strlen(state->layer_label) == 0) {
        char text[10] = {};
        snprintf(text, sizeof(text), "LAYER %i", state->layer_index);
        lv_canvas_draw_text(canvas, 0, 1, CANVAS_SIZE, &label_dsc, text);
    } else {
        lv_canvas_draw_text(canvas, 0, 1, CANVAS_SIZE, &label_dsc, state->layer_label);
    }

    // Draw the modifier indicators below the layer label.
    bool windows_mode = sharp_mip_os_windows_mode();
    struct modifier_cell {
        const uint8_t *bitmap;
        uint8_t y;
        uint8_t glyph_x;
        zmk_mod_flags_t mask;
    } cells[] = {
        {windows_mode ? sharp_mip_alt_bitmap : sharp_mip_option_bitmap,
         23, 12, MOD_LALT | MOD_RALT},
        {windows_mode ? sharp_mip_control_bitmap : sharp_mip_command_bitmap, 23, 40,
         windows_mode ? MOD_LCTL | MOD_RCTL : MOD_LGUI | MOD_RGUI},
        {sharp_mip_shift_bitmap, 48, 12, MOD_LSFT | MOD_RSFT},
        {windows_mode ? sharp_mip_windows_bitmap : sharp_mip_control_bitmap, 48, 40,
         windows_mode ? MOD_LGUI | MOD_RGUI : MOD_LCTL | MOD_RCTL},
    };

    for (size_t i = 0; i < ARRAY_SIZE(cells); i++) {
        zmk_mod_flags_t active_modifiers = state->modifiers | state->virtual_modifiers;
        if (smart_shift_disarmed) {
            active_modifiers &= ~(MOD_LSFT | MOD_RSFT);
        }
        bool active = (active_modifiers & cells[i].mask) != 0;
        if (active) {
            // Expand the background upward and downward around the fixed glyph position.
            lv_canvas_draw_rect(canvas, cells[i].glyph_x - 7, cells[i].y - 3, 28, 25,
                                &rect_white_dsc);
        }
        lv_draw_rect_dsc_t glyph_dsc;
        init_rect_dsc(&glyph_dsc, active ? LVGL_BACKGROUND : LVGL_FOREGROUND);
        for (uint8_t row = 0; row < 14; row++) {
            for (uint8_t col = 0; col < 14; col++) {
                if (cells[i].bitmap[row * 2 + col / 8] & (0x80 >> (col % 8))) {
                    lv_canvas_draw_rect(canvas, cells[i].glyph_x + col, cells[i].y + 2 + row,
                                        1, 1, &glyph_dsc);
                }
            }
        }
    }

    // Keep Caps Lock and the macOS Fn indicator in the third row.
    bool caps_active = caps_word_active || caps_lock_active;
    if (caps_active) {
        lv_canvas_draw_rect(canvas, 5, 70, 28, 22, &rect_white_dsc);
    }
    draw_caps_lock_glyph(canvas, 12, 73,
                         caps_active ? LVGL_BACKGROUND : LVGL_FOREGROUND);
    if (!windows_mode) {
        if (globe_active) {
            lv_canvas_draw_rect(canvas, 33, 70, 28, 22, &rect_white_dsc);
        }
        lv_draw_label_dsc_t fn_label_dsc = label_dsc;
        fn_label_dsc.color = globe_active ? LVGL_BACKGROUND : LVGL_FOREGROUND;
        fn_label_dsc.letter_space = -1;
        lv_canvas_draw_text(canvas, 33, 73, 28, &fn_label_dsc, "fn");
    }

    rotate_modifier_canvas(widget);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_middle(widget, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

struct modifier_status_state {
    zmk_mod_flags_t modifiers;
};

static void modifier_status_update_cb(struct modifier_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.modifiers = state.modifiers;
        draw_middle(widget, &widget->state);
    }
}

static struct modifier_status_state modifier_status_get_state(const zmk_event_t *eh) {
    return (struct modifier_status_state){.modifiers = zmk_hid_get_explicit_mods()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_modifier_status, struct modifier_status_state,
                            modifier_status_update_cb, modifier_status_get_state)
ZMK_SUBSCRIPTION(widget_modifier_status, zmk_keycode_state_changed);

static void refresh_caps_status(void) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_middle(widget, &widget->state);
    }
}

static int caps_word_signal_listener(const zmk_event_t *eh) {
    const struct zmk_caps_word_signal *ev = as_zmk_caps_word_signal(eh);
    if (ev == NULL || !ev->toggled) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    caps_word_active = !caps_word_active;
    refresh_caps_status();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_caps_word, caps_word_signal_listener);
ZMK_SUBSCRIPTION(widget_caps_word, zmk_caps_word_signal);

static int os_mode_listener(const zmk_event_t *eh) {
    (void)eh;
    refresh_caps_status();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_os_mode, os_mode_listener);
ZMK_SUBSCRIPTION(widget_os_mode, zmk_os_mode_changed);

static int smart_shift_status_listener(const zmk_event_t *eh) {
    const struct zmk_smart_shift_state_changed *ev =
        as_zmk_smart_shift_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    smart_shift_disarmed = ev->disarmed;
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.virtual_modifiers = ev->armed ? MOD_LSFT : 0;
        draw_middle(widget, &widget->state);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_smart_shift_status, smart_shift_status_listener);
ZMK_SUBSCRIPTION(widget_smart_shift_status, zmk_smart_shift_state_changed);

static int caps_lock_keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    bool refresh = false;

    if (ev != NULL && ev->state && ev->usage_page == HID_USAGE_KEY &&
        ev->keycode == HID_USAGE_KEY_KEYBOARD_CAPS_LOCK) {
        caps_lock_active = !caps_lock_active;
        refresh = true;
    }

    // GLOBE is a consumer usage; mirror the held state on the Fn indicator.
    if (ev != NULL && ev->usage_page == 0x0C && ev->keycode == 0x029D) {
        globe_active = ev->state;
        refresh = true;
    }

    if (ev != NULL && ev->state && caps_word_active && ev->usage_page == HID_USAGE_KEY &&
        !is_mod(ev->usage_page, ev->keycode) &&
        !(ev->keycode >= HID_USAGE_KEY_KEYBOARD_A &&
          ev->keycode <= HID_USAGE_KEY_KEYBOARD_Z) &&
        !(ev->keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
          ev->keycode <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) &&
        ev->keycode != HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE &&
        ev->keycode != HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE &&
        ev->keycode != HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD) {
        caps_word_active = false;
        refresh = true;
    }

    if (refresh) {
        refresh_caps_status();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_caps_lock_status, caps_lock_keycode_listener);
ZMK_SUBSCRIPTION(widget_caps_lock_status, zmk_keycode_state_changed);

#ifdef CONFIG_SHARP_MIP_ROTATE_180 // sets positions for default and flipped canvases
int top_pos = 0;
int middle_pos = 68;
#else
int top_pos = 92;
int middle_pos = 0;
#endif

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_set_style_bg_color(widget->obj, LVGL_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_LEFT, top_pos, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    widget->modifier_canvas = lv_canvas_create(widget->obj);
    lv_obj_align(widget->modifier_canvas, LV_ALIGN_TOP_LEFT, middle_pos, 0);
    lv_canvas_set_buffer(widget->modifier_canvas, widget->cbuf3, MODIFIER_OUTPUT_WIDTH,
                         CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    widget->modifier_source = lv_canvas_create(widget->obj);
    lv_obj_add_flag(widget->modifier_source, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_buffer(widget->modifier_source, widget->cbuf2, CANVAS_SIZE,
                         MODIFIER_SOURCE_HEIGHT, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
