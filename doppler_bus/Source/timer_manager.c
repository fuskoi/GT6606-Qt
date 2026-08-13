#include "timer_manager.h"
#include "log.h"

TimerManager manager;
static inline uint64_t get_current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / NS_PER_MS;
}


/* 哈希表操作 */
static void hash_insert(int key, int value) {
    int slot = key % 128;
    HashEntry* entry = malloc(sizeof(HashEntry));
    entry->key = key;
    entry->value = value;
    entry->next = manager.hash_table[slot];
    manager.hash_table[slot] = entry;
}

static int hash_get(int key) {
    int slot = key % 128;
    HashEntry* entry = manager.hash_table[slot];
    while (entry) {
        if (entry->key == key) return entry->value;
        entry = entry->next;
    }
    return -1;
}

static void hash_delete(int key) {
    int slot = key % 128;
    HashEntry** pp = &manager.hash_table[slot];
    while (*pp) {
        if ((*pp)->key == key) {
            HashEntry* to_free = *pp;
            *pp = to_free->next;
            free(to_free);
            return;
        }
        pp = &(*pp)->next;
    }
}







static void heap_swap(int a, int b) 
{
    // 先删除旧的哈希表条目
    hash_delete(manager.heap[a]->id);
    hash_delete(manager.heap[b]->id);
    
    // 交换堆中的定时器
    Timer* tmp = manager.heap[a];
    manager.heap[a] = manager.heap[b];
    manager.heap[b] = tmp;
    
    // 插入新的哈希表条目
    hash_insert(manager.heap[a]->id, a);
    hash_insert(manager.heap[b]->id, b);
}

static void heap_up(int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (manager.heap[index]->expire >= manager.heap[parent]->expire)
            break;
        heap_swap(index, parent);
        index = parent;
    }
}

static void heap_down(int index) {
    int smallest = index;
    while (1) {
        int left = 2 * index + 1;
        int right = 2 * index + 2;

        if (left < manager.size && 
            manager.heap[left]->expire < manager.heap[smallest]->expire)
            smallest = left;
        
        if (right < manager.size && 
            manager.heap[right]->expire < manager.heap[smallest]->expire)
            smallest = right;
        
        if (smallest == index) break;
        
        heap_swap(index, smallest);
        index = smallest;
    }
}



/* 定时器核心逻辑 */
static bool timer_reschedule(Timer* timer) {
    bool need_remove = false;
    if (timer->type == TIMER_REPEAT) {
        timer->expire += timer->interval;
        heap_up(manager.size - 1);
        heap_down(0);
    } else if (timer->type == TIMER_MULTI_SHOT) {
        if (--timer->remaining > 0) {
            timer->expire += timer->interval;
            heap_up(manager.size - 1);
            heap_down(0);
        } else {
            need_remove = true;
        }
    }
    return need_remove;
}

static void* timer_thread(void* arg) {
    struct epoll_event events[MAX_EVENTS];
    
    while (1) {
        int n = epoll_wait(manager.epoll_fd, events, MAX_EVENTS, -1);
        int i = 0;
        for (i = 0; i < n; i++) {
            Timer* timer = events[i].data.ptr;
            uint64_t exp;
            read(timer->fd, &exp, sizeof(exp));
            
            pthread_mutex_lock(&manager.lock);
            if (!timer->active) {
                pthread_mutex_unlock(&manager.lock);
                continue;
            }

            timer->callback(timer->arg);
            
            bool need_remove = false;
            struct itimerspec its ;
            memset(&its,0,sizeof(struct itimerspec));
            
            if (timer->type != TIMER_ONCE) {
                need_remove = timer_reschedule(timer);
                if (!need_remove && timer->type == TIMER_MULTI_SHOT) {
                    uint64_t interval_ms = timer->interval;
                    its.it_value.tv_sec = interval_ms / 1000;
                    its.it_value.tv_nsec = (interval_ms % 1000) * NS_PER_MS;
                    timerfd_settime(timer->fd, 0, &its, NULL);
                }
            } else {
                need_remove = true;
            }

            if (need_remove) {
                epoll_ctl(manager.epoll_fd, EPOLL_CTL_DEL, timer->fd, NULL);
                close(timer->fd);
                hash_delete(timer->id);
                
                int index = hash_get(timer->id);
                if (index != -1 && index < manager.size) {
                    heap_swap(index, manager.size-1);
                    manager.size--;
                    heap_down(index);
                }
                free(timer);
            }
            pthread_mutex_unlock(&manager.lock);
        }
    }
    return NULL;
}

/* 公共接口 */
void timer_init() {
    manager.capacity = INITIAL_CAPACITY;
    manager.size = 0;
    manager.next_id = 1;
    manager.heap = malloc(manager.capacity * sizeof(Timer*));
    memset(manager.hash_table, 0, sizeof(manager.hash_table));
    pthread_mutex_init(&manager.lock, NULL);
    manager.epoll_fd = epoll_create1(0);
    pthread_create(&manager.thread, NULL, timer_thread, NULL);
}

int add_timer(uint64_t delay, uint64_t interval, TimerType type, int count, 
             void (*cb)(void*), void* arg) {
    struct itimerspec its;
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    
    if (fd == -1) {
        LOG(LOG_DEBUG,"timerfd_create failed");
        return -1;
    }
    Timer* timer = malloc(sizeof(Timer));
    timer->id = manager.next_id++;
    timer->fd = fd;
    timer->type = type;
    timer->interval = interval;
    timer->remaining = count;
    timer->callback = cb;
    timer->arg = arg;
    timer->active = true;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    timer->expire = ts.tv_sec * 1000 + ts.tv_nsec / NS_PER_MS + delay;

    // 设置定时器
    uint64_t initial = delay;
    its.it_value.tv_sec = initial / 1000;
    its.it_value.tv_nsec = (initial % 1000) * NS_PER_MS;
    its.it_interval.tv_sec = interval / 1000;
    its.it_interval.tv_nsec = (interval % 1000) * NS_PER_MS;
    timerfd_settime(fd, 0, &its, NULL);

    // 添加到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = timer;
    epoll_ctl(manager.epoll_fd, EPOLL_CTL_ADD, fd, &ev);



    // 添加到堆
    pthread_mutex_lock(&manager.lock);
    if (manager.size >= manager.capacity) {
        manager.capacity *= 2;
        manager.heap = realloc(manager.heap, manager.capacity * sizeof(Timer*));
    }
    manager.heap[manager.size] = timer;
    hash_insert(timer->id, manager.size);
    manager.size++;
    heap_up(manager.size - 1);
    pthread_mutex_unlock(&manager.lock);
    LOG(LOG_DEBUG,"timer %d add SUCCESS",timer->id);
    return timer->id;
}

bool remove_timer(int timer_id) {
    pthread_mutex_lock(&manager.lock);
    
    // 获取当前索引（需确保哈希表最新）
    int index = hash_get(timer_id);
    if (index == -1 || index >= manager.size) {
        pthread_mutex_unlock(&manager.lock);
        LOG(LOG_ERROR,"timer %d remove FAILED,index ERROR",timer_id);
        return false;
    }

    Timer* timer = manager.heap[index];
    if (!timer->active || timer->id != timer_id) {  // 二次验证ID
        pthread_mutex_unlock(&manager.lock);
        LOG(LOG_ERROR,"timer %d remove FAILED,timer id ERROR",timer_id);
        return false;
    }

    // 标记并清理定时器
    timer->active = false;
    epoll_ctl(manager.epoll_fd, EPOLL_CTL_DEL, timer->fd, NULL);
    close(timer->fd);
    hash_delete(timer_id);

    // 与末尾元素交换后调整堆
    heap_swap(index, manager.size - 1);
    manager.size--;
    
    // 仅当删除的不是最后一个元素时才需调整堆
    if (index < manager.size) {
        heap_down(index);
        heap_up(index);  // 可能需向上或向下调整
    }
    
    free(timer);
    pthread_mutex_unlock(&manager.lock);
    LOG(LOG_DEBUG,"timer %d remove SUCCESS",timer_id);
    return true;
}

void timer_cleanup() {
    pthread_mutex_lock(&manager.lock);
    int i = 0;
    for (i = 0; i < manager.size; i++) {
        close(manager.heap[i]->fd);
        free(manager.heap[i]);
    }
    free(manager.heap);
    for (i = 0; i < 128; i++) {
        HashEntry* entry = manager.hash_table[i];
        while (entry) {
            HashEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    pthread_mutex_unlock(&manager.lock);
}


/* // 使用示例
void sample_callback(void* arg) {
    printf("Timer fired! Data: %s\n", (char*)arg);
}

int main() {
    timer_init();

    add_timer(1000, 500, TIMER_REPEAT, 0, sample_callback, "Repeat Timer");
    add_timer(1000, 500, TIMER_REPEAT, 0, sample_callback, "Repeat Timer2");
    add_timer(1000, 500, TIMER_REPEAT, 0, sample_callback, "Repeat Timer3");
    add_timer(2000, 0, TIMER_ONCE, 0, sample_callback, "One-shot Timer");
    add_timer(2000, 0, TIMER_ONCE, 0, sample_callback, "One-shot Timer2");
    add_timer(2000, 0, TIMER_ONCE, 0, sample_callback, "One-shot Timer3");
    add_timer(2000, 0, TIMER_ONCE, 0, sample_callback, "One-shot Timer4");
    add_timer(3000, 1000, TIMER_MULTI_SHOT, 5, sample_callback, "Multi-shot Timer");

    pthread_join(manager.thread, NULL);
    return 0;
} */


