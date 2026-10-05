/*
 * Piston - Signal Chain UI.
 *
 * The shadow UI opens this (instead of a knob grid) because the DSP answers
 * `ui_hierarchy` with an empty string. Everything except Back is routed to
 * onMidiMessageInternal while this view is up: pads (because we hold
 * host_pad_block), the eight knobs, and their capacitive touch notes 0-7.
 *
 * All behaviour lives in ui_core.mjs; this file only binds Schwung's globals,
 * resolved at CALL time because host_module_get/set_param are per-slot shims
 * the shadow UI installs around this module.
 */
import { createUi } from './ui_core.mjs';

const has = (name) => typeof globalThis[name] === "function";

const core = createUi({
    setParam: (key, value) => has("host_module_set_param")
        ? host_module_set_param(key, value) !== false : false,
    getParam: (key) => has("host_module_get_param") ? host_module_get_param(key) : null,
    padBlock: (on) => { if (has("host_pad_block")) host_pad_block(on ? 1 : 0); },
    sendLed: (note, color) => has("move_midi_internal_send")
        ? move_midi_internal_send([0x09, 0x90, note, color]) !== false : false,
    padSnapshot: () => has("shadow_get_pad_led_snapshot") ? shadow_get_pad_led_snapshot() : null,
    displayOn: () => !has("shadow_get_display_mode") || shadow_get_display_mode() === 1,
    now: () => Date.now(),
    gfx: {
        clear: () => clear_screen(),
        print: (x, y, text, color) => print(x, y, text, color),
        fillRect: (x, y, w, h, color) => fill_rect(x, y, w, h, color),
        drawRect: (x, y, w, h, color) => draw_rect(x, y, w, h, color),
        textWidth: (text) => has("text_width") ? text_width(text) : text.length * 5,
    },
});

globalThis.chain_ui = {
    init: core.init,
    tick: core.tick,
    onMidiMessageInternal: core.onMidi,
    handleBack: core.handleBack,
};
