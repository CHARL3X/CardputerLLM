// Tetris -- third mode on the unified Cardputer launcher.
//
// Classic Tetris: 10x20 playfield, 7 tetrominoes in their canonical
// colors, soft + hard drop, lines accumulate score and level, high
// score persists to NVS via settings::tetrisHighScore().
//
// Blocking; owns the display + keyboard for the session. Returns to
// the picker on `~` / backtick OR on game-over after the user
// acknowledges.
#pragma once

namespace tetris_screen {

void run();

} // namespace tetris_screen
