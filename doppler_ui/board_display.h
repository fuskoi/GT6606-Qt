#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

/* GT6606 息屏/亮屏：优先 backlight / fb blank，失败也不影响上层黑屏遮罩。 */
namespace BoardDisplay {

void blank(void);
void unblank(void);

}

#endif
