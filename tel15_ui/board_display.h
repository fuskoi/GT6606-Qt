#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

/* GT6606 息屏/亮屏：优先 backlight / fb blank，失败也不影响上层黑屏遮罩。 */
namespace BoardDisplay {

/* 关闭背光并尝试关闭 framebuffer 显示（硬件息屏）。 */
void blank(void);

/* 恢复 framebuffer 与背光（硬件亮屏）。 */
void unblank(void);

}

#endif
