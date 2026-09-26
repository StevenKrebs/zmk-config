#define DT_DRV_COMPAT zmk_behavior_os_mode

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keys.h>

#include "os_mode.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(zmk_os_mode_changed);

static bool windows_mode_by_profile[ZMK_BLE_PROFILE_COUNT];
// OS-specific behaviors already choose their modifiers, so don't swap them again.
static bool emitting_os_keycode;

#if IS_ENABLED(CONFIG_SETTINGS) && \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
static int os_mode_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                               void *cb_arg) {
    if (strncmp(name, "profile_", 8) != 0) {
        return -ENOENT;
    }

    char *end;
    unsigned long profile = strtoul(name + 8, &end, 10);
    if (end == name + 8 || *end != '\0' || profile >= ZMK_BLE_PROFILE_COUNT) {
        return -EINVAL;
    }

    uint8_t saved_mode;
    if (len != sizeof(saved_mode)) {
        return -EINVAL;
    }
    int ret = read_cb(cb_arg, &saved_mode, sizeof(saved_mode));
    if (ret < 0) {
        return ret;
    }
    if (ret != sizeof(saved_mode) || saved_mode > 1) {
        return -EINVAL;
    }

    // ZMK loads settings before initializing the display.
    windows_mode_by_profile[profile] = saved_mode != 0;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(os_mode, "os_mode", NULL, os_mode_settings_set, NULL, NULL);

#endif

static uint8_t os_active_profile_index(void) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    int index = zmk_ble_active_profile_index();
    if (index < 0 || index >= ZMK_BLE_PROFILE_COUNT) {
        return 0;
    }
    return (uint8_t)index;
#else
    return 0;
#endif
}

static bool os_windows_mode_for_profile(uint8_t index) {
    return index < ZMK_BLE_PROFILE_COUNT && windows_mode_by_profile[index];
}

bool sharp_mip_os_windows_mode(void) {
    return os_windows_mode_for_profile(os_active_profile_index());
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static int os_active_profile_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *ev = as_zmk_ble_active_profile_changed(eh);
    if (ev == NULL || ev->index >= ZMK_BLE_PROFILE_COUNT) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    raise_zmk_os_mode_changed((struct zmk_os_mode_changed){
        .windows_mode = os_windows_mode_for_profile(ev->index),
    });
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_active_profile, os_active_profile_listener);
ZMK_SUBSCRIPTION(os_active_profile, zmk_ble_active_profile_changed);
#endif

static uint8_t os_swap_control_gui(uint8_t modifiers) {
    uint8_t swapped = modifiers & ~(MOD_LCTL | MOD_RCTL | MOD_LGUI | MOD_RGUI);
    if (modifiers & MOD_LCTL)
        swapped |= MOD_LGUI;
    if (modifiers & MOD_RCTL)
        swapped |= MOD_RGUI;
    if (modifiers & MOD_LGUI)
        swapped |= MOD_LCTL;
    if (modifiers & MOD_RGUI)
        swapped |= MOD_RCTL;
    return swapped;
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static uint32_t globe_press_count;
static bool globe_windows_mode;

static int os_keycode_listener(const zmk_event_t *eh) {
    if (emitting_os_keycode) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->usage_page == ZMK_HID_USAGE_PAGE(GLOBE) &&
        ev->keycode == ZMK_HID_USAGE_ID(GLOBE)) {
        // Keep overlapping Globe holds in the same mode until all are released.
        if (ev->state) {
            if (globe_press_count == 0) {
                globe_windows_mode = sharp_mip_os_windows_mode();
            }
            globe_press_count++;
        } else if (globe_press_count > 0) {
            globe_press_count--;
        }

        if (globe_windows_mode) {
            *ev = zmk_keycode_state_changed_from_encoded(LC(LGUI), ev->state, ev->timestamp);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!sharp_mip_os_windows_mode() || ev->usage_page != HID_USAGE_KEY) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    ev->implicit_modifiers = os_swap_control_gui(ev->implicit_modifiers);
    ev->explicit_modifiers = os_swap_control_gui(ev->explicit_modifiers);

    switch (ev->keycode) {
    case HID_USAGE_KEY_KEYBOARD_LEFTCONTROL:
        ev->keycode = HID_USAGE_KEY_KEYBOARD_LEFT_GUI;
        break;
    case HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL:
        ev->keycode = HID_USAGE_KEY_KEYBOARD_RIGHT_GUI;
        break;
    case HID_USAGE_KEY_KEYBOARD_LEFT_GUI:
        ev->keycode = HID_USAGE_KEY_KEYBOARD_LEFTCONTROL;
        break;
    case HID_USAGE_KEY_KEYBOARD_RIGHT_GUI:
        ev->keycode = HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL;
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_keycode, os_keycode_listener);
ZMK_SUBSCRIPTION(os_keycode, zmk_keycode_state_changed);
#endif

int sharp_mip_os_emit_keycode(uint32_t keycode,
                                 struct zmk_behavior_binding_event event, bool pressed) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    const struct zmk_behavior_binding key_press = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)),
        .param1 = keycode,
    };
    bool previous = emitting_os_keycode;
    emitting_os_keycode = true;
    int ret = zmk_behavior_invoke_binding(&key_press, event, pressed);
    emitting_os_keycode = previous;
    return ret;
#else
    return 0;
#endif
}

static int os_mode_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    uint8_t profile = os_active_profile_index();
    windows_mode_by_profile[profile] = !windows_mode_by_profile[profile];
    bool windows_mode = windows_mode_by_profile[profile];
    raise_zmk_os_mode_changed((struct zmk_os_mode_changed){
        .windows_mode = windows_mode,
    });
#if IS_ENABLED(CONFIG_SETTINGS) && \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
    // Save each deliberate profile mode toggle immediately.
    uint8_t saved_mode = windows_mode;
    char setting_name[24];
    snprintf(setting_name, sizeof(setting_name), "os_mode/profile_%u",
             (unsigned int)profile);
    int ret = settings_save_one(setting_name, &saved_mode, sizeof(saved_mode));
    if (ret < 0) {
        LOG_ERR("Failed to save OS mode for profile %u (%d)", (unsigned int)profile, ret);
    }
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int os_mode_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    (void)binding;
    (void)event;
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api os_mode_api = {
    .binding_pressed = os_mode_pressed,
    .binding_released = os_mode_released,
};

#define OS_MODE_INST(n)                                                     \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
                            &os_mode_api);

DT_INST_FOREACH_STATUS_OKAY(OS_MODE_INST)
