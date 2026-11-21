#ifndef SDL3_MINI_H
#define SDL3_MINI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

typedef struct SDL_Rect {
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;

typedef struct SDL_FRect {
    float x;
    float y;
    float w;
    float h;
} SDL_FRect;

typedef union SDL_Event {
    uint32_t type;
    uint8_t padding[128];
} SDL_Event;

#define SDL_INIT_VIDEO 0x00000020u
#define SDL_WINDOW_HIDDEN 0x0000000000000008ull
#define SDL_PIXELFORMAT_RGBA32 376840196
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_EVENT_QUIT 0x100
#define SDL_EVENT_KEY_DOWN 0x300

bool SDL_Init(uint32_t flags);
void SDL_Quit(void);
const char* SDL_GetError(void);

bool SDL_CreateWindowAndRenderer(const char* title, int width, int height, uint64_t window_flags, SDL_Window** window, SDL_Renderer** renderer);
void SDL_DestroyWindow(SDL_Window* window);
void SDL_DestroyRenderer(SDL_Renderer* renderer);

SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, int format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture* texture);
bool SDL_LockTexture(SDL_Texture* texture, const SDL_Rect* rect, void** pixels, int* pitch);
void SDL_UnlockTexture(SDL_Texture* texture);

bool SDL_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect* srcrect, const SDL_FRect* dstrect);
bool SDL_RenderPresent(SDL_Renderer* renderer);
bool SDL_SetRenderVSync(SDL_Renderer* renderer, int vsync);

bool SDL_PollEvent(SDL_Event* event);
uint64_t SDL_GetPerformanceCounter(void);
uint64_t SDL_GetPerformanceFrequency(void);

#ifdef __cplusplus
}
#endif

#endif // SDL3_MINI_H
