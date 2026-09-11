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
};

#ifdef __SWITCH__
Input readSwitchInput(PadState& pad);
#endif

}  // namespace vitrine
