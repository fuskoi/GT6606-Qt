/**
 * Linux 产品常用激活辅助：改写启动脚本，并安装 ota_boot.sh（确认 / 回滚）。
 *
 * Ubuntu/SysV 注意：/etc/init.d/rcS 常见末尾为 `exec /etc/init.d/rc S`，
 * 其后追加的行永远不会执行。GT6606 等板子应优先挂 /etc/rc.local。
 */
#ifndef LIBOTA_LINUX_H
#define LIBOTA_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 删除含 drop_substrings 任一子串的行。
 * 若文件中存在以 exec 开头的行：在首个 exec 之前插入 "bin_path &"。
 * 否则在末尾追加。bin_path 为 NULL 时只做删除（清理死行）。
 * drop_substrings 以 NULL 结尾。成功 0，失败 -1。
 */
int ota_linux_rewrite_rcs(const char *rcs_path,
                          const char *bin_path,
                          const char *const *drop_substrings);

/**
 * 改写 /etc/rc.local（或等价路径）：删 drop 行，在 exit 0 前插入 "bin_path &"。
 * 文件不存在则创建带 shebang 的新脚本。成功 0，失败 -1。
 */
int ota_linux_rewrite_rc_local(const char *rc_local_path,
                               const char *bin_path,
                               const char *const *drop_substrings);

/**
 * 写入 ota_boot.sh，并挂到启动脚本。
 * rcs_path / rc_local_path 可其一为 NULL；Ubuntu 建议 rc_local_path="/etc/rc.local"。
 * qt_dir 非空时在 exec 前 export Qt 5.x 环境（如 /usr/local/htnice/Qt-5.8.0）。
 */
int ota_linux_install_boot_guard_ex(const char *rcs_path,
                                    const char *rc_local_path,
                                    const char *root_dir,
                                    const char *ota_dir_rel,
                                    const char *bin_name,
                                    const char *qt_dir,
                                    const char *const *drop_substrings);

/**
 * 兼容旧接口：只改 rcS、无 Qt 环境（等同 ex(rcs, NULL, ..., NULL, drops)）。
 */
int ota_linux_install_boot_guard(const char *rcs_path,
                                 const char *root_dir,
                                 const char *ota_dir_rel,
                                 const char *bin_name,
                                 const char *const *drop_substrings);

#ifdef __cplusplus
}
#endif

#endif /* LIBOTA_LINUX_H */
