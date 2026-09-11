#include "input.hpp"

namespace vitrine {

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
    return input;
}
#endif

}  // namespace vitrine
