#define DT_DRV_COMPAT zmk_behavior_caps_word_signal

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>

#include "caps_word_signal.h"

ZMK_EVENT_IMPL(zmk_caps_word_signal);

static int caps_word_signal_pressed(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    const struct zmk_behavior_binding caps_word = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(caps_word)),
    };

    raise_zmk_caps_word_signal((struct zmk_caps_word_signal){.toggled = true});
    return zmk_behavior_invoke_binding(&caps_word, event, true);
}

static int caps_word_signal_released(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct zmk_behavior_binding caps_word = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(caps_word)),
    };

    return zmk_behavior_invoke_binding(&caps_word, event, false);
}

static const struct behavior_driver_api caps_word_signal_api = {
    .binding_pressed = caps_word_signal_pressed,
    .binding_released = caps_word_signal_released,
};

#define CAPS_WORD_SIGNAL_INST(n)                                                        \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                    \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                       \
                            &caps_word_signal_api);

DT_INST_FOREACH_STATUS_OKAY(CAPS_WORD_SIGNAL_INST)
