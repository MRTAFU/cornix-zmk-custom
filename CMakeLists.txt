add_subdirectory_ifdef(CONFIG_SHIELD_CORNIX_INDICATOR boards/shields/cornix_indicator)

# cornix_layer_gate only needs to run on the central half of the split;
# the peripheral just forwards raw key events like it does for every
# other keymap behavior.
if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_CORNIX_LAYER_GATE app PRIVATE
        src/behaviors/behavior_cornix_layer_gate.c)
endif()
