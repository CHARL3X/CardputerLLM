#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include <ESPAI.h>
#include <M5GFX.h>

struct ModelChoice {
    const char* slug;
    const char* label;
};

class ChatScreen {
public:
    ChatScreen(ESPAI::OpenAICompatibleProvider* ai,
               const String& systemPrompt,
               const std::vector<ModelChoice>& models,
               int initialModelIdx,
               int initialHistoryDepth);

    void begin();
    void tick();

private:
    enum class Mode {
        Chat,
        Menu,
        Picker,
        DepthPicker,
        Confirm,
        Info,
    };

    struct Line {
        String   text;
        uint16_t color;
        bool     rightAlign;
    };

    struct Held { uint32_t firstPress = 0; uint32_t lastRepeat = 0; bool active = false; };

    // ---- main loop ----
    void pollKeyboard();
    bool repeatTick(Held& h, uint32_t initialMs = 400, uint32_t periodMs = 50);

    // ---- rising-edge handlers (dispatch by mode) ----
    void onCharPressed(char c, bool fn);
    void onDel();
    void onEnter();

    // ---- mode transitions ----
    void openMenu();
    void closeMenu();
    void openPicker(Mode returnTo);
    void openDepth();
    void openInfoScreen(const String& title);
    void confirmDestructive(const String& q, const String& detail,
                            std::function<void()> onYes, Mode returnTo);
    void resolveConfirm(bool yes);

    // ---- actions ----
    void sendCurrent();
    void newChat();
    void applyModel(int idx);
    void applyDepth(int depth);

    // ---- scroll helpers ----
    void scrollUp();
    void scrollDown();

    // ---- rendering ----
    void renderAll();
    void renderStatus();
    void renderBody();      // composes the right sub-render into _bodyCanvas and pushes
    void renderInput();
    void renderCursorOnly();

    // Body sub-renders. Each draws into _bodyCanvas in canvas-local coords.
    void renderChatBody();
    void renderMenuBody();
    void renderPickerBody();
    void renderDepthBody();
    void renderConfirmBody();
    void renderInfoBody();

    // ---- helpers ----
    void buildLines(std::vector<Line>& out);
    // Width measurement uses _bodyCanvas which carries the right font.
    void wrapIntoCanvas(const String& s, int maxPx, uint16_t color, bool right,
                        std::vector<Line>& out);
    int  lineHeight() const;
    int  bodyTop() const;
    int  bodyHeight() const;
    int  visibleLines() const;

    // ---- info-screen builders ----
    void buildSystemPromptLines();
    void buildWiFiLines();
    void buildDiagnosticsLines();

    // ---- dependencies ----
    ESPAI::OpenAICompatibleProvider* _ai;
    String                           _systemPrompt;
    std::vector<ModelChoice>         _models;
    int                              _modelIdx;
    int                              _historyDepth;

    // ---- state ----
    Mode _mode = Mode::Chat;
    ESPAI::Conversation _conv;
    String              _input;
    String              _sessionFile;
    bool                _streaming    = false;
    bool                _cancelStream = false;
    bool                _bodyDirty   = true;
    bool                _statusDirty = true;
    bool                _inputDirty  = true;
    int                 _scrollOffset = 0;
    bool                _autoScroll  = true;

    // ---- modal contexts ----
    int  _menuSel        = 0;
    int  _pickerSel      = 0;
    int  _depthSel       = 0;
    int  _pendingModelIdx = -1;
    Mode _modalReturnTo  = Mode::Chat;

    // Confirm context: reused by clear-chat and switch-model
    String                _confirmQ;
    String                _confirmD;
    std::function<void()> _confirmOnYes;
    Mode                  _confirmReturn = Mode::Menu;

    // Info screen content
    String              _infoTitle;
    std::vector<String> _infoLines;
    int                 _infoScroll = 0;

    // ---- keyboard tracking ----
    std::vector<char> _prevWord;
    bool _prevDel   = false;
    bool _prevEnter = false;
    Held _heldDel, _heldScrollUp, _heldScrollDown;

    // ---- cursor blink ----
    uint32_t _cursorTick = 0;
    bool     _cursorOn   = true;

    // ---- ambient animation for empty state + streaming indicator ----
    uint32_t _animTick   = 0;
    uint32_t _animPhase  = 0;

    // ---- empty-state renderer ----
    void renderEmptyChat();

    // ---- local slash commands (no API call) ----
    bool handleSlashCommand(const String& cmd);
    void addLocalExchange(const String& userMsg, const String& assistantMsg);
    String buildDiagSummary() const;

    // ---- slash autocomplete ----
    void updateSuggestions();
    void renderSuggestions();
    void completeSuggestion();
    std::vector<int> _suggMatches;
    int  _suggSel     = 0;
    bool _suggVisible = false;
    bool _prevTab     = false;

    // ---- periodic status row refresh (for RSSI/time updates) ----
    uint32_t _statusTick = 0;

    // ---- offscreen body buffer ----
    M5Canvas _bodyCanvas;
    bool     _canvasOk = false;
};
