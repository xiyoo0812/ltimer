#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <lua.h>
#include <lauxlib.h>

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)
#define TIME_NEAR_MASK (TIME_NEAR-1)
#define TIME_LEVEL_MASK (TIME_LEVEL-1)

struct timer_node {
    struct timer_node* next;
    uint32_t expire;
    uint64_t timer_id;
};

struct link_list {
    struct timer_node head;
    struct timer_node* tail;
};

struct timer {
    uint32_t time;
    struct link_list near[TIME_NEAR];
    struct link_list t[4][TIME_LEVEL];
};

static struct timer driver = { 0 };



static inline struct timer_node* link_clear(struct link_list* list) {
    struct timer_node* ret = list->head.next;
    list->head.next = 0;
    list->tail = &(list->head);
    return ret;
}

static void timer_init() {
    int i, j;
    memset(&driver, 0, sizeof(struct timer));
    for (i = 0; i < TIME_NEAR; i++) {
        link_clear(&driver.near[i]);
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < TIME_LEVEL; j++) {
            link_clear(&driver.t[i][j]);
        }
    }
}

static inline void link_node(struct link_list* list, struct timer_node* node) {
    list->tail->next = node;
    list->tail = node;
    node->next = 0;
}

static void add_node(struct timer_node* node) {
    uint32_t time = node->expire;
    uint32_t current_time = driver.time;
    if ((time | TIME_NEAR_MASK) == (current_time | TIME_NEAR_MASK)) {
        link_node(&driver.near[time & TIME_NEAR_MASK], node);
    }
    else {
        int i;
        uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT;
        for (i = 0; i < 3; i++) {
            if ((time | (mask - 1)) == (current_time | (mask - 1))) {
                break;
            }
            mask <<= TIME_LEVEL_SHIFT;
        }
        link_node(&driver.t[i][((time >> (TIME_NEAR_SHIFT + i * TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)], node);
    }
}

static void timer_add(uint64_t timer_id, int time) {
    struct timer_node* node = (struct timer_node*)malloc(sizeof(*node));
    if (time < 0)
        time = 0;
    node->timer_id = timer_id;
    node->expire = time + driver.time;
    add_node(node);
}

static void move_list(int level, int idx) {
    struct timer_node* current = link_clear(&driver.t[level][idx]);
    while (current) {
        struct timer_node* temp = current->next;
        add_node(current);
        current = temp;
    }
}

static void timer_shift() {
    int mask = TIME_NEAR;
    uint32_t ct = ++driver.time;
    if (ct == 0) {
        move_list(3, 0);
    }
    else {
        int i = 0;
        uint32_t time = ct >> TIME_NEAR_SHIFT;
        while ((ct & (mask - 1)) == 0) {
            int idx = time & TIME_LEVEL_MASK;
            if (idx != 0) {
                move_list(i, idx);
                break;
            }
            mask <<= TIME_LEVEL_SHIFT;
            time >>= TIME_LEVEL_SHIFT;
            ++i;
        }
    }
}

static inline void link_free(struct link_list* list) {
    struct timer_node* current = link_clear(list);
    while (current) {
        struct timer_node* temp = current;
        current = current->next;
        free(temp);
    }
    link_clear(list);
}

static inline void dispatch_list(lua_State* L, struct timer_node* current, int* n) {
    do {
        struct timer_node* temp = current;
        current = current->next;

        ++* n;
        lua_pushinteger(L, temp->timer_id);
        lua_rawseti(L, -2, *n);

        free(temp);
    } while (current);
}

static void timer_execute(lua_State* L, int* n) {
    int idx = driver.time & TIME_NEAR_MASK;
    while (driver.near[idx].head.next) {
        struct timer_node* current = link_clear(&driver.near[idx]);
        dispatch_list(L, current, n);
    }
}

static int ldestory(lua_State* L) {
    int i, j;
    for (i = 0; i < TIME_NEAR; i++) {
        link_free(&driver.near[i]);
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < TIME_LEVEL; j++) {
            link_free(&driver.t[i][j]);
        }
    }
    return 0;
}

static int linsert(lua_State* L) {
    uint64_t timer_id = luaL_checkinteger(L, 1);
    int time = luaL_checkinteger(L, 2);
    timer_add(timer_id, time);
    return 0;
}

static int lupdate(lua_State* L) {
    int elapse = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    // try to dispatch timeout 0 (rare condition)
    int n = 0;
    timer_execute(L, &n);
    for (int i = 0; i < elapse; i++) {
        // shift time first, and then dispatch timer message
        timer_shift();
        timer_execute(L, &n);
    }
    return 0;
}

#ifdef WIN32
#include <windows.h>
int gettimeofday(struct timeval* tp, void* tzp){
    time_t clock;
    struct tm tm;
    SYSTEMTIME wtm;
    GetLocalTime(&wtm);
    tm.tm_year = wtm.wYear - 1900;
    tm.tm_mon = wtm.wMonth - 1;
    tm.tm_mday = wtm.wDay;
    tm.tm_hour = wtm.wHour;
    tm.tm_min = wtm.wMinute;
    tm.tm_sec = wtm.wSecond;
    tm.tm_isdst = -1;
    clock = mktime(&tm);
    tp->tv_sec = clock;
    tp->tv_usec = wtm.wMilliseconds * 1000;
    return (0);
}
#define usleep Sleep 
#else
#include<unistd.h>
#include <sys/time.h>
#endif

static int ltime(lua_State* L) {
    struct timeval now;
    gettimeofday(&now, NULL);
    uint64_t now_ms = (uint64_t)now.tv_sec * 1000 + now.tv_usec / 1000;
    lua_pushinteger(L, now_ms);
    lua_pushinteger(L, now.tv_sec);
    return 2;
}

static int lsleep(lua_State* L) {
    size_t ms = luaL_checkinteger(L, 1);
    usleep(ms);
    return 0;
}

#ifdef _MSC_VER
#define LTIMER_API _declspec(dllexport)
#else
#define LTIMER_API extern
#endif

LTIMER_API int luaopen_ltimer(lua_State* L) {
    luaL_checkversion(L);
    timer_init();
    luaL_Reg l[] = {
        { "destory", ldestory },
        { "insert" , linsert },
        { "update", lupdate },
        { "sleep", lsleep },
        { "time", ltime },
        { NULL, NULL },
    };
    luaL_newlib(L, l);
    return 1;
}
