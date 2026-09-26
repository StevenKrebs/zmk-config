# Add Smart Shift to the app target before ZMK appends the HID listener source.
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/smart_shift.c)

if(CONFIG_ZMK_DISPLAY AND CONFIG_SHARP_MIP_WIDGET_STATUS)
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  zephyr_library_sources(custom_status_screen.c)
  zephyr_library_sources(caps_word_signal.c)
  target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/os_mode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/screen_actions.c
  )
  zephyr_library_sources(widgets/bolt.c)
  zephyr_library_sources(widgets/modifier_icons.c)
  zephyr_library_sources(widgets/util.c)

  if(NOT CONFIG_ZMK_SPLIT OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    zephyr_library_sources(widgets/status.c)
  else()
    zephyr_library_sources(widgets/miryoku.c)
    zephyr_library_sources(widgets/peripheral_status.c)
  endif()
endif()
