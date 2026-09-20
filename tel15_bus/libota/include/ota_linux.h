/**
 * Linux 产品常用激活辅助：改写 BusyBox/sysv 风格 /etc/init.d/rcS，
 * 并安装 ota_boot.sh（启动确认 / 失败回滚）。
 */
#ifndef LIBOTA_LINUX_H
#define LIBOTA_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 删除含 drop_substrings 任一子串的行，末尾追加 "bin_path &\n"，chmod 0755。
 * drop_substrings 以 NULL 结尾。
 * 成功返回 0，失败返回 -1。
 */
int ota_linux_rewrite_rcs(const char *rcs_path,
                          const char *bin_path,
                          const char *const *drop_substrings);

/**
 * 在 ota_dir 下写入 ota_boot.sh（根据 active / boot_pending 启动或回滚），
 * 并把 rcS 改成启动该脚本（不再直接挂二进制）。
 * root_dir 可为 ""；ota_dir_rel 如 "/ota"；bin_name 如 "DOPPLER"。
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
