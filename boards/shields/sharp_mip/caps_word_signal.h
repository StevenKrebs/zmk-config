#pragma once

#include <zmk/event_manager.h>

struct zmk_caps_word_signal {
    bool toggled;
};

ZMK_EVENT_DECLARE(zmk_caps_word_signal);
