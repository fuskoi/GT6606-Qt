#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

/* 软件息屏遮罩用：不写 backlight，避免亮度被写成 0 后难恢复。 */
namespace BoardDisplay {

void blank(void);
void unblank(void);
bool isBlanked(void);

}

#endif
