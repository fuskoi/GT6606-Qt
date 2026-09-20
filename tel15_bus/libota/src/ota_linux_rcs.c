#include "ota_linux.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef OTA_PATH_MAX
#define OTA_PATH_MAX 260
#endif

static int line_should_drop(const char *line, const char *const *drops)
{
    size_t i = 0;

    if (line == NULL || drops == NULL) {
        return 0;
    }
    for (i = 0; drops[i] != NULL; i++) {
        if (drops[i][0] != '\0' && strstr(line, drops[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static const char *skip_ws(const char *p)
{
    while (p != NULL && (*p == ' ' || *p == '\t')) {
        p++;
    }
    return p;
}

/* Debian/SysV rcS 常见：exec /etc/init.d/rc S —— 其后命令不会执行 */
static int line_is_exec(const char *line)
{
    const char *p = skip_ws(line);

    if (p == NULL) {
        return 0;
    }
    return (strncmp(p, "exec ", 5) == 0 || strncmp(p, "exec\t", 5) == 0);
}

/* rc.local 常见末行：exit 0 */
static int line_is_exit(const char *line)
{
    const char *p = skip_ws(line);

    if (p == NULL) {
        return 0;
    }
    if (strncmp(p, "exit", 4) != 0) {
        return 0;
    }
    p += 4;
    return (*p == '\0' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n');
}

static void write_launch_line(FILE *fp, const char *bin_path)
{
    /* nohup + 脱离 tty，避免父 shell exec/退出时被 SIGHUP 杀掉 */
    fprintf(fp, "nohup %s >/tmp/tel15_ota_boot.log 2>&1 </dev/null &\n", bin_path);
}

int ota_linux_rewrite_rcs(const char *rcs_path,
                          const char *bin_path,
                          const char *const *drop_substrings)
{
    char tmp_path[OTA_PATH_MAX];
    char line[512];
    FILE *fp_in = NULL;
    FILE *fp_out = NULL;
    int inserted = 0;
    int strip_only = (bin_path == NULL || bin_path[0] == '\0');

    if (rcs_path == NULL) {
        return -1;
    }
    if ((size_t)snprintf(tmp_path, sizeof(tmp_path), "%s.ota.tmp", rcs_path) >= sizeof(tmp_path)) {
        return -1;
    }

    fp_in = fopen(rcs_path, "rb");
    if (fp_in == NULL) {
        if (strip_only) {
            return 0;
        }
        /* rc.local 可能尚不存在：新建 */
        fp_out = fopen(tmp_path, "wb");
        if (fp_out == NULL) {
            return -1;
        }
        fprintf(fp_out, "#!/bin/sh\n");
        write_launch_line(fp_out, bin_path);
        fprintf(fp_out, "exit 0\n");
        fclose(fp_out);
        if (rename(tmp_path, rcs_path) != 0) {
            remove(tmp_path);
            return -1;
        }
        (void)chmod(rcs_path, 0755);
        return 0;
    }

    fp_out = fopen(tmp_path, "wb");
    if (fp_out == NULL) {
        fclose(fp_in);
        return -1;
    }

    while (fgets(line, (int)sizeof(line), fp_in) != NULL) {
        if (line_should_drop(line, drop_substrings)) {
            continue;
        }
        if (!strip_only && !inserted && (line_is_exec(line) || line_is_exit(line))) {
            write_launch_line(fp_out, bin_path);
            inserted = 1;
        }
        fputs(line, fp_out);
    }
    if (!strip_only && !inserted) {
        write_launch_line(fp_out, bin_path);
    }
    fclose(fp_in);
    fclose(fp_out);

    (void)remove(rcs_path);
    if (rename(tmp_path, rcs_path) != 0) {
        remove(tmp_path);
        return -1;
    }
    (void)chmod(rcs_path, 0755);
    return 0;
}

static void write_exec_bin(FILE *fp)
{
    /* 优先走安装目录 run_tel15.sh（Qt 环境 + 选路上行），否则直接 exec 槽二进制 */
    fprintf(fp, "if [ -x \"$BIN\" ]; then\n");
    fprintf(fp, "  if [ -x \"$APP_ROOT/run_tel15.sh\" ]; then\n");
    fprintf(fp, "    exec \"$APP_ROOT/run_tel15.sh\" \"$BIN\"\n");
    fprintf(fp, "  fi\n");
    fprintf(fp, "  exec \"$BIN\"\n");
    fprintf(fp, "fi\n");
}

int ota_linux_install_boot_guard(const char *rcs_path,
                                 const char *root_dir,
                                 const char *ota_dir_rel,
                                 const char *bin_name,
                                 const char *const *drop_substrings)
{
    char ota_dir[OTA_PATH_MAX];
    char script_path[OTA_PATH_MAX];
    char slot_a[OTA_PATH_MAX];
    char slot_b[OTA_PATH_MAX];
    char active_path[OTA_PATH_MAX];
    char pending_path[OTA_PATH_MAX];
    FILE *fp = NULL;
    const char *root = (root_dir != NULL) ? root_dir : "";
    const char *ota_rel = (ota_dir_rel != NULL && ota_dir_rel[0] != '\0') ? ota_dir_rel : "/ota";
    const char *bname = (bin_name != NULL && bin_name[0] != '\0') ? bin_name : "DOPPLER";

    if (rcs_path == NULL) {
        return -1;
    }

    snprintf(ota_dir, sizeof(ota_dir), "%s%s", root, ota_rel);
    snprintf(script_path, sizeof(script_path), "%s/ota_boot.sh", ota_dir);
    snprintf(slot_a, sizeof(slot_a), "%s/slot_a/%s", ota_dir, bname);
    snprintf(slot_b, sizeof(slot_b), "%s/slot_b/%s", ota_dir, bname);
    snprintf(active_path, sizeof(active_path), "%s/active", ota_dir);
    snprintf(pending_path, sizeof(pending_path), "%s/boot_pending", ota_dir);

    fp = fopen(script_path, "wb");
    if (fp == NULL) {
        return -1;
    }
    /*
     * 启动守卫：
     * - 无 boot_pending：按 active 启动对应槽
     * - 有 pending 且 tries>=1：回滚 prev 槽并重启
     * - 有 pending 且 tries==0：记 tries=1 后试新槽（由应用 ota_confirm_boot 清 pending）
     */
    fprintf(fp, "#!/bin/sh\n");
    fprintf(fp, "APP_ROOT=\"%s\"\n", root);
    fprintf(fp, "OTA_DIR=\"%s\"\n", ota_dir);
    fprintf(fp, "ACTIVE_FILE=\"%s\"\n", active_path);
    fprintf(fp, "PENDING=\"%s\"\n", pending_path);
    fprintf(fp, "BIN_A=\"%s\"\n", slot_a);
    fprintf(fp, "BIN_B=\"%s\"\n", slot_b);
    fprintf(fp, "cd \"$APP_ROOT\" 2>/dev/null || true\n");
    fprintf(fp, "i=0\n");
    fprintf(fp, "while [ \"$i\" -lt 30 ]; do\n");
    fprintf(fp, "  [ -e /dev/fb0 ] && break\n");
    fprintf(fp, "  i=$((i + 1))\n");
    fprintf(fp, "  sleep 1\n");
    fprintf(fp, "done\n");
    fprintf(fp, "SLOT=A\n");
    fprintf(fp, "if [ -f \"$ACTIVE_FILE\" ]; then\n");
    fprintf(fp, "  SLOT=`cat \"$ACTIVE_FILE\" 2>/dev/null | tr -d '\\r\\n'`\n");
    fprintf(fp, "fi\n");
    fprintf(fp, "if [ \"$SLOT\" = \"B\" ] || [ \"$SLOT\" = \"b\" ] || [ \"$SLOT\" = \"1\" ]; then\n");
    fprintf(fp, "  BIN=\"$BIN_B\"\n");
    fprintf(fp, "  CUR=B\n");
    fprintf(fp, "else\n");
    fprintf(fp, "  BIN=\"$BIN_A\"\n");
    fprintf(fp, "  CUR=A\n");
    fprintf(fp, "fi\n");
    fprintf(fp, "if [ -f \"$PENDING\" ]; then\n");
    fprintf(fp, "  TRIES=`sed -n '1p' \"$PENDING\" 2>/dev/null`\n");
    fprintf(fp, "  PREV_SLOT=`sed -n '2p' \"$PENDING\" 2>/dev/null`\n");
    fprintf(fp, "  PREV_BIN=`sed -n '3p' \"$PENDING\" 2>/dev/null`\n");
    fprintf(fp, "  if [ -z \"$TRIES\" ]; then TRIES=0; fi\n");
    fprintf(fp, "  if [ \"$TRIES\" -ge 1 ]; then\n");
    fprintf(fp, "    if [ \"$PREV_SLOT\" = \"B\" ] || [ \"$PREV_SLOT\" = \"b\" ]; then\n");
    fprintf(fp, "      echo B > \"$ACTIVE_FILE\"\n");
    fprintf(fp, "      BIN=\"$BIN_B\"\n");
    fprintf(fp, "    else\n");
    fprintf(fp, "      echo A > \"$ACTIVE_FILE\"\n");
    fprintf(fp, "      BIN=\"$BIN_A\"\n");
    fprintf(fp, "    fi\n");
    fprintf(fp, "    if [ -n \"$PREV_BIN\" ] && [ -x \"$PREV_BIN\" ]; then BIN=\"$PREV_BIN\"; fi\n");
    fprintf(fp, "    rm -f \"$PENDING\"\n");
    fprintf(fp, "    sync\n");
    write_exec_bin(fp);
    fprintf(fp, "    exit 1\n");
    fprintf(fp, "  fi\n");
    fprintf(fp, "  {\n");
    fprintf(fp, "    echo 1\n");
    fprintf(fp, "    echo \"$PREV_SLOT\"\n");
    fprintf(fp, "    echo \"$PREV_BIN\"\n");
    fprintf(fp, "  } > \"$PENDING\"\n");
    fprintf(fp, "  sync\n");
    fprintf(fp, "fi\n");
    write_exec_bin(fp);
    fprintf(fp, "echo \"ota_boot: missing bin $BIN\" >&2\n");
    fprintf(fp, "exit 1\n");
    fclose(fp);
    (void)chmod(script_path, 0755);

    return ota_linux_rewrite_rcs(rcs_path, script_path, drop_substrings);
}
