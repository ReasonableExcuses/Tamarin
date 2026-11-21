// ============================================================================
// C++ GUI Calculator - High-Performance Native Raylib 5.5 App
// ============================================================================
#include "raylib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── Calculator State ────────────────────────────────────────────────────────
struct CalcState {
    int displayLen;
    int exprLen;
    double storedVal;
    unsigned char pendingOp; // 0, '+', '-', '*', '/'
    bool hasPendingOp;
    bool shouldResetOnDigit;
    bool hasError;
};

static void resetAll(CalcState& s, unsigned char display[], unsigned char expr[]) {
    display[0] = '0';
    display[1] = '\0';
    s.displayLen = 1;
    expr[0] = '\0';
    s.exprLen = 0;
    s.storedVal = 0.0;
    s.pendingOp = 0;
    s.hasPendingOp = false;
    s.shouldResetOnDigit = false;
    s.hasError = false;
}

static double parseDouble(const unsigned char buf[], int len) {
    double res = 0.0;
    double frac = 0.0;
    double div = 10.0;
    bool inFrac = false;
    bool neg = false;
    int i = 0;
    if (len > 0 && buf[0] == '-') {
        neg = true;
        i = 1;
    }
    while (i < len) {
        unsigned char c = buf[i];
        if (c == '.') {
            inFrac = true;
        } else if (c >= '0' && c <= '9') {
            double digit = (double)(c - '0');
            if (!inFrac) {
                res = res * 10.0 + digit;
            } else {
                frac = frac + digit / div;
                div = div * 10.0;
            }
        }
        i++;
    }
    double total = res + frac;
    if (neg) total = -total;
    return total;
}

static void formatDouble(double val, unsigned char buf[], int& outLen) {
    if (val != val) {
        buf[0] = 'E'; buf[1] = 'r'; buf[2] = 'r'; buf[3] = '\0';
        outLen = 3;
        return;
    }
    bool neg = false;
    if (val < 0.0) {
        neg = true;
        val = -val;
    }
    long long intPart = (long long)val;
    double fracPart = val - (double)intPart;

    unsigned char temp[32];
    int tLen = 0;
    if (intPart == 0LL) {
        temp[0] = '0';
        tLen = 1;
    } else {
        long long n = intPart;
        while (n > 0LL) {
            int rem = (int)(n % 10LL);
            temp[tLen++] = (unsigned char)('0' + rem);
            n /= 10LL;
        }
    }

    int pos = 0;
    if (neg) {
        buf[pos++] = '-';
    }
    for (int k = tLen - 1; k >= 0; k--) {
        buf[pos++] = temp[k];
    }

    if (fracPart > 0.0000001 && pos < 20) {
        buf[pos++] = '.';
        int decimals = 0;
        while (decimals < 6 && fracPart > 0.0000001 && pos < 26) {
            fracPart *= 10.0;
            int d = (int)fracPart;
            buf[pos++] = (unsigned char)('0' + d);
            fracPart -= (double)d;
            decimals++;
        }
        while (pos > 0 && buf[pos - 1] == '0') pos--;
        if (pos > 0 && buf[pos - 1] == '.') pos--;
    }
    buf[pos] = '\0';
    outLen = pos;
}

static void appendDigit(CalcState& s, unsigned char display[], unsigned char digit) {
    if (s.hasError || s.shouldResetOnDigit || (s.displayLen == 1 && display[0] == '0')) {
        display[0] = digit;
        display[1] = '\0';
        s.displayLen = 1;
        s.shouldResetOnDigit = false;
        s.hasError = false;
    } else if (s.displayLen < 15) {
        display[s.displayLen++] = digit;
        display[s.displayLen] = '\0';
    }
}

static void appendDot(CalcState& s, unsigned char display[]) {
    if (s.hasError || s.shouldResetOnDigit) {
        display[0] = '0';
        display[1] = '.';
        display[2] = '\0';
        s.displayLen = 2;
        s.shouldResetOnDigit = false;
        s.hasError = false;
        return;
    }
    bool hasDot = false;
    for (int i = 0; i < s.displayLen; i++) {
        if (display[i] == '.') hasDot = true;
    }
    if (!hasDot && s.displayLen < 15) {
        display[s.displayLen++] = '.';
        display[s.displayLen] = '\0';
    }
}

static void doBackspace(CalcState& s, unsigned char display[], unsigned char expr[]) {
    if (s.hasError || s.shouldResetOnDigit) {
        resetAll(s, display, expr);
        return;
    }
    if (s.displayLen > 1) {
        s.displayLen--;
        display[s.displayLen] = '\0';
    } else {
        display[0] = '0';
        display[1] = '\0';
        s.displayLen = 1;
    }
}

static void doToggleSign(CalcState& s, unsigned char display[]) {
    if (s.hasError) return;
    if (s.displayLen == 1 && display[0] == '0') return;
    if (display[0] == '-') {
        for (int i = 0; i < s.displayLen - 1; i++) {
            display[i] = display[i + 1];
        }
        s.displayLen--;
        display[s.displayLen] = '\0';
    } else if (s.displayLen < 30) {
        for (int i = s.displayLen; i >= 0; i--) {
            display[i + 1] = display[i];
        }
        display[0] = '-';
        s.displayLen++;
        display[s.displayLen] = '\0';
    }
}

static void doPercent(CalcState& s, unsigned char display[]) {
    if (s.hasError) return;
    double v = parseDouble(display, s.displayLen) / 100.0;
    formatDouble(v, display, s.displayLen);
}

static void executePending(CalcState& s, unsigned char display[]) {
    if (!s.hasPendingOp) return;
    double current = parseDouble(display, s.displayLen);
    double result = 0.0;
    if (s.pendingOp == '+') {
        result = s.storedVal + current;
    } else if (s.pendingOp == '-') {
        result = s.storedVal - current;
    } else if (s.pendingOp == '*') {
        result = s.storedVal * current;
    } else if (s.pendingOp == '/') {
        if (current == 0.0) {
            s.hasError = true;
            std::strcpy((char*)display, "Error");
            s.displayLen = 5;
            s.hasPendingOp = false;
            s.shouldResetOnDigit = true;
            return;
        }
        result = s.storedVal / current;
    }
    formatDouble(result, display, s.displayLen);
    s.storedVal = result;
}

static void doOperator(CalcState& s, unsigned char display[], unsigned char expr[], unsigned char op) {
    if (s.hasError) return;
    if (s.hasPendingOp && !s.shouldResetOnDigit) {
        executePending(s, display);
    } else {
        s.storedVal = parseDouble(display, s.displayLen);
    }
    s.pendingOp = op;
    s.hasPendingOp = true;
    s.shouldResetOnDigit = true;

    for (int i = 0; i < s.displayLen; i++) {
        expr[i] = display[i];
    }
    expr[s.displayLen] = ' ';
    expr[s.displayLen + 1] = op;
    expr[s.displayLen + 2] = '\0';
    s.exprLen = s.displayLen + 2;
}

static void doEquals(CalcState& s, unsigned char display[], unsigned char expr[]) {
    if (s.hasError || !s.hasPendingOp) return;
    executePending(s, display);
    s.hasPendingOp = false;
    s.pendingOp = 0;
    s.shouldResetOnDigit = true;
    expr[0] = '\0';
    s.exprLen = 0;
}

static const char* getBtnLabel(int idx) {
    static const char* labels[] = {
        "C", "+/-", "%", "/",
        "7", "8", "9", "*",
        "4", "5", "6", "-",
        "1", "2", "3", "+",
        "0", ".", "DEL", "="
    };
    if (idx >= 0 && idx < 20) return labels[idx];
    return "";
}

int main() {
    const int winWidth = 360;
    const int winHeight = 560;

    const char* benchEnv = std::getenv("TAMARIN_CALC_BENCH");
    int maxFrames = 0;
    if (benchEnv != nullptr) {
        maxFrames = 300;
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
    }

    InitWindow(winWidth, winHeight, "C++ GUI Calculator");
    SetTargetFPS(60);

    CalcState state;
    state.displayLen = 1;
    state.exprLen = 0;
    state.storedVal = 0.0;
    state.pendingOp = 0;
    state.hasPendingOp = false;
    state.shouldResetOnDigit = false;
    state.hasError = false;

    unsigned char display[32] = { '0', '\0' };
    unsigned char expr[48] = { '\0' };

    // Color Palette matching Tamarin app exactly
    Color colBg        = { 30, 30, 46, 255 };     // Deep Charcoal
    Color colDispBg    = { 24, 24, 37, 255 };     // Display Panel
    Color colBtnNum    = { 49, 50, 68, 255 };     // Dark Slate
    Color colBtnHover  = { 69, 71, 90, 255 };     // Hover Slate
    Color colBtnTop    = { 59, 62, 82, 255 };     // Top row fn buttons
    Color colBtnOp     = { 108, 92, 231, 255 };   // Vibrant Indigo
    Color colBtnOpHov  = { 162, 155, 254, 255 };  // Light Indigo
    Color colBtnEq     = { 0, 184, 148, 255 };    // Emerald Accent
    Color colBtnEqHov  = { 85, 239, 196, 255 };   // Bright Emerald
    Color colBtnDel    = { 214, 48, 49, 255 };    // Crimson
    Color colBtnDelHov = { 255, 118, 117, 255 };  // Soft Crimson
    Color colTextWhite = { 240, 240, 245, 255 };
    Color colTextMuted = { 147, 153, 178, 255 };
    Color colTextDark  = { 20, 25, 35, 255 };
    Color colBorder    = { 49, 50, 68, 255 };

    int frameCount = 0;
    while (!WindowShouldClose()) {
        if (maxFrames > 0 && frameCount >= maxFrames) {
            break;
        }
        frameCount++;

        Vector2 mouse = GetMousePosition();
        bool mouseClicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        // Keyboard Shortcuts
        if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0)) appendDigit(state, display, '0');
        if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) appendDigit(state, display, '1');
        if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) appendDigit(state, display, '2');
        if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) appendDigit(state, display, '3');
        if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4)) appendDigit(state, display, '4');
        if (IsKeyPressed(KEY_FIVE) || IsKeyPressed(KEY_KP_5)) appendDigit(state, display, '5');
        if (IsKeyPressed(KEY_SIX) || IsKeyPressed(KEY_KP_6)) appendDigit(state, display, '6');
        if (IsKeyPressed(KEY_SEVEN) || IsKeyPressed(KEY_KP_7)) appendDigit(state, display, '7');
        if (IsKeyPressed(KEY_EIGHT) || IsKeyPressed(KEY_KP_8)) appendDigit(state, display, '8');
        if (IsKeyPressed(KEY_NINE) || IsKeyPressed(KEY_KP_9)) appendDigit(state, display, '9');
        if (IsKeyPressed(KEY_PERIOD) || IsKeyPressed(KEY_KP_DECIMAL)) appendDot(state, display);
        if (IsKeyPressed(KEY_KP_ADD)) doOperator(state, display, expr, '+');
        if (IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressed(KEY_MINUS)) doOperator(state, display, expr, '-');
        if (IsKeyPressed(KEY_KP_MULTIPLY)) doOperator(state, display, expr, '*');
        if (IsKeyPressed(KEY_KP_DIVIDE) || IsKeyPressed(KEY_SLASH)) doOperator(state, display, expr, '/');
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_EQUAL)) doEquals(state, display, expr);
        if (IsKeyPressed(KEY_BACKSPACE)) doBackspace(state, display, expr);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_C)) resetAll(state, display, expr);

        BeginDrawing();
        ClearBackground(colBg);

        // 1. Draw Display Screen
        Rectangle dispRec = { 16.0f, 16.0f, 328.0f, 100.0f };
        DrawRectangleRec(dispRec, colDispBg);
        DrawRectangleLinesEx(dispRec, 2.0f, colBorder);

        // Top expression line
        if (state.exprLen > 0) {
            int exprW = MeasureText((const char*)expr, 18);
            DrawText((const char*)expr, 330 - exprW, 26, 18, colTextMuted);
        }

        // Main value line
        int textW = MeasureText((const char*)display, 36);
        DrawText((const char*)display, 330 - textW, 60, 36, colTextWhite);

        // 2. Draw 5x4 Button Grid
        int btnW = 74;
        int btnH = 68;
        int startX = 16;
        int startY = 132;
        int gapX = 10;
        int gapY = 12;

        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 4; col++) {
                int idx = row * 4 + col;
                int bx = startX + col * (btnW + gapX);
                int by = startY + row * (btnH + gapY);
                Rectangle bRec = { (float)bx, (float)by, (float)btnW, (float)btnH };

                bool hov = CheckCollisionPointRec(mouse, bRec);

                Color curCol = colBtnNum;
                Color curTxt = colTextWhite;

                if (idx == 0 || idx == 1 || idx == 2) {
                    curCol = hov ? colBtnHover : colBtnTop;
                } else if (idx == 3 || idx == 7 || idx == 11 || idx == 15) {
                    curCol = hov ? colBtnOpHov : colBtnOp;
                } else if (idx == 18) { // DEL
                    curCol = hov ? colBtnDelHov : colBtnDel;
                } else if (idx == 19) { // =
                    curCol = hov ? colBtnEqHov : colBtnEq;
                    curTxt = colTextDark;
                } else {
                    curCol = hov ? colBtnHover : colBtnNum;
                }

                DrawRectangleRec(bRec, curCol);

                const char* lbl = getBtnLabel(idx);
                int lblW = MeasureText(lbl, 22);
                int lblX = bx + (btnW - lblW) / 2;
                int lblY = by + (btnH - 22) / 2;
                DrawText(lbl, lblX, lblY, 22, curTxt);

                if (hov && mouseClicked) {
                    if (idx == 0) resetAll(state, display, expr);
                    else if (idx == 1) doToggleSign(state, display);
                    else if (idx == 2) doPercent(state, display);
                    else if (idx == 3) doOperator(state, display, expr, '/');
                    else if (idx == 4) appendDigit(state, display, '7');
                    else if (idx == 5) appendDigit(state, display, '8');
                    else if (idx == 6) appendDigit(state, display, '9');
                    else if (idx == 7) doOperator(state, display, expr, '*');
                    else if (idx == 8) appendDigit(state, display, '4');
                    else if (idx == 9) appendDigit(state, display, '5');
                    else if (idx == 10) appendDigit(state, display, '6');
                    else if (idx == 11) doOperator(state, display, expr, '-');
                    else if (idx == 12) appendDigit(state, display, '1');
                    else if (idx == 13) appendDigit(state, display, '2');
                    else if (idx == 14) appendDigit(state, display, '3');
                    else if (idx == 15) doOperator(state, display, expr, '+');
                    else if (idx == 16) appendDigit(state, display, '0');
                    else if (idx == 17) appendDot(state, display);
                    else if (idx == 18) doBackspace(state, display, expr);
                    else if (idx == 19) doEquals(state, display, expr);
                }
            }
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
