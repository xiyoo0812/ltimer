#include <list>
#include <thread>
#include <chrono>

extern "C"
{
    #include <lua.h>
    #include <lauxlib.h>
}

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)
#define TIME_NEAR_MASK (TIME_NEAR-1)
#define TIME_LEVEL_MASK (TIME_LEVEL-1)

struct timer_node {
    size_t expire;
    uint64_t timer_id;
};

typedef std::list<timer_node> timer_list;

class lua_timer {
public:
    void shift();
    void move_list(uint32_t level, uint32_t idx);
    void add_timer(uint64_t timer_id, size_t escape);
    void execute(lua_State* L, uint32_t& size);

protected:
    void reset();
    void add_node(timer_node& node);

protected:
    size_t time = 0;
    timer_list near[TIME_NEAR];
    timer_list t[4][TIME_LEVEL];
};

void lua_timer::reset() {
    for (uint32_t i = 0; i < TIME_NEAR; i++) {
        near[i].clear();
    }
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = 0; j < TIME_LEVEL; j++) {
            near[i].clear();
        }
    }
}

void lua_timer::add_node(timer_node& node) {
    size_t expire = node.expire;
    if ((expire | TIME_NEAR_MASK) == (time | TIME_NEAR_MASK)) {
        near[expire & TIME_NEAR_MASK].push_back(node);
    }
    else {
        uint32_t i;
        uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT;
        for (i = 0; i < 3; i++) {
            if ((expire | (mask - 1)) == (time | (mask - 1))) {
                break;
            }
            expire <<= TIME_LEVEL_SHIFT;
        }
        t[i][((expire >> (TIME_NEAR_SHIFT + i * TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)].push_back(node);
    }
}

void lua_timer::add_timer(uint64_t timer_id, size_t escape) {
    add_node(timer_node { time + (escape < 0 ? 0 : escape), timer_id });
}

void lua_timer::move_list(uint32_t level, uint32_t idx) {
    timer_list& list = t[level][idx];
    for (auto node : t[level][idx]) {
        add_node(node);
    }
    list.clear();
}

void lua_timer::shift() {
    int mask = TIME_NEAR;
    size_t ct = ++time;
    if (ct == 0) {
        move_list(3, 0);
    }
    else {
        uint32_t i = 0;
        size_t time = ct >> TIME_NEAR_SHIFT;
        while ((ct & (mask - 1)) == 0) {
            uint32_t idx = time & TIME_LEVEL_MASK;
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

void lua_timer::execute(lua_State* L, uint32_t& size) {
    uint32_t idx = time & TIME_NEAR_MASK;
    for (auto node : near[idx]) {
        lua_pushinteger(L, node.timer_id);
        lua_rawseti(L, -2, ++size);
    }
    near[idx].clear();
}

static int ldestory(lua_State* L) {
    lua_timer* driver = (lua_timer*)lua_touserdata(L, 1);
    if (driver) {
        delete driver;
        driver = nullptr;
    }
    return 0;
}

static int linsert(lua_State* L) {
    lua_timer* driver = (lua_timer*)lua_touserdata(L, 1);
    if (driver) {
        uint64_t timer_id = luaL_checkinteger(L, 2);
        size_t escape = luaL_checkinteger(L, 3);
        driver->add_timer(timer_id, escape);
    }
    return 0;
}

static int lupdate(lua_State* L) {
    lua_timer* driver = (lua_timer*)lua_touserdata(L, 1);
    if (driver) {
        size_t elapse = luaL_checkinteger(L, 2);
        lua_newtable(L);
        uint32_t size = 0;
        driver->execute(L, size);
        for (size_t i = 0; i < elapse; i++) {
            driver->shift();
            driver->execute(L, size);
        }
        return 1;
    }
    return 0;
}

using namespace std::chrono;
static int ltime(lua_State* L) {
    system_clock::duration dur = system_clock::now().time_since_epoch();
    lua_pushinteger(L, duration_cast<milliseconds>(dur).count());
    lua_pushinteger(L, duration_cast<seconds>(dur).count());
    return 2;
}

static int lnow(lua_State* L) {
    system_clock::duration dur = system_clock::now().time_since_epoch();
    lua_pushinteger(L, duration_cast<seconds>(dur).count());
    return 1;
}

static int lnow_ms(lua_State* L) {
    system_clock::duration dur = system_clock::now().time_since_epoch();
    lua_pushinteger(L, duration_cast<milliseconds>(dur).count());
    return 1;
}

static int lsteady(lua_State* L) {
    steady_clock::duration dur = steady_clock::now().time_since_epoch();
    lua_pushinteger(L, duration_cast<seconds>(dur).count());
    return 1;
}

static int lsteady_ms(lua_State* L) {
    steady_clock::duration dur = steady_clock::now().time_since_epoch();
    lua_pushinteger(L, duration_cast<milliseconds>(dur).count());
    return 1;
}

static int lsleep(lua_State* L) {
    size_t ms = luaL_checkinteger(L, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return 0;
}

#ifdef _MSC_VER
#define LTIMER_API extern "C" _declspec(dllexport)
#else
#define LTIMER_API extern "C"
#endif

LTIMER_API int luaopen_ltimer(lua_State* L) {
    luaL_checkversion(L);
    lua_timer* timer = new lua_timer();
    lua_pushlightuserdata(L, timer);
    luaL_newmetatable(L, "__LUA_TIMER__");
    luaL_Reg ltimer[] = {
        { "now", lnow },
        { "time", ltime },
        { "now_ms", lnow_ms },
        { "steady", lsteady },
        { "steady_ms", lsteady_ms },
        { "sleep", lsleep },
        { "update", lupdate },
        { "insert" , linsert },
        { "destory", ldestory },
        { NULL, NULL },
    };
    luaL_newlib(L, ltimer);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    return 1;
}
