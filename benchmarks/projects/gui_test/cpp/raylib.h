#ifndef RAYLIB_H
#define RAYLIB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Vector2 {
    float x;
    float y;
} Vector2;

typedef struct Color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} Color;

typedef struct Rectangle {
    float x;
    float y;
    float width;
    float height;
} Rectangle;

#define FLAG_WINDOW_HIDDEN 128
#define MOUSE_BUTTON_LEFT 0

#define KEY_NULL 0
#define KEY_APOSTROPHE 39
#define KEY_COMMA 44
#define KEY_MINUS 45
#define KEY_PERIOD 46
#define KEY_SLASH 47
#define KEY_ZERO 48
#define KEY_ONE 49
#define KEY_TWO 50
#define KEY_THREE 51
#define KEY_FOUR 52
#define KEY_FIVE 53
#define KEY_SIX 54
#define KEY_SEVEN 55
#define KEY_EIGHT 56
#define KEY_NINE 57
#define KEY_SEMICOLON 59
#define KEY_EQUAL 61
#define KEY_ENTER 257
#define KEY_BACKSPACE 259
#define KEY_ESCAPE 256
#define KEY_KP_0 320
#define KEY_KP_1 321
#define KEY_KP_2 322
#define KEY_KP_3 323
#define KEY_KP_4 324
#define KEY_KP_5 325
#define KEY_KP_6 326
#define KEY_KP_7 327
#define KEY_KP_8 328
#define KEY_KP_9 329
#define KEY_KP_DECIMAL 330
#define KEY_KP_DIVIDE 331
#define KEY_KP_MULTIPLY 332
#define KEY_KP_SUBTRACT 333
#define KEY_KP_ADD 334
#define KEY_KP_ENTER 335
#define KEY_C 67

void InitWindow(int width, int height, const char *title);
void CloseWindow(void);
bool WindowShouldClose(void);
bool IsWindowReady(void);
void SetConfigFlags(unsigned int flags);
void SetTargetFPS(int fps);
void BeginDrawing(void);
void EndDrawing(void);
void ClearBackground(Color color);
void DrawRectangleRec(Rectangle rec, Color color);
void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color);
void DrawText(const char *text, int posX, int posY, int fontSize, Color color);
int MeasureText(const char *text, int fontSize);
Vector2 GetMousePosition(void);
bool IsMouseButtonPressed(int button);
bool IsKeyPressed(int key);
bool CheckCollisionPointRec(Vector2 point, Rectangle rec);

#ifdef __cplusplus
}
#endif

#endif
