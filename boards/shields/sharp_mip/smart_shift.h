#pragma once

#include <zmk/event_manager.h>

struct zmk_smart_shift_state_changed {
    bool armed;
    bool disarmed;
};

ZMK_EVENT_DECLARE(zmk_smart_shift_state_changed);
