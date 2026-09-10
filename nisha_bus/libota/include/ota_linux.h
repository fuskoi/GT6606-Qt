/**
 * Linux 产品常用激活辅助：改写 BusyBox/sysv 风格启动脚本，
 * 并安装 ota_boot.sh（启动确认 / 失败回滚）。
 *
 * 推荐挂到 /etc/rc.local（挂载完成后再启）；勿只写在 rcS 的 exec 之前。
 */
#ifndef LIBOTA_LINUX_H
#define LIBOTA_LINUX_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 删除含 drop_substrings 任一子串的行；若 bin_path 非空则写入启动行：
 *   nohup bin_path >/tmp/nisha_ota_boot.log 2>&1 </dev/null &
 * 插在首个 exec / exit 之前（避免被 exec 吃掉或写在 exit 之后），否则追加。
 * bin_path 为空时仅做删除（用于清掉误写在 rcS 里的旧行）。
 * 文件不存在且需写入时会新建。chmod 0755。
 * drop_substrings 以 NULL 结尾。成功返回 0，失败返回 -1。
 */
int ota_linux_rewrite_rcs(const char *rcs_path,
                          const char *bin_path,
                          const char *const *drop_substrings);

/**
 * 在 ota_dir 下写入 ota_boot.sh（根据 active / boot_pending 启动或回滚），
 * 并把目标脚本（建议 /etc/rc.local）改成启动该脚本。
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
