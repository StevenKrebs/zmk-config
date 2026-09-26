/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_smart_shift

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <drivers/behavior.h>

#include <errno.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keys.h>

#include "smart_shift.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

ZMK_EVENT_IMPL(zmk_smart_shift_state_changed);

#define SMART_SHIFT_STATE_HISTORY 6
#define SMART_SHIFT_TIMEOUT_MS 5000

enum sentence_state {
    SENTENCE_INIT,
    SENTENCE_WORD,
    SENTENCE_ABBREVIATION,
    SENTENCE_ENDING,
    SENTENCE_PRIMED,
};

static bool smart_shift_enabled = DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT);
static bool smart_shift_disarmed;
static bool smart_shift_masked_letter;
static uint16_t smart_shift_masked_page;
static uint32_t smart_shift_masked_keycode;
static enum sentence_state sentence_state;
static enum sentence_state state_history[SMART_SHIFT_STATE_HISTORY];
static uint8_t state_history_count;
static int64_t last_key_timestamp;

#if IS_ENABLED(CONFIG_SETTINGS)
static int smart_shift_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                    void *cb_arg) {
    if (strcmp(name, "enabled") != 0) {
        return -ENOENT;
    }

    uint8_t saved_enabled;
    if (len != sizeof(saved_enabled)) {
        return -EINVAL;
    }
    int ret = read_cb(cb_arg, &saved_enabled, sizeof(saved_enabled));
    if (ret < 0) {
        return ret;
    }
    if (ret != sizeof(saved_enabled) || saved_enabled > 1) {
        return -EINVAL;
    }

    smart_shift_enabled = saved_enabled != 0;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(smart_shift, "smart_shift", NULL, smart_shift_settings_set, NULL,
                               NULL);
#endif

static void set_sentence_state(enum sentence_state state) {
    sentence_state = state;
    if (state != SENTENCE_PRIMED) {
        smart_shift_disarmed = false;
    }
    bool armed = smart_shift_enabled && state == SENTENCE_PRIMED && !smart_shift_disarmed;
    static bool previous_armed;
    static bool previous_disarmed;
    if (armed != previous_armed || smart_shift_disarmed != previous_disarmed) {
        previous_armed = armed;
        previous_disarmed = smart_shift_disarmed;
        raise_zmk_smart_shift_state_changed(
            (struct zmk_smart_shift_state_changed){
                .armed = armed,
                .disarmed = smart_shift_disarmed,
            });
    }
}

static void clear_sentence_state(void) {
    set_sentence_state(SENTENCE_INIT);
    state_history_count = 0;
}

static void push_state(void) {
    if (state_history_count < SMART_SHIFT_STATE_HISTORY) {
        state_history_count++;
    }
    for (int i = state_history_count - 1; i > 0; i--) {
        state_history[i] = state_history[i - 1];
    }
    state_history[0] = sentence_state;
}

static void rewind_state(void) {
    if (state_history_count == 0) {
        set_sentence_state(SENTENCE_INIT);
        return;
    }
    set_sentence_state(state_history[0]);
    for (int i = 0; i + 1 < state_history_count; i++) {
        state_history[i] = state_history[i + 1];
    }
    state_history_count--;
}

static bool is_letter(uint16_t page, uint32_t keycode) {
    return page == HID_USAGE_KEY && keycode >= HID_USAGE_KEY_KEYBOARD_A &&
           keycode <= HID_USAGE_KEY_KEYBOARD_Z;
}

static bool is_quote(uint16_t page, uint32_t keycode) {
    return page == HID_USAGE_KEY &&
           keycode == HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE;
}

static bool is_sentence_ending(const struct zmk_keycode_state_changed *ev) {
    if (ev->usage_page != HID_USAGE_KEY) {
        return false;
    }

    bool shifted = ((ev->implicit_modifiers | zmk_hid_get_explicit_mods()) &
                    (MOD_LSFT | MOD_RSFT)) != 0;

    return (ev->keycode == HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN && !shifted) ||
           (ev->keycode == HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK && shifted) ||
           (ev->keycode == HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && shifted);
}

static int smart_shift_keycode_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || !smart_shift_enabled) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state) {
        if (smart_shift_masked_letter && ev->usage_page == smart_shift_masked_page &&
            ev->keycode == smart_shift_masked_keycode) {
            smart_shift_masked_letter = false;
            zmk_hid_masked_modifiers_clear();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (last_key_timestamp != 0 &&
        ev->timestamp - last_key_timestamp > SMART_SHIFT_TIMEOUT_MS) {
        clear_sentence_state();
    }
    last_key_timestamp = ev->timestamp;

    // Pressing Shift while Smart Shift is armed opts out of the pending automatic
    // capitalization. If held through the next letter, it is masked for that letter.
    if (is_mod(ev->usage_page, ev->keycode)) {
        bool shift_key = ev->keycode == HID_USAGE_KEY_KEYBOARD_LEFTSHIFT ||
                         ev->keycode == HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT;
        if (sentence_state == SENTENCE_PRIMED && shift_key) {
            smart_shift_disarmed = true;
            set_sentence_state(sentence_state);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->usage_page == HID_USAGE_KEY &&
        ev->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        rewind_state();
        return ZMK_EV_EVENT_BUBBLE;
    }

    zmk_mod_flags_t mods = ev->implicit_modifiers | zmk_hid_get_explicit_mods();
    if ((mods & ~(MOD_LSFT | MOD_RSFT)) != 0) {
        clear_sentence_state();
        return ZMK_EV_EVENT_BUBBLE;
    }

    enum sentence_state next_state = SENTENCE_INIT;

    if (is_quote(ev->usage_page, ev->keycode)) {
        // Quotes can surround terminal punctuation; keep the state and record
        // the key so Backspace rewinds the quote before the punctuation.
        next_state = sentence_state;
    } else if (is_letter(ev->usage_page, ev->keycode)) {
        if (sentence_state == SENTENCE_PRIMED) {
            if (smart_shift_disarmed && (mods & (MOD_LSFT | MOD_RSFT)) != 0) {
                // Shift is the opt-out gesture, so mask the held physical Shift
                // for this one letter as well as cancelling Smart Shift.
                zmk_hid_masked_modifiers_set(MOD_LSFT | MOD_RSFT);
                smart_shift_masked_letter = true;
                smart_shift_masked_page = ev->usage_page;
                smart_shift_masked_keycode = ev->keycode;
            } else if (!smart_shift_disarmed && (mods & (MOD_LSFT | MOD_RSFT)) == 0) {
                // ZMK's Caps Word uses this same event field to add Shift.
                ev->implicit_modifiers |= MOD_LSFT;
            }
        }
        next_state = (sentence_state == SENTENCE_ENDING ||
                      sentence_state == SENTENCE_ABBREVIATION)
                         ? SENTENCE_ABBREVIATION
                         : SENTENCE_WORD;
    } else if (ev->usage_page == HID_USAGE_KEY &&
               ev->keycode == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
        if (sentence_state == SENTENCE_ENDING || sentence_state == SENTENCE_PRIMED) {
            next_state = SENTENCE_PRIMED;
        }
    } else if (is_sentence_ending(ev)) {
        // A contiguous run such as "!!", "!?", or "?!" is still one
        // sentence ending and should arm after the following space.
        next_state = (sentence_state == SENTENCE_WORD ||
                      sentence_state == SENTENCE_ENDING)
                         ? SENTENCE_ENDING
                         : SENTENCE_ABBREVIATION;
    } else if (ev->usage_page == HID_USAGE_KEY &&
               ev->keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
               ev->keycode <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        // Digits count as word characters before sentence punctuation.
        next_state = SENTENCE_WORD;
    } else {
        clear_sentence_state();
        return ZMK_EV_EVENT_BUBBLE;
    }

    push_state();
    set_sentence_state(next_state);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(smart_shift, smart_shift_keycode_listener);
ZMK_SUBSCRIPTION(smart_shift, zmk_keycode_state_changed);

static int smart_shift_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    smart_shift_enabled = !smart_shift_enabled;
    clear_sentence_state();
    last_key_timestamp = 0;
#if IS_ENABLED(CONFIG_SETTINGS)
    uint8_t saved_enabled = smart_shift_enabled;
    int ret = settings_save_one("smart_shift/enabled", &saved_enabled, sizeof(saved_enabled));
    if (ret < 0) {
        LOG_ERR("Failed to save Smart Shift state (%d)", ret);
    }
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

#else

static int smart_shift_pressed(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    return ZMK_BEHAVIOR_OPAQUE;
}

#endif

static int smart_shift_released(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api smart_shift_api = {
    .binding_pressed = smart_shift_pressed,
    .binding_released = smart_shift_released,
};

#define SMART_SHIFT_INST(n)                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &smart_shift_api);

DT_INST_FOREACH_STATUS_OKAY(SMART_SHIFT_INST)
