#ifndef DOPPLER_APP_H
#define DOPPLER_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start Doppler business threads/timers. Returns 0 on success, -1 on hard failure. */
int Doppler_BusinessStart(void);

#ifdef __cplusplus
}
#endif

#endif /* DOPPLER_APP_H */
