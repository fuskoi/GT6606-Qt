#include "ota_linux.h"

#include <ctype.h>
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

static int line_is_exec(const char *line)
{
    const char *p = line;

    if (p == NULL) {
        return 0;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (strncmp(p, "exec", 4) == 0 &&
            (p[4] == '\0' || isspace((unsigned char)p[4]))) ? 1 : 0;
}

static int line_is_exit_zero(const char *line)
{
    const char *p = line;

    if (p == NULL) {
        return 0;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (strncmp(p, "exit", 4) != 0 || !isspace((unsigned char)p[4])) {
        return 0;
    }
    p += 4;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (p[0] == '0' && (p[1] == '\0' || p[1] == '\n' || p[1] == '\r' ||
                            isspace((unsigned char)p[1])));
}

static void emit_launch(FILE *fp_out, const char *bin_path)
{
    if (fp_out != NULL && bin_path != NULL && bin_path[0] != '\0') {
        fprintf(fp_out, "%s &\n", bin_path);
    }
}

/*
 * 通用改写：删 drops；若有 exec 则插在首个 exec 前；否则若有 exit 0 插在其前；
 * 否则末尾追加。bin_path==NULL 只 scrub。
 */
static int rewrite_script_file(const char *path,
                               const char *bin_path,
                               const char *const *drop_substrings,
                               int create_if_missing)
{
    char tmp_path[OTA_PATH_MAX];
    char line[512];
    FILE *fp_in = NULL;
    FILE *fp_out = NULL;
    int inserted = 0;
    int saw_exec = 0;
    int empty_new = 0;

    if (path == NULL) {
        return -1;
    }
    if ((size_t)snprintf(tmp_path, sizeof(tmp_path), "%s.ota.tmp", path) >= sizeof(tmp_path)) {
        return -1;
    }

    fp_in = fopen(path, "rb");
    if (fp_in == NULL) {
        if (!create_if_missing || bin_path == NULL || bin_path[0] == '\0') {
            return -1;
        }
        empty_new = 1;
    }

    fp_out = fopen(tmp_path, "wb");
    if (fp_out == NULL) {
        if (fp_in != NULL) {
            fclose(fp_in);
        }
        return -1;
    }

    if (empty_new) {
        fprintf(fp_out, "#!/bin/sh\n");
        emit_launch(fp_out, bin_path);
        fprintf(fp_out, "exit 0\n");
        fclose(fp_out);
        if (rename(tmp_path, path) != 0) {
            remove(tmp_path);
            return -1;
        }
        (void)chmod(path, 0755);
        return 0;
    }

    /* 先扫一遍是否存在 exec（Ubuntu rcS） */
    while (fgets(line, (int)sizeof(line), fp_in) != NULL) {
        if (line_is_exec(line)) {
            saw_exec = 1;
            break;
        }
    }
    rewind(fp_in);

    while (fgets(line, (int)sizeof(line), fp_in) != NULL) {
        if (line_should_drop(line, drop_substrings)) {
            continue;
        }
        if (!inserted && bin_path != NULL && bin_path[0] != '\0') {
            if (saw_exec && line_is_exec(line)) {
                emit_launch(fp_out, bin_path);
                inserted = 1;
            } else if (!saw_exec && line_is_exit_zero(line)) {
                emit_launch(fp_out, bin_path);
                inserted = 1;
            }
        }
        fputs(line, fp_out);
    }
    if (!inserted) {
        emit_launch(fp_out, bin_path);
    }

    fclose(fp_in);
    fclose(fp_out);

    (void)remove(path);
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }
    (void)chmod(path, 0755);
    return 0;
}

int ota_linux_rewrite_rcs(const char *rcs_path,
                          const char *bin_path,
                          const char *const *drop_substrings)
{
    /* rcS 一般已存在；不要误创建空 rcS */
    return rewrite_script_file(rcs_path, bin_path, drop_substrings, 0);
}

int ota_linux_rewrite_rc_local(const char *rc_local_path,
                               const char *bin_path,
                               const char *const *drop_substrings)
{
    if (bin_path == NULL || bin_path[0] == '\0') {
        return -1;
    }
    return rewrite_script_file(rc_local_path, bin_path, drop_substrings, 1);
}

static void emit_qt_env(FILE *fp, const char *qt_dir)
{
    if (fp == NULL || qt_dir == NULL || qt_dir[0] == '\0') {
        return;
    }
    fprintf(fp, "QTDIR=\"%s\"\n", qt_dir);
    fprintf(fp, "export QT_QPA_PLATFORM_PLUGIN_PATH=$QTDIR/plugins/\n");
    fprintf(fp, "export QML2_IMPORT_PATH=$QTDIR/qml/\n");
    fprintf(fp, "export LD_LIBRARY_PATH=$QTDIR/lib:${LD_LIBRARY_PATH}\n");
    fprintf(fp, "if [ -z \"$QT_QPA_PLATFORM\" ]; then\n");
    fprintf(fp, "  export QT_QPA_PLATFORM=linuxfb\n");
    fprintf(fp, "fi\n");
}

static void emit_exec_bin(FILE *fp)
{
    /* cd 到二进制目录，便于相对资源；GetAppDir 仍靠 /proc/self/exe */
    fprintf(fp, "if [ -x \"$BIN\" ]; then\n");
    fprintf(fp, "  BIN_DIR=`dirname \"$BIN\"`\n");
    fprintf(fp, "  cd \"$BIN_DIR\" 2>/dev/null || true\n");
    fprintf(fp, "  exec \"$BIN\"\n");
    fprintf(fp, "fi\n");
}

int ota_linux_install_boot_guard_ex(const char *rcs_path,
                                    const char *rc_local_path,
                                    const char *root_dir,
                                    const char *ota_dir_rel,
                                    const char *bin_name,
                                    const char *qt_dir,
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
    int ok = 0;

    if (rcs_path == NULL && rc_local_path == NULL) {
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

    fprintf(fp, "#!/bin/sh\n");
    fprintf(fp, "OTA_DIR=\"%s\"\n", ota_dir);
    fprintf(fp, "ACTIVE_FILE=\"%s\"\n", active_path);
    fprintf(fp, "PENDING=\"%s\"\n", pending_path);
    fprintf(fp, "BIN_A=\"%s\"\n", slot_a);
    fprintf(fp, "BIN_B=\"%s\"\n", slot_b);
    emit_qt_env(fp, qt_dir);
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
    emit_exec_bin(fp);
    fprintf(fp, "    exit 1\n");
    fprintf(fp, "  fi\n");
    fprintf(fp, "  {\n");
    fprintf(fp, "    echo 1\n");
    fprintf(fp, "    echo \"$PREV_SLOT\"\n");
    fprintf(fp, "    echo \"$PREV_BIN\"\n");
    fprintf(fp, "  } > \"$PENDING\"\n");
    fprintf(fp, "  sync\n");
    fprintf(fp, "fi\n");
    emit_exec_bin(fp);
    fprintf(fp, "exit 1\n");
    fclose(fp);
    (void)chmod(script_path, 0755);

    /* Ubuntu：优先 rc.local；同时 scrub rcS 里无效的旧 ota 行 */
    if (rc_local_path != NULL) {
        if (ota_linux_rewrite_rc_local(rc_local_path, script_path, drop_substrings) == 0) {
            ok = 1;
        }
        if (rcs_path != NULL) {
            /* 只清理 rcS 死行，不再往 exec 后追加 */
            (void)ota_linux_rewrite_rcs(rcs_path, NULL, drop_substrings);
        }
    } else if (rcs_path != NULL) {
        if (ota_linux_rewrite_rcs(rcs_path, script_path, drop_substrings) == 0) {
            ok = 1;
        }
    }

    return ok ? 0 : -1;
}

int ota_linux_install_boot_guard(const char *rcs_path,
                                 const char *root_dir,
                                 const char *ota_dir_rel,
                                 const char *bin_name,
                                 const char *const *drop_substrings)
{
    return ota_linux_install_boot_guard_ex(rcs_path, NULL, root_dir, ota_dir_rel,
                                           bin_name, NULL, drop_substrings);
}
