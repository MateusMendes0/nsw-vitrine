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

int main(int, char**) {
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
#endif
    vitrine::App app(networkReady);
    vitrine::ImageRenderer images;
    bool running = true;
#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
#endif

    while (running) {
        vitrine::Input input;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
#ifndef __SWITCH__
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

#ifdef __SWITCH__
        if (!appletMainLoop()) running = false;
        const vitrine::Input switchInput = vitrine::readSwitchInput(pad);
        input = switchInput;
#endif
        if (input.quit) running = false;
        app.handle(input);
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
