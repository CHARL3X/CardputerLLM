#pragma once
#include <Arduino.h>
#include <vector>
#include <ESPAI.h>

class ChatScreen {
public:
    explicit ChatScreen(ESPAI::OpenAICompatibleProvider* ai);

    void begin();
    void tick(); // call from loop()

private:
    struct Turn {
        ESPAI::Role role;
        String      text;
    };

    struct Line {
        String   text;
        uint16_t color;
        bool     rightAlign;
    };

    void onKeyChange();
    void sendCurrent();

    void renderAll();
    void renderTurns();
    void renderInput();
    void renderCursorOnly();

    void wrapInto(const String& s, int maxPx, uint16_t color, bool right,
                  std::vector<Line>& out);
    int  lineHeight() const;

    ESPAI::OpenAICompatibleProvider* _ai;

    std::vector<Turn> _turns;
    String            _input;
    bool              _streaming   = false;
    uint32_t          _cursorTick  = 0;
    bool              _cursorOn    = true;
    bool              _bodyDirty   = true;
    bool              _inputDirty  = true;
};
