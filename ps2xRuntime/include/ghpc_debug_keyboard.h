#pragma once

#include <cstdint>

// The game's debug keyboard, fed from the host. GH2's debug build reads a USB
// keyboard and hands each key to CheatsManager, whose table
// (config/gen/cheats.dtb) binds p to toggle_auto_play, w to a win, F1-F8 to
// the track and lighting debug views and more. The IOP side reports a
// keyboard that never types, so this serves keys at the call instead: a
// queued key turns KeyboardPoll into ProcessRead(true) over a filled gKBData,
// and the game builds and sends its own KeyboardKeyMsg.
//
// Codes are what libusbkb reports: a character as itself ('p', 'P'), a
// non-character key as 0x8000 | its USB usage (F1 is 0x803a).
//
// Nothing in the window types into it: features reach it from the Esc menu.
// GHPC_DEBUG_KEYS="game_screen=p" presses keys once the named screen first
// shows, one per poll, for scripted runs.
namespace ghpc
{
    void debugKeyPush(uint16_t code);

    // The Esc menu's autoplay, through the game's own p cheat. Each press
    // steps cheats_funcs.dtb's MULTI_AUTO_STATES (off, player1, player2,
    // all), which alternates player 1, so one press per change keeps this in
    // step in a one-player game. It also sets PlayerConfig autoplay, so it
    // holds across songs and can be turned on from any menu. Off at launch,
    // as the game's is.
    bool autoplay();
    void setAutoplay(bool on);

    // At a call to KeyboardPoll. True when a key was served and the call now
    // goes to ProcessRead.
    bool debugKeyServe(uint8_t *rdram, uint32_t &targetPc, uint32_t &a0);
}
