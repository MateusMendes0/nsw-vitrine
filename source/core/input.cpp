#include "input.hpp"

#include <cstdlib>

namespace vitrine {

TouchGestureDirection touchGestureDirection(const Input& input, int minimumDistance) {
    if (!input.touchReleased) return TouchGestureDirection::None;
    const int deltaX = input.touchX - input.touchStartX;
    const int deltaY = input.touchY - input.touchStartY;
    if (std::abs(deltaX) < minimumDistance && std::abs(deltaY) < minimumDistance) {
        return TouchGestureDirection::None;
    }
    if (std::abs(deltaX) >= std::abs(deltaY)) {
        return deltaX < 0 ? TouchGestureDirection::Left : TouchGestureDirection::Right;
    }
    return deltaY < 0 ? TouchGestureDirection::Up : TouchGestureDirection::Down;
}

#ifdef __SWITCH__
Input readSwitchInput(PadState& pad) {
    padUpdate(&pad);
    const u64 down = padGetButtonsDown(&pad);
    Input input;
    input.up = down & HidNpadButton_Up;
    input.down = down & HidNpadButton_Down;
    input.left = down & HidNpadButton_Left;
    input.right = down & HidNpadButton_Right;
    input.accept = down & HidNpadButton_A;
    input.back = down & HidNpadButton_B;
    input.search = down & HidNpadButton_Y;
    input.sort = down & HidNpadButton_X;
    input.previousGenre = down & HidNpadButton_L;
    input.nextGenre = down & HidNpadButton_R;
    input.surprise = down & HidNpadButton_ZL;
    input.backlog = down & HidNpadButton_ZR;
    input.viewMode = down & HidNpadButton_StickR;
    input.favorite = down & HidNpadButton_StickL;
    input.sync = down & HidNpadButton_Minus;
    input.quit = down & HidNpadButton_Plus;

    struct TouchTracker {
        bool active = false;
        u32 fingerId = 0;
        int startX = 0;
        int startY = 0;
        int lastX = 0;
        int lastY = 0;
    };
    static TouchTracker touch;

    HidTouchScreenState touchState{};
    const bool hasState = hidGetTouchScreenStates(&touchState, 1) != 0;
    const HidTouchState* point = nullptr;
    if (hasState && touchState.count > 0) {
        if (touch.active) {
            for (s32 index = 0; index < touchState.count; ++index) {
                if (touchState.touches[index].finger_id == touch.fingerId) {
                    point = &touchState.touches[index];
                    break;
                }
            }
        } else {
            point = &touchState.touches[0];
        }
    }

    if (!touch.active && point) {
        touch.active = true;
        touch.fingerId = point->finger_id;
        touch.startX = touch.lastX = static_cast<int>(point->x);
        touch.startY = touch.lastY = static_cast<int>(point->y);
        input.touchBegan = true;
    } else if (touch.active && point) {
        touch.lastX = static_cast<int>(point->x);
        touch.lastY = static_cast<int>(point->y);
    } else if (touch.active) {
        input.touchReleased = true;
        input.touchStartX = touch.startX;
        input.touchStartY = touch.startY;
        input.touchX = touch.lastX;
        input.touchY = touch.lastY;
        touch.active = false;
    }
    if (touch.active) {
        input.touchActive = true;
        input.touchStartX = touch.startX;
        input.touchStartY = touch.startY;
        input.touchX = touch.lastX;
        input.touchY = touch.lastY;
    }
    return input;
}
#endif

}  // namespace vitrine
