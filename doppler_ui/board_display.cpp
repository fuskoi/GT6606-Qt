#include "board_display.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/fb.h>
#include <sys/ioctl.h>

namespace {

bool write_file(const char *path, const char *text)
{
    const int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const size_t n = strlen(text);
    const ssize_t w = ::write(fd, text, n);
    ::close(fd);
    return (w == (ssize_t)n);
}

bool try_backlight(int on)
{
    DIR *dir = opendir("/sys/class/backlight");
    if (!dir)
        return false;

    bool ok = false;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        char path[256];
        char maxPath[256];
        snprintf(path, sizeof(path), "/sys/class/backlight/%s/brightness", ent->d_name);
        if (on) {
            int maxv = 255;
            snprintf(maxPath, sizeof(maxPath),
                     "/sys/class/backlight/%s/max_brightness", ent->d_name);
            FILE *fp = fopen(maxPath, "r");
            if (fp) {
                if (fscanf(fp, "%d", &maxv) != 1)
                    maxv = 255;
                fclose(fp);
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", maxv > 0 ? maxv : 255);
            if (write_file(path, buf))
                ok = true;
        } else {
            if (write_file(path, "0"))
                ok = true;
        }
    }
    closedir(dir);
    return ok;
}

bool try_fb_sysfs(int blank)
{
    return write_file("/sys/class/graphics/fb0/blank", blank ? "1" : "0");
}

bool try_fb_ioctl(int blank)
{
    const int fd = ::open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return false;
    const int arg = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
    const int ret = ioctl(fd, FBIOBLANK, arg);
    ::close(fd);
    return (ret == 0);
}

} /* namespace */

namespace BoardDisplay {

void blank(void)
{
    (void)try_backlight(0);
    if (!try_fb_sysfs(1))
        (void)try_fb_ioctl(1);
}

void unblank(void)
{
    if (!try_fb_sysfs(0))
        (void)try_fb_ioctl(0);
    (void)try_backlight(1);
}

}
