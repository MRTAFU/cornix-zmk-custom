/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_cornix_layer_gate

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define CORNIX_LAYER_GATE_COUNT 2
#define CORNIX_LAYER_GATE_NO_POSITION UINT32_MAX

struct behavior_cornix_layer_gate_config {
    uint8_t id;
    uint8_t layer;
    uint32_t tap_keycode;
    uint16_t tap_term_ms;
};

/*
 * Runtime state for LOWER (id 0) and RAISE (id 1). Kept as one small
 * file-scope table (not per-instance driver data) because ADJUST detection
 * requires each gate to see the other gate's state, and there are always
 * exactly two gates by design.
 */
struct cornix_layer_gate_state {
    bool pressed;
    bool used;
    int64_t pressed_at;
    uint32_t position;
};

static struct cornix_layer_gate_state gate_state[CORNIX_LAYER_GATE_COUNT] = {
    {.position = CORNIX_LAYER_GATE_NO_POSITION},
    {.position = CORNIX_LAYER_GATE_NO_POSITION},
};

static int on_cornix_layer_gate_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cornix_layer_gate_config *cfg = dev->config;

    struct cornix_layer_gate_state *self = &gate_state[cfg->id];
    struct cornix_layer_gate_state *other = &gate_state[1 - cfg->id];

    self->pressed = true;
    self->pressed_at = event.timestamp;
    self->position = event.position;
    /*
     * If the other gate is already held, this press is the second key of
     * the LOWER+RAISE chord: ADJUST is about to become active via
     * conditional_layers, and neither gate may resolve to a tap anymore,
     * even this one, even if no third key ever follows.
     */
    self->used = other->pressed;
    if (other->pressed) {
        other->used = true;
    }

    LOG_DBG("cornix_layer_gate id=%d pos=%d layer=%d activate", cfg->id, event.position,
           cfg->layer);

    return zmk_keymap_layer_activate(cfg->layer, false);
}

static int on_cornix_layer_gate_binding_released(struct zmk_behavior_binding *binding,
                                                 struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_cornix_layer_gate_config *cfg = dev->config;
    struct cornix_layer_gate_state *self = &gate_state[cfg->id];

    self->pressed = false;
    self->position = CORNIX_LAYER_GATE_NO_POSITION;

    zmk_keymap_layer_deactivate(cfg->layer, false);

    int64_t elapsed = event.timestamp - self->pressed_at;
    bool qualifies_as_tap = !self->used && elapsed < cfg->tap_term_ms;

    self->used = false;

    if (qualifies_as_tap) {
        LOG_DBG("cornix_layer_gate id=%d tap keycode=0x%02X", cfg->id, cfg->tap_keycode);
        raise_zmk_keycode_state_changed_from_encoded(cfg->tap_keycode, true, event.timestamp);
        raise_zmk_keycode_state_changed_from_encoded(cfg->tap_keycode, false, event.timestamp);
    }

    return 0;
}

static const struct behavior_driver_api behavior_cornix_layer_gate_driver_api = {
    .binding_pressed = on_cornix_layer_gate_binding_pressed,
    .binding_released = on_cornix_layer_gate_binding_released,
};

/*
 * Marks a currently-held gate as "used" the instant any OTHER physical key
 * goes down. Key releases are ignored entirely, and a gate's own down event
 * is ignored here because the chord (ADJUST) case is already handled,
 * order-independently, inside on_cornix_layer_gate_binding_pressed above:
 * this listener only ever sees `gate_state[i].position` either still equal
 * to the gate's own new position (set moments ago or moments from now by
 * the same raised event, in either listener order) or left at the
 * not-pressed sentinel, so it never double-counts a gate's own press.
 */
static int cornix_layer_gate_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < CORNIX_LAYER_GATE_COUNT; i++) {
        if (!gate_state[i].pressed || ev->position == gate_state[i].position) {
            continue;
        }

        gate_state[i].used = true;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(cornix_layer_gate, cornix_layer_gate_position_listener);
ZMK_SUBSCRIPTION(cornix_layer_gate, zmk_position_state_changed);

#define CORNIX_LAYER_GATE_INST(n)                                                                \
    static const struct behavior_cornix_layer_gate_config                                       \
        behavior_cornix_layer_gate_config_##n = {                                                \
            .id = DT_INST_PROP(n, id),                                                           \
            .layer = DT_INST_PROP(n, layer),                                                     \
            .tap_keycode = DT_INST_PROP(n, tap_keycode),                                         \
            .tap_term_ms = DT_INST_PROP(n, tap_term_ms),                                         \
    };                                                                                           \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_cornix_layer_gate_config_##n,         \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                    \
                            &behavior_cornix_layer_gate_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CORNIX_LAYER_GATE_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
