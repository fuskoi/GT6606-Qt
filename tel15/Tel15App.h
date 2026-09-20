#ifndef TEL15_APP_H
#define TEL15_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Start TEL-15 channel-flow business (original main without while(1)).
 * Returns 0 on success, -1 on hard failure. */
int Tel15_BusinessStart(void);

#ifdef __cplusplus
}
#endif

#endif /* TEL15_APP_H */
