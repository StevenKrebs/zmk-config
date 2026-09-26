#define DT_DRV_COMPAT zmk_behavior_screen_keycode

#include <drivers/behavior.h>
#include <zephyr/device.h>

#include <zmk/behavior.h>

#include "os_mode.h"

struct screen_keycode_config {
    uint32_t mac_key;
    uint32_t windows_key;
};

#define SCREEN_KEYCODE_POSITION_COUNT 64
static uint32_t active_keycodes[SCREEN_KEYCODE_POSITION_COUNT];

static int screen_keycode_invoke(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event,
                                 bool pressed) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct screen_keycode_config *config = dev->config;
    uint32_t keycode = sharp_mip_os_windows_mode() ? config->windows_key
                                                      : config->mac_key;

    if (event.position < SCREEN_KEYCODE_POSITION_COUNT) {
        if (pressed) {
            active_keycodes[event.position] = keycode;
        } else if (active_keycodes[event.position] != 0) {
            keycode = active_keycodes[event.position];
            active_keycodes[event.position] = 0;
        }
    }

    return sharp_mip_os_emit_keycode(keycode, event, pressed);
}

static int screen_keycode_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    return screen_keycode_invoke(binding, event, true);
}

static int screen_keycode_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    return screen_keycode_invoke(binding, event, false);
}

static const struct behavior_driver_api screen_keycode_api = {
    .binding_pressed = screen_keycode_pressed,
    .binding_released = screen_keycode_released,
};

#define SCREEN_KEYCODE_INST(n)                                                  \
    static const struct screen_keycode_config screen_keycode_config_##n = {    \
        .mac_key = DT_INST_PROP(n, mac_key),                                   \
        .windows_key = DT_INST_PROP(n, windows_key),                           \
    };                                                                         \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &screen_keycode_config_##n,   \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,   \
                            &screen_keycode_api);

DT_INST_FOREACH_STATUS_OKAY(SCREEN_KEYCODE_INST)

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_smart_modifier

#define SMART_MODIFIER_INST(n)                                                     \
    static const struct screen_keycode_config smart_modifier_config_##n = {        \
        .mac_key = DT_INST_PROP(n, mac_key),                                       \
        .windows_key = DT_INST_PROP(n, windows_key),                               \
    };                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &smart_modifier_config_##n,       \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,      \
                            &screen_keycode_api);

DT_INST_FOREACH_STATUS_OKAY(SMART_MODIFIER_INST)
