#pragma once

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace vitrine {

struct Input {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool accept = false;
    bool back = false;
    bool search = false;
    bool sort = false;
    bool previousGenre = false;
    bool nextGenre = false;
    bool viewMode = false;
    bool favorite = false;
    bool backlog = false;
    bool surprise = false;
    bool sync = false;
    bool quit = false;
    bool touchBegan = false;
    bool touchActive = false;
    bool touchReleased = false;
    int touchStartX = 0;
    int touchStartY = 0;
    int touchX = 0;
    int touchY = 0;
};

enum class TouchGestureDirection {
    None,
    Left,
    Right,
    Up,
    Down,
};

TouchGestureDirection touchGestureDirection(const Input& input, int minimumDistance = 64);

int selectionForTouchScroll(int touchScrollY, int rowStride, int columns,
                            int controllerRows, int previousSelection, int itemCount);
int touchScrollForSelection(int selection, int rowStride, int columns,
                            int controllerRows, int maximumScroll);

#ifdef __SWITCH__
Input readSwitchInput(PadState& pad);
#endif

}  // namespace vitrine
