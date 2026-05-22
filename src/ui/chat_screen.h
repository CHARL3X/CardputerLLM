#pragma once
#include <Arduino.h>
#include <vector>
#include <ESPAI.h>

struct ModelChoice {
    const char* slug;
    const char* label;
};

class ChatScreen {
public:
    ChatScreen(ESPAI::OpenAICompatibleProvider* ai,
               const String& systemPrompt,
               const std::vector<ModelChoice>& models,
               int initialModelIdx);

    void begin();
    void tick();

private:
    enum class Mode { Chat, Picker, Confirm };

    struct Line {
        String   text;
        uint16_t color;
        bool     rightAlign;
    };

    // Keyboard polling + hold-to-repeat
    struct Held { uint32_t firstPress = 0; uint32_t lastRepeat = 0; bool active = false; };
    void pollKeyboard();
    bool repeatTick(Held& h, uint32_t initialMs = 400, uint32_t periodMs = 50);

    // Per-mode handlers
    void onCharPressed(char c, bool fn);
    void onDel();
    void onEnter();

    // Actions
    void sendCurrent();
    void newChat();
    void openPicker();
    void closePicker(bool committed);
    void applyModel(int idx);

    // Rendering
    void renderAll();
    void renderStatus();
    void renderBody();   // dispatches to renderChat / renderPicker / renderConfirm
    void renderInput();
    void renderCursorOnly();
    void renderChat();
    void renderPicker();
    void renderConfirm();

    // Helpers
    void buildLines(std::vector<Line>& out);
    void wrapInto(const String& s, int maxPx, uint16_t color, bool right,
                  std::vector<Line>& out);
    int  lineHeight() const;
    int  bodyTop() const;
    int  bodyHeight() const;
    int  visibleLines() const;

    // Dependencies
    ESPAI::OpenAICompatibleProvider* _ai;
    String                           _systemPrompt;
    std::vector<ModelChoice>         _models;
    int                              _modelIdx;
    int                              _pendingModelIdx = -1;

    // State
    Mode _mode = Mode::Chat;
    ESPAI::Conversation _conv;
    String              _input;
    String              _sessionFile;   // filename in /CardputerLLM/chats/
    bool                _streaming  = false;
    bool                _bodyDirty  = true;
    bool                _statusDirty = true;
    bool                _inputDirty = true;
    int                 _scrollOffset = 0; // lines from bottom, 0 = newest visible
    bool                _autoScroll  = true;
    int                 _pickerSel   = 0;

    // Keyboard tracking
    std::vector<char> _prevWord;
    bool _prevDel = false;
    bool _prevEnter = false;
    Held _heldDel, _heldScrollUp, _heldScrollDown;

    // Cursor blink
    uint32_t _cursorTick = 0;
    bool     _cursorOn   = true;
};
