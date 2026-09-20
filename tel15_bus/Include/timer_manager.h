#ifndef _TIMER_MANAGER_H
#define _TIMER_MANAGER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#define MAX_EVENTS 10
#define NS_PER_MS 1000000
#define US_PER_MS 1000
#define INITIAL_CAPACITY 128

typedef enum {
    TIMER_ONCE,
    TIMER_REPEAT,
    TIMER_MULTI_SHOT
} TimerType;

typedef struct Timer {
    int id;
    int fd;
    TimerType type;
    uint64_t interval;
    int remaining;
    void (*callback)(void*);
    void* arg;
    uint64_t expire;
    bool active;
} Timer;

typedef struct HashEntry {
    int key;
    int value;
    struct HashEntry* next;
} HashEntry;

typedef struct {
    Timer** heap;
    int capacity;
    int size;
    pthread_mutex_t lock;
    int epoll_fd;
    pthread_t thread;
    int next_id;
    HashEntry* hash_table[128];
} TimerManager;

extern TimerManager manager;


void timer_init() ;

int add_timer(uint64_t delay, uint64_t interval, TimerType type, int count, void (*cb)(void*), void* arg) ;
bool remove_timer(int timer_id);
void timer_cleanup();
#endif