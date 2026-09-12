#include "app.hpp"
#include "image_renderer.hpp"
#include "input.hpp"
#include "text_renderer.hpp"
#include "ui_constants.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <string>

int main(int argc, char** argv) {
#ifdef __SWITCH__
    const std::string executablePath = argc > 0 && argv && argv[0] ? argv[0] : "";
#else
    bool demoMode = false;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--demo") demoMode = true;
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return EXIT_FAILURE;
    if ((IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP) & (IMG_INIT_JPG | IMG_INIT_PNG)) == 0) {
        IMG_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window* window = SDL_CreateWindow("Vitrine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          vitrine::kWidth, vitrine::kHeight, SDL_WINDOW_SHOWN);
    if (!window) {
        IMG_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_RenderSetLogicalSize(renderer, vitrine::kWidth, vitrine::kHeight);

    vitrine::TextRenderer text;
    if (!text.initialize()) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }

    bool networkReady = true;
#ifdef __SWITCH__
    romfsInit();
    networkReady = R_SUCCEEDED(socketInitializeDefault());
#else
    if (demoMode) networkReady = false;
#endif
    vitrine::App app(networkReady,
#ifdef __SWITCH__
                     executablePath
#else
                     std::string()
#endif
    );
    vitrine::ImageRenderer images;
    bool running = true;
#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
#else
    const Uint32 demoStart = SDL_GetTicks();
    int demoStep = 0;
    bool fingerActive = false;
    SDL_FingerID activeFinger = 0;
    int fingerStartX = 0;
    int fingerStartY = 0;
    int fingerX = 0;
    int fingerY = 0;
    bool mouseActive = false;
    int mouseStartX = 0;
    int mouseStartY = 0;
    int mouseX = 0;
    int mouseY = 0;
#endif

    while (running) {
        vitrine::Input input;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
#ifndef __SWITCH__
            const auto normalizedX = [](float value) {
                return std::max(0, std::min(vitrine::kWidth - 1,
                    static_cast<int>(std::lround(value * vitrine::kWidth))));
            };
            const auto normalizedY = [](float value) {
                return std::max(0, std::min(vitrine::kHeight - 1,
                    static_cast<int>(std::lround(value * vitrine::kHeight))));
            };
            if (event.type == SDL_FINGERDOWN && !fingerActive) {
                fingerActive = true;
                activeFinger = event.tfinger.fingerId;
                fingerStartX = normalizedX(event.tfinger.x);
                fingerStartY = normalizedY(event.tfinger.y);
                fingerX = fingerStartX;
                fingerY = fingerStartY;
                input.touchBegan = true;
            }
            if (event.type == SDL_FINGERMOTION && fingerActive &&
                event.tfinger.fingerId == activeFinger) {
                fingerX = normalizedX(event.tfinger.x);
                fingerY = normalizedY(event.tfinger.y);
            }
            if (event.type == SDL_FINGERUP && fingerActive &&
                event.tfinger.fingerId == activeFinger) {
                input.touchReleased = true;
                input.touchStartX = fingerStartX;
                input.touchStartY = fingerStartY;
                input.touchX = fingerX = normalizedX(event.tfinger.x);
                input.touchY = fingerY = normalizedY(event.tfinger.y);
                fingerActive = false;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
                event.button.which != SDL_TOUCH_MOUSEID) {
                mouseActive = true;
                mouseStartX = event.button.x;
                mouseStartY = event.button.y;
                mouseX = mouseStartX;
                mouseY = mouseStartY;
                input.touchBegan = true;
            }
            if (event.type == SDL_MOUSEMOTION && mouseActive &&
                event.motion.which != SDL_TOUCH_MOUSEID) {
                mouseX = event.motion.x;
                mouseY = event.motion.y;
            }
            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
                event.button.which != SDL_TOUCH_MOUSEID && mouseActive) {
                input.touchReleased = true;
                input.touchStartX = mouseStartX;
                input.touchStartY = mouseStartY;
                input.touchX = mouseX = event.button.x;
                input.touchY = mouseY = event.button.y;
                mouseActive = false;
            }
            if (event.type == SDL_TEXTINPUT) app.appendSearchText(event.text.text);
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                input.up |= key == SDLK_UP;
                input.down |= key == SDLK_DOWN;
                input.left |= key == SDLK_LEFT;
                input.right |= key == SDLK_RIGHT;
                input.accept |= key == SDLK_RETURN;
                input.back |= key == SDLK_ESCAPE;
                input.search |= key == SDLK_y || key == SDLK_SLASH;
                input.sort |= key == SDLK_x;
                input.previousGenre |= key == SDLK_q;
                input.nextGenre |= key == SDLK_e;
                input.surprise |= key == SDLK_s;
                input.backlog |= key == SDLK_l;
                input.viewMode |= key == SDLK_v;
                input.favorite |= key == SDLK_f;
                input.sync |= key == SDLK_MINUS;
                if (key == SDLK_BACKSPACE) app.eraseSearchCharacter();
            }
#endif
        }

#ifndef __SWITCH__
        if (fingerActive) {
            input.touchActive = true;
            input.touchStartX = fingerStartX;
            input.touchStartY = fingerStartY;
            input.touchX = fingerX;
            input.touchY = fingerY;
        } else if (mouseActive) {
            input.touchActive = true;
            input.touchStartX = mouseStartX;
            input.touchStartY = mouseStartY;
            input.touchX = mouseX;
            input.touchY = mouseY;
        }
#endif

#ifdef __SWITCH__
        if (!appletMainLoop()) running = false;
        const vitrine::Input switchInput = vitrine::readSwitchInput(pad);
        input = switchInput;
#else
        if (demoMode) {
            const Uint32 elapsed = SDL_GetTicks() - demoStart;
            switch (demoStep) {
                case 0: if (elapsed >= 1500) { input.right = true; ++demoStep; } break;
                case 1: if (elapsed >= 2300) { input.right = true; ++demoStep; } break;
                case 2: if (elapsed >= 3100) { input.right = true; ++demoStep; } break;
                case 3: if (elapsed >= 4100) { input.accept = true; ++demoStep; } break;
                case 4: if (elapsed >= 7800) { input.right = true; ++demoStep; } break;
                case 5: if (elapsed >= 9200) { input.left = true; ++demoStep; } break;
                case 6: if (elapsed >= 10600) { input.back = true; ++demoStep; } break;
                case 7: if (elapsed >= 11600) { input.viewMode = true; ++demoStep; } break;
                case 8: if (elapsed >= 12600) { input.right = true; ++demoStep; } break;
                case 9: if (elapsed >= 13600) { input.right = true; ++demoStep; } break;
                case 10: if (elapsed >= 14600) { input.left = true; ++demoStep; } break;
                case 11: if (elapsed >= 15600) { input.viewMode = true; ++demoStep; } break;
                case 12: if (elapsed >= 16600) { input.nextGenre = true; ++demoStep; } break;
                case 13: if (elapsed >= 17600) { input.nextGenre = true; ++demoStep; } break;
                case 14: if (elapsed >= 18600) { input.nextGenre = true; ++demoStep; } break;
                case 15: if (elapsed >= 20000) { input.quit = true; ++demoStep; } break;
                default: break;
            }
        }
#endif
        if (input.quit) running = false;
        app.handle(input);
        if (app.quitRequested()) running = false;
        app.render(renderer, text, images);
        SDL_RenderPresent(renderer);
    }

    app.releaseRendererResources();
    images.clear();
    text.shutdown();
#ifdef __SWITCH__
    romfsExit();
    if (networkReady) socketExit();
#endif
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return EXIT_SUCCESS;
}
