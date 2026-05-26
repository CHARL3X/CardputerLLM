// Tetris implementation. Standalone game; no external state beyond
// the NVS high-score key.
//
// Renders PORTRAIT (135 wide x 240 tall) so the player rotates the
// Cardputer 90 deg CCW (keyboard on the left, screen on the right)
// to play. Doubles the vertical real-estate vs the default landscape
// orientation, which lets us use 10 px cells -> 100 x 200 playfield.
//
// Layout (135 x 240 portrait):
//   top strip:    y=0..12   score (left) | HI (right)
//   playfield:    x=2..102, y=14..214    10 cols x 20 rows @ 10 px
//   side panel:   x=106..132, y=14..214  NEXT box + LINES + LEVEL
//   hint bar:     y=216..239              two lines of key hints
//
// Portrait-friendly keymap (diamond around the home-row left side
// after the user rotates the device 90 deg CCW):
//        d        (up / rotate)
//     e  s  z     (left, down, right)
//   spc = hard drop, p = pause, ` = exit.

#include "tetris.h"
#include "../storage/settings.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <vector>
#include <math.h>

namespace {

constexpr int kScreenW = 135;   // post-rotation portrait width
constexpr int kScreenH = 240;   // post-rotation portrait height

constexpr int kCols    = 10;
constexpr int kRows    = 20;
constexpr int kCell    = 10;
constexpr int kBoardX  = 2;
constexpr int kBoardY  = 14;
constexpr int kBoardW  = kCols * kCell;          // 100
constexpr int kBoardH  = kRows * kCell;          // 200

constexpr int kPanelX  = kBoardX + kBoardW + 4;  // 106
constexpr int kPanelY  = 14;
constexpr int kPanelW  = kScreenW - kPanelX - 1; // 28

constexpr int kHintY   = 218;

constexpr uint16_t kBg          = 0x0000;
constexpr uint16_t kGrid        = 0x1082;
constexpr uint16_t kBorder      = 0x07E0;  // phosphor green frame
constexpr uint16_t kIdle        = 0xEF7D;
constexpr uint16_t kDim         = 0x6B4D;
constexpr uint16_t kAccent      = 0x07E0;
constexpr uint16_t kGhost       = 0x2104;
constexpr uint16_t kPanelInk    = 0xEF7D;

// Canonical tetromino colors (RGB565).
constexpr uint16_t kPieceColor[7] = {
    0x07FF,  // I  cyan
    0xFFE0,  // O  yellow
    0x801F,  // T  purple
    0x07E0,  // S  green
    0xF800,  // Z  red
    0x001F,  // J  blue
    0xFD20,  // L  orange
};

// Tetromino shapes encoded as 16-bit bitmaps over a 4x4 grid (LSB =
// (row 0, col 0); bit (r*4 + c)). Four rotation states per piece.
//
// Pieces ordered: I, O, T, S, Z, J, L (indices 0..6).
const uint16_t kPieces[7][4] = {
    // I
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },
    // O (rotation invariant)
    { 0x0660, 0x0660, 0x0660, 0x0660 },
    // T
    { 0x0E40, 0x4C40, 0x4E00, 0x4640 },
    // S
    { 0x06C0, 0x4620, 0x06C0, 0x4620 },
    // Z
    { 0x0C60, 0x2640, 0x0C60, 0x2640 },
    // J
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
    // L
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },
};

// Spawn columns (top-left of the 4x4 bounding box).
constexpr int kSpawnX[7] = { 3, 4, 3, 3, 3, 3, 3 };

struct Piece {
    int type;    // 0..6
    int x;       // grid x (col of bbox's left edge)
    int y;       // grid y (row of bbox's top edge)
    int rot;     // 0..3
};

class TetrisScreen {
public:
    TetrisScreen() : _canvas(&M5Cardputer.Display) {}
    void run();

private:
    void newGame();
    void spawnNext();
    bool isValid(const Piece& p) const;
    void merge(const Piece& p);
    int  clearLines();
    int  ghostY(const Piece& p) const;

    void renderAll();
    void renderBoard();
    void renderPanel();
    void renderPiece(const Piece& p, uint16_t color, bool faint);
    void renderHintBar();
    void renderGameOver();
    void renderPaused();
    void renderReadyScreen();

    void onKey(char c);
    void hardDrop();
    void rotate();
    void move(int dx);
    void softDrop();

    void beep(uint16_t freq, uint16_t ms);
    void cheerLineClear(int lines);
    void cheerGameOver();

    uint32_t dropIntervalMs() const;

    // ---- state ----
    uint8_t  _board[kRows][kCols] = {0};  // 0 = empty, else piece type + 1
    Piece    _cur{};
    int      _nextType  = 0;
    int      _score     = 0;
    int      _lines     = 0;
    int      _level     = 1;
    uint32_t _highScore = 0;
    uint32_t _lastDrop  = 0;
    bool     _soft      = false;
    bool     _paused    = false;
    bool     _gameOver  = false;
    bool     _exit      = false;

    // ---- keyboard tracking ----
    std::vector<char> _prevWord;
    bool _prevDel = false, _prevEnter = false;

    // Held-key auto-repeat: initial press fires immediately, then
    // after kDasMs delay starts repeating every kArrMs. Matches the
    // hold-to-repeat pattern in CardputerLLM's chat_screen.
    struct Held { uint32_t firstPress = 0; uint32_t lastRepeat = 0; bool active = false; };
    Held _heldLeft, _heldRight, _heldDown;
    static constexpr uint32_t kDasMs = 220;
    static constexpr uint32_t kArrMs = 60;
    bool repeatTick(Held& h, uint32_t now) {
        if (!h.active) return false;
        if (now - h.firstPress < kDasMs) return false;
        if (now - h.lastRepeat < kArrMs) return false;
        h.lastRepeat = now;
        return true;
    }

    // ---- canvas ----
    M5Canvas _canvas;
    bool     _canvasOk = false;
};

void TetrisScreen::run() {
    Serial.printf("[tetris] heap pre-canvas free=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());

    // Flip display to portrait. The picker / chat / notes all assume
    // landscape rotation 1; we restore that on exit. Rotation 2 is the
    // 180-degree-flipped portrait orientation -- keyboard ends up on
    // the user's RIGHT after a 90 deg CW physical rotation of the
    // device, which is what the e/d/s/z control diamond was designed
    // for. (Rotation 0 is the other portrait orientation; we tried
    // that first and the user was holding the device upside-down to
    // play.)
    M5Cardputer.Display.setRotation(2);
    M5Cardputer.Display.fillScreen(kBg);

    _canvas.setColorDepth(16);
    _canvasOk = _canvas.createSprite(kScreenW, kScreenH);
    if (!_canvasOk) {
        Serial.println("[tetris] full-screen canvas alloc failed, retrying smaller");
        // Fall back to a smaller canvas covering just the playfield;
        // panel + hint bar render directly to the display.
        _canvasOk = _canvas.createSprite(kBoardW + 2, kBoardH + 2);
        if (!_canvasOk) {
            Serial.println("[tetris] fallback canvas alloc also failed");
            M5Cardputer.Display.setFont(&fonts::Font2);
            M5Cardputer.Display.setTextColor(0xF800, kBg);
            M5Cardputer.Display.setCursor(8, 110);
            M5Cardputer.Display.print("tetris: heap too low");
            delay(2000);
            M5Cardputer.Display.setRotation(1);
            return;
        }
    }
    Serial.printf("[tetris] canvas ok (%d x %d)\n",
                  _canvas.width(), _canvas.height());

    _highScore = settings::tetrisHighScore();
    randomSeed(millis());

    // Show the "rotate device" prompt + controls until the player
    // presses enter (or hits ` to bail).
    renderReadyScreen();
    while (!_exit) {
        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();
        bool started = false;
        if (s.enter && !_prevEnter) started = true;
        for (char c : s.word) {
            if (c == '`' || c == '~') { _exit = true; break; }
        }
        _prevEnter = s.enter;
        _prevWord  = s.word;
        if (started) break;
        if (_exit) {
            _canvas.deleteSprite();
            M5Cardputer.Display.setRotation(1);
            return;
        }
        delay(20);
    }
    // Drain modifier state so the same Enter press doesn't trigger
    // anything inside the game loop on the first frame.
    _prevDel   = false;
    _prevEnter = true;
    _prevWord.clear();

    newGame();
    renderAll();

    while (!_exit) {
        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        // Rising-edge per-char dispatch.
        for (char c : s.word) {
            bool wasPrev = false;
            for (char p : _prevWord) if (p == c) { wasPrev = true; break; }
            if (wasPrev) continue;
            onKey(c);
        }
        if (s.enter && !_prevEnter) {
            if (_gameOver) {
                newGame();
                renderAll();
            } else if (_paused) {
                _paused = false;
                renderAll();
            }
        }
        if (s.del && !_prevDel) {
            if (_gameOver) {
                _exit = true;
            } else {
                _paused = !_paused;
                renderAll();
            }
        }
        // Held-key auto-repeat for the three directional keys. Each
        // direction was already moved once on its rising edge inside
        // onKey(); this block triggers additional moves once the key
        // has been held past kDasMs, then every kArrMs.
        //
        // Portrait keymap: e=LEFT, z=RIGHT, s=DOWN (soft drop), d=UP.
        bool leftHeld = false, rightHeld = false, downHeld = false;
        for (char c : s.word) {
            if      (c == 'e' || c == 'E') leftHeld  = true;
            else if (c == 'z' || c == 'Z') rightHeld = true;
            else if (c == 's' || c == 'S') downHeld  = true;
        }
        uint32_t nowAr = millis();
        if (!leftHeld)  _heldLeft.active  = false;
        if (!rightHeld) _heldRight.active = false;
        if (!downHeld)  _heldDown.active  = false;

        if (!_paused && !_gameOver) {
            if (repeatTick(_heldLeft,  nowAr)) move(-1);
            if (repeatTick(_heldRight, nowAr)) move(+1);
            // Down auto-repeat: nudge one row each tick past DAS.
            if (repeatTick(_heldDown, nowAr)) {
                Piece test = _cur;
                test.y += 1;
                if (isValid(test)) {
                    _cur = test;
                    _score += 1;
                    _lastDrop = nowAr;
                    renderAll();
                }
            }
        }

        // Soft drop while s is held: bumps gravity to a fast interval.
        _soft = downHeld;

        _prevWord  = s.word;
        _prevDel   = s.del;
        _prevEnter = s.enter;

        // Gravity tick.
        if (!_paused && !_gameOver) {
            uint32_t now = millis();
            uint32_t interval = _soft ? 50 : dropIntervalMs();
            if (now - _lastDrop >= interval) {
                _lastDrop = now;
                Piece test = _cur;
                test.y += 1;
                if (isValid(test)) {
                    _cur = test;
                    renderAll();
                } else {
                    merge(_cur);
                    int cleared = clearLines();
                    if (cleared > 0) cheerLineClear(cleared);
                    spawnNext();
                    if (!isValid(_cur)) {
                        _gameOver = true;
                        if ((uint32_t)_score > _highScore) {
                            _highScore = _score;
                            settings::setTetrisHighScore(_highScore);
                        }
                        cheerGameOver();
                        renderAll();
                    } else {
                        renderAll();
                    }
                }
            }
        }
        delay(15);
    }

    _canvas.deleteSprite();
    M5Cardputer.Display.setRotation(1);
}

void TetrisScreen::newGame() {
    for (int r = 0; r < kRows; r++)
        for (int c = 0; c < kCols; c++)
            _board[r][c] = 0;
    _score    = 0;
    _lines    = 0;
    _level    = 1;
    _soft     = false;
    _paused   = false;
    _gameOver = false;
    _nextType = random(7);
    spawnNext();
    _lastDrop = millis();
}

void TetrisScreen::spawnNext() {
    _cur.type = _nextType;
    _cur.rot  = 0;
    _cur.x    = kSpawnX[_cur.type];
    _cur.y    = -1;  // partly off the top so it can scroll in
    _nextType = random(7);
}

bool TetrisScreen::isValid(const Piece& p) const {
    uint16_t bits = kPieces[p.type][p.rot];
    for (int i = 0; i < 16; i++) {
        if (!(bits & (1 << i))) continue;
        int cx = p.x + (i % 4);
        int cy = p.y + (i / 4);
        if (cx < 0 || cx >= kCols)  return false;
        if (cy >= kRows)            return false;
        if (cy < 0)                 continue;          // above the board is OK
        if (_board[cy][cx])         return false;
    }
    return true;
}

void TetrisScreen::merge(const Piece& p) {
    uint16_t bits = kPieces[p.type][p.rot];
    for (int i = 0; i < 16; i++) {
        if (!(bits & (1 << i))) continue;
        int cx = p.x + (i % 4);
        int cy = p.y + (i / 4);
        if (cy < 0 || cy >= kRows || cx < 0 || cx >= kCols) continue;
        _board[cy][cx] = (uint8_t)(p.type + 1);
    }
}

int TetrisScreen::clearLines() {
    int cleared = 0;
    for (int r = kRows - 1; r >= 0; r--) {
        bool full = true;
        for (int c = 0; c < kCols; c++) {
            if (!_board[r][c]) { full = false; break; }
        }
        if (full) {
            // shift everything above r down by 1
            for (int rr = r; rr > 0; rr--) {
                for (int c = 0; c < kCols; c++) _board[rr][c] = _board[rr-1][c];
            }
            for (int c = 0; c < kCols; c++) _board[0][c] = 0;
            cleared++;
            r++;  // re-check this row index after shift
        }
    }
    if (cleared > 0) {
        static const int kLineScore[5] = { 0, 100, 300, 500, 800 };
        _score += kLineScore[cleared] * _level;
        _lines += cleared;
        _level = 1 + _lines / 10;
    }
    return cleared;
}

int TetrisScreen::ghostY(const Piece& p) const {
    Piece q = p;
    while (true) {
        Piece test = q;
        test.y += 1;
        if (!isValid(test)) return q.y;
        q = test;
    }
}

uint32_t TetrisScreen::dropIntervalMs() const {
    // Level 1 -> 800 ms, scaling down by 0.8 per level, floored at 80.
    float f = 800.0f;
    for (int i = 1; i < _level; i++) f *= 0.82f;
    if (f < 80.0f) f = 80.0f;
    return (uint32_t)f;
}

void TetrisScreen::move(int dx) {
    if (_paused || _gameOver) return;
    Piece test = _cur;
    test.x += dx;
    if (isValid(test)) { _cur = test; renderAll(); }
}

void TetrisScreen::rotate() {
    if (_paused || _gameOver) return;
    Piece test = _cur;
    test.rot = (test.rot + 1) & 3;
    // basic wall kicks: try -1, +1, -2, +2
    int kicks[5] = { 0, -1, 1, -2, 2 };
    for (int k : kicks) {
        Piece q = test;
        q.x += k;
        if (isValid(q)) { _cur = q; renderAll(); return; }
    }
}

void TetrisScreen::softDrop() {
    _soft = true;
}

void TetrisScreen::hardDrop() {
    if (_paused || _gameOver) return;
    int target = ghostY(_cur);
    int dropped = target - _cur.y;
    _cur.y = target;
    _score += 2 * dropped;
    merge(_cur);
    int cleared = clearLines();
    if (cleared > 0) cheerLineClear(cleared);
    spawnNext();
    if (!isValid(_cur)) {
        _gameOver = true;
        if ((uint32_t)_score > _highScore) {
            _highScore = _score;
            settings::setTetrisHighScore(_highScore);
        }
        cheerGameOver();
    }
    _lastDrop = millis();
    renderAll();
}

void TetrisScreen::onKey(char c) {
    if (c == '`' || c == '~') { _exit = true; return; }
    if (_gameOver) {
        if (c == 'r' || c == 'R') { newGame(); renderAll(); }
        return;
    }
    if (_paused) return;
    uint32_t now = millis();
    // Portrait keymap (after the player rotates the device 90 deg CCW
    // so the keyboard is on the left, screen on the right):
    //        d        (up / rotate)
    //     e  s  z     (left, down, right)
    switch (c) {
        case 'e': case 'E':
            move(-1);
            _heldLeft.active = true;
            _heldLeft.firstPress = now;
            _heldLeft.lastRepeat = now;
            break;
        case 'z': case 'Z':
            move(+1);
            _heldRight.active = true;
            _heldRight.firstPress = now;
            _heldRight.lastRepeat = now;
            break;
        case 's': case 'S': {
            // Soft-drop rising edge: nudge one row immediately + score.
            // Held-state handler below keeps gravity fast while held.
            Piece test = _cur;
            test.y += 1;
            if (isValid(test)) {
                _cur = test;
                _score += 1;
                _lastDrop = now;
                renderAll();
            }
            _heldDown.active = true;
            _heldDown.firstPress = now;
            _heldDown.lastRepeat = now;
            break;
        }
        case 'd': case 'D':           rotate(); break;   // up = rotate
        case ' ':                     hardDrop(); break;
        case 'p': case 'P':           _paused = !_paused; renderAll(); break;
        default: break;
    }
}

void TetrisScreen::beep(uint16_t freq, uint16_t ms) {
    M5Cardputer.Speaker.setVolume(40);
    M5Cardputer.Speaker.tone(freq, ms);
}

void TetrisScreen::cheerLineClear(int lines) {
    if (lines >= 4) {
        // Tetris! Quick arpeggio.
        beep(523, 80); delay(60);
        beep(659, 80); delay(60);
        beep(784, 80); delay(60);
        beep(1047, 120);
    } else {
        beep(880, 60);
    }
}

void TetrisScreen::cheerGameOver() {
    beep(330, 120); delay(80);
    beep(262, 120); delay(80);
    beep(196, 200);
}

void TetrisScreen::renderAll() {
    if (!_canvasOk) return;
    _canvas.fillScreen(kBg);
    renderBoard();
    renderPanel();
    renderHintBar();
    if (_paused)   renderPaused();
    if (_gameOver) renderGameOver();
    _canvas.pushSprite(0, 0);
}

void TetrisScreen::renderBoard() {
    // Frame around the playfield
    _canvas.drawRect(kBoardX - 1, kBoardY - 1, kBoardW + 2, kBoardH + 2, kBorder);

    // Faint grid
    for (int c = 1; c < kCols; c++) {
        _canvas.drawFastVLine(kBoardX + c * kCell, kBoardY, kBoardH, kGrid);
    }
    for (int r = 1; r < kRows; r++) {
        _canvas.drawFastHLine(kBoardX, kBoardY + r * kCell, kBoardW, kGrid);
    }

    // Locked cells
    for (int r = 0; r < kRows; r++) {
        for (int c = 0; c < kCols; c++) {
            if (_board[r][c]) {
                uint16_t col = kPieceColor[_board[r][c] - 1];
                int px = kBoardX + c * kCell;
                int py = kBoardY + r * kCell;
                _canvas.fillRect(px + 1, py + 1, kCell - 1, kCell - 1, col);
            }
        }
    }

    // Ghost preview of the current piece's landing position
    Piece ghost = _cur;
    ghost.y = ghostY(_cur);
    if (ghost.y != _cur.y) {
        renderPiece(ghost, kGhost, true);
    }

    // Active piece on top
    renderPiece(_cur, kPieceColor[_cur.type], false);
}

void TetrisScreen::renderPiece(const Piece& p, uint16_t color, bool faint) {
    uint16_t bits = kPieces[p.type][p.rot];
    for (int i = 0; i < 16; i++) {
        if (!(bits & (1 << i))) continue;
        int cx = p.x + (i % 4);
        int cy = p.y + (i / 4);
        if (cy < 0 || cy >= kRows || cx < 0 || cx >= kCols) continue;
        int px = kBoardX + cx * kCell;
        int py = kBoardY + cy * kCell;
        if (faint) {
            _canvas.drawRect(px + 1, py + 1, kCell - 1, kCell - 1, color);
        } else {
            _canvas.fillRect(px + 1, py + 1, kCell - 1, kCell - 1, color);
        }
    }
}

void TetrisScreen::renderPanel() {
    // Top strip: SCORE on the left, HI on the right (above playfield)
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(1);
    _canvas.setTextDatum(top_left);
    _canvas.setTextColor(kAccent, kBg);
    _canvas.drawString(String(_score).c_str(), kBoardX, 2);

    _canvas.setTextDatum(top_right);
    _canvas.setTextColor(kDim, kBg);
    String hiTxt = String("HI ") + String(_highScore);
    _canvas.drawString(hiTxt.c_str(), kScreenW - 2, 2);
    _canvas.setTextDatum(top_left);

    // Side panel
    int y = kPanelY;
    int boxX = kPanelX;
    int boxSize = 24;

    // NEXT label centered above the box (Font0 NEXT is 24px wide so it lines up).
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextColor(kDim, kBg);
    _canvas.drawString("NEXT", boxX, y);
    y += 8;

    int nextBoxY = y;
    _canvas.drawRect(boxX, nextBoxY, boxSize, boxSize, kDim);

    // Render the next piece centered in its 4x4 bounding box.
    // 4 px/cell -> 16x16 grid, centered inside the 24x24 box.
    uint16_t nbits = kPieces[_nextType][0];
    int cellSize = 4;
    int gridW = cellSize * 4;
    int gx = boxX + (boxSize - gridW) / 2;
    int gy = nextBoxY + (boxSize - gridW) / 2;
    for (int i = 0; i < 16; i++) {
        if (!(nbits & (1 << i))) continue;
        int cx = i % 4;
        int cy = i / 4;
        _canvas.fillRect(gx + cx * cellSize, gy + cy * cellSize,
                         cellSize - 1, cellSize - 1, kPieceColor[_nextType]);
    }
    y = nextBoxY + boxSize + 8;

    // Stats stacked vertically. Label (dim) above value (ink).
    auto drawStat = [&](const char* label, const String& value) {
        _canvas.setFont(&fonts::Font0);
        _canvas.setTextColor(kDim, kBg);
        _canvas.drawString(label, boxX, y);
        y += 9;
        _canvas.setTextColor(kPanelInk, kBg);
        _canvas.drawString(value.c_str(), boxX, y);
        y += 12;
    };

    drawStat("LINES", String(_lines));
    drawStat("LEVEL", String(_level));
}

void TetrisScreen::renderHintBar() {
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(1);
    _canvas.setTextDatum(top_left);
    _canvas.setTextColor(kDim, kBg);
    // Two lines so the controls actually fit at 135 px wide.
    _canvas.drawString("e/z move  d rotate",  2, kHintY);
    _canvas.drawString("s soft  spc drop  p", 2, kHintY + 10);
}

void TetrisScreen::renderPaused() {
    int oy = kBoardY + kBoardH / 2 - 10;
    _canvas.fillRect(kBoardX - 2, oy, kBoardW + 4, 20, kBg);
    _canvas.drawRect(kBoardX - 2, oy, kBoardW + 4, 20, kAccent);
    _canvas.setFont(&fonts::FreeMonoBold9pt7b);
    _canvas.setTextDatum(top_center);
    _canvas.setTextColor(kAccent, kBg);
    _canvas.drawString("PAUSED", kBoardX + kBoardW / 2, oy + 4);
    _canvas.setTextDatum(top_left);
}

void TetrisScreen::renderGameOver() {
    int oy = kBoardY + kBoardH / 2 - 22;
    _canvas.fillRect(kBoardX - 2, oy, kBoardW + 4, 44, kBg);
    _canvas.drawRect(kBoardX - 2, oy, kBoardW + 4, 44, 0xF800);
    _canvas.setFont(&fonts::FreeMonoBold9pt7b);
    _canvas.setTextDatum(top_center);
    _canvas.setTextColor(0xF800, kBg);
    _canvas.drawString("GAME", kBoardX + kBoardW / 2, oy + 4);
    _canvas.drawString("OVER", kBoardX + kBoardW / 2, oy + 20);
    _canvas.setTextDatum(top_left);
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextColor(kDim, kBg);
    _canvas.drawString("ret retry  ` exit", kBoardX, oy + 50);
}

void TetrisScreen::renderReadyScreen() {
    if (!_canvasOk) return;
    _canvas.fillScreen(kBg);

    // Title
    _canvas.setFont(&fonts::FreeMonoBold9pt7b);
    _canvas.setTextSize(1);
    _canvas.setTextDatum(top_center);
    _canvas.setTextColor(kAccent, kBg);
    _canvas.drawString("TETRIS", kScreenW / 2, 20);

    // Rotation hint: a curved arrow + "ROTATE DEVICE" text.
    // The arrow is a quick set of strokes pointing CCW around a circle.
    int cx = kScreenW / 2;
    int cy = 60;
    int r  = 14;
    // Draw a 3/4 circle arc segmented around the center
    for (int a = -45; a <= 225; a += 8) {
        float rad = a * 3.14159f / 180.0f;
        int x = cx + (int)(cosf(rad) * r);
        int y = cy + (int)(sinf(rad) * r);
        _canvas.fillCircle(x, y, 1, kAccent);
    }
    // Arrowhead at the start of the arc (lower-right, pointing CCW)
    float arad = -45.0f * 3.14159f / 180.0f;
    int ax = cx + (int)(cosf(arad) * r);
    int ay = cy + (int)(sinf(arad) * r);
    _canvas.fillTriangle(ax, ay, ax + 5, ay - 1, ax + 2, ay + 5, kAccent);

    _canvas.setFont(&fonts::Font0);
    _canvas.setTextColor(kIdle, kBg);
    _canvas.drawString("rotate device", kScreenW / 2, 86);
    _canvas.setTextColor(kDim, kBg);
    _canvas.drawString("keyboard on the left", kScreenW / 2, 98);

    // Control diamond illustration
    int dx = kScreenW / 2;
    int dy = 140;
    int gap = 18;
    auto keycap = [&](int x, int y, const char* label, uint16_t color) {
        _canvas.fillRoundRect(x - 9, y - 8, 18, 16, 3, kBg);
        _canvas.drawRoundRect(x - 9, y - 8, 18, 16, 3, color);
        _canvas.setTextDatum(top_center);
        _canvas.setTextColor(color, kBg);
        _canvas.drawString(label, x, y - 5);
    };
    keycap(dx,        dy - gap, "D", kAccent);  // up
    keycap(dx - gap,  dy,       "E", kIdle);    // left
    keycap(dx,        dy,       "S", kIdle);    // down
    keycap(dx + gap,  dy,       "Z", kIdle);    // right

    // Labels next to the diamond
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextColor(kDim, kBg);
    _canvas.setTextDatum(top_left);
    _canvas.drawString("d  rotate",     8, 168);
    _canvas.drawString("e  left",       8, 180);
    _canvas.drawString("z  right",      8, 192);
    _canvas.drawString("s  soft drop",  8, 204);
    _canvas.drawString("spc hard drop", 8, 216);

    // Footer
    _canvas.setTextDatum(top_center);
    _canvas.setTextColor(kAccent, kBg);
    _canvas.drawString("press ret to start", kScreenW / 2, 228);
    _canvas.setTextDatum(top_left);

    _canvas.pushSprite(0, 0);
}

} // namespace

namespace tetris_screen {

void run() {
    Serial.println("[tetris] start");
    TetrisScreen* ts = new TetrisScreen();
    ts->run();
    delete ts;
    Serial.println("[tetris] exit");
}

} // namespace tetris_screen
