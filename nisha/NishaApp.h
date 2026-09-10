#ifndef NISHA_APP_H
#define NISHA_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* 启动通辽泥沙升降业务（原 main 的 Init + 线程）。成功返回 0，失败返回 -1。 */
int Nisha_BusinessStart(void);

#ifdef __cplusplus
}
#endif

#endif /* NISHA_APP_H */
