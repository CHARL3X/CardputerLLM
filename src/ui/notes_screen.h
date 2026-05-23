// Verbatim notes-list home screen (Phase 5).
//
// The home screen IS the list. Not a placeholder leading to a list.
// State machine modeled after CardputerLLM's chat_screen:
//
//   Mode::List         default; navigation + open/delete/select/ask/record
//   Mode::MultiSelect  checkboxes shown; SPC toggles row selection
//   Mode::Confirm      generic yes/no overlay; reused for delete confirmation
//
// Phase 5 wires every key the hint bar advertises. Anything not wired
// (menu, retitle, ask-mode LLM dispatch) is intentionally NOT advertised.
#pragma once
#include <Arduino.h>
#include <M5GFX.h>
#include <vector>
#include <functional>

class NotesScreen {
public:
    NotesScreen(const String& apiKey,
                const String& txModel,
                const String& titleModel);

    void begin();
    void tick();

private:
    enum class Mode : uint8_t {
        List,
        MultiSelect,
        Confirm,
    };

    struct NoteRow {
        String   filename;
        String   title;
        String   createdUtc;
        uint32_t durationSec = 0;
        bool     selected    = false;
    };

    // ---- core loop helpers ----
    void render();
    void renderStatus();
    void renderBody();
    void renderHint();
    void renderListBody();
    void renderEmptyBody();
    void renderMultiSelectBody();
    void renderConfirmBody();
    void renderRow(const NoteRow& r, int y, int lineH, bool highlighted,
                   bool showCheckbox);

    // ---- keyboard ----
    void pollKeyboard();
    void onCharRising(char c, bool fn);
    void onEnter();
    void onDel();
    void onSpc();
    bool isUpKey(char c, bool fn) const;
    bool isDownKey(char c, bool fn) const;

    // ---- actions ----
    void rescan();           // re-read /Cardputer/notes/ and rebuild _rows
    void openHighlighted();  // open detail; refresh list if deleted there
    void deleteHighlightedConfirm();
    void doDeleteHighlighted();
    void enterMultiSelect();
    void exitMultiSelect();
    void toggleHighlightedSelection();
    void enterAskMode();     // Phase 6 stub for now
    void recordNew();        // SPC dispatcher

    // ---- confirm overlay ----
    void confirm(const String& q, const String& detail,
                 std::function<void()> onYes);
    void resolveConfirm(bool yes);

    // ---- formatting helpers ----
    String formatRightLabel(const NoteRow& r) const;
    int    visibleRowCount() const;
    int    bodyHeight()      const;
    int    bodyTop()         const;
    void   clampScroll();
    int    selectedCount()   const;

    // ---- dependencies ----
    String _apiKey;
    String _txModel;
    String _titleModel;

    // ---- state ----
    Mode _mode = Mode::List;
    std::vector<NoteRow> _rows;
    int  _highlight = 0;
    int  _scrollTop = 0;
    bool _bodyDirty   = true;
    bool _statusDirty = true;
    bool _hintDirty   = true;

    // confirm context
    String                _confirmQ;
    String                _confirmDetail;
    std::function<void()> _confirmOnYes;
    Mode                  _confirmReturn = Mode::List;

    // keyboard tracking
    std::vector<char> _prevWord;
    bool _prevDel   = false;
    bool _prevEnter = false;

    // canvas (allocated lazily on first render to leave heap free
    // during boot)
    M5Canvas _bodyCanvas;
    bool     _canvasOk = false;
};
