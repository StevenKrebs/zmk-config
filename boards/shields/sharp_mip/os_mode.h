#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>

struct zmk_os_mode_changed {
    bool windows_mode;
};

ZMK_EVENT_DECLARE(zmk_os_mode_changed);

bool sharp_mip_os_windows_mode(void);
int sharp_mip_os_emit_keycode(uint32_t keycode,
                                 struct zmk_behavior_binding_event event, bool pressed);
