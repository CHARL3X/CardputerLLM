// CardputerLLM phase 1: toolchain proof.
// Boots, draws a fixed header, echoes typed characters to the input row
// and to USB CDC serial. Confirms framework, M5Cardputer lib, and ADV
// keyboard (TCA8418) all work end to end.

#include <M5Cardputer.h>

static constexpr int kScreenW = 240;
static constexpr int kScreenH = 135;
static constexpr int kHeaderH = 14;
static constexpr int kInputH  = 16;

static String input;

static void drawHeader() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kHeaderH, TFT_DARKGREY);
    M5Cardputer.Display.setTextColor(TFT_BLACK, TFT_DARKGREY);
    M5Cardputer.Display.setCursor(2, 3);
    M5Cardputer.Display.print("CardputerLLM"); // TODO: name
    M5Cardputer.Display.setCursor(kScreenW - 32, 3);
    M5Cardputer.Display.print("P1");
}

static void drawBody() {
    M5Cardputer.Display.fillRect(0, kHeaderH, kScreenW, kScreenH - kHeaderH - kInputH, TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setCursor(2, kHeaderH + 4);
    M5Cardputer.Display.println("phase 1: toolchain ok.");
    M5Cardputer.Display.setCursor(2, kHeaderH + 16);
    M5Cardputer.Display.println("type; chars echo here +");
    M5Cardputer.Display.setCursor(2, kHeaderH + 28);
    M5Cardputer.Display.println("on usb serial @ 115200.");
    M5Cardputer.Display.setCursor(2, kHeaderH + 44);
    M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5Cardputer.Display.println("enter logs the buffer.");
}

static void drawInput() {
    const int y = kScreenH - kInputH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kInputH, TFT_BLACK);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, TFT_DARKGREY);
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.setCursor(2, y + 4);
    M5Cardputer.Display.print("> ");
    M5Cardputer.Display.print(input);
    M5Cardputer.Display.print('_');
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println("[boot] cardputerllm phase 1");
    Serial.printf("[boot] M5.getBoard() = %d\n", (int)M5.getBoard());
    Serial.printf("[boot] display: %dx%d\n",
                  M5Cardputer.Display.width(), M5Cardputer.Display.height());

    drawHeader();
    drawBody();
    drawInput();
}

void loop() {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto& state = M5Cardputer.Keyboard.keysState();

        for (auto c : state.word) {
            input += (char)c;
            Serial.write((char)c);
        }
        if (state.del && input.length() > 0) {
            input.remove(input.length() - 1);
            Serial.print("\b \b");
        }
        if (state.enter) {
            Serial.println();
            Serial.print("[line] ");
            Serial.println(input);
            input = "";
        }
        drawInput();
    }

    delay(5);
}
