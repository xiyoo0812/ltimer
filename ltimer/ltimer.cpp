#define LUA_LIB

#include <list>
#include "ltimer.h"

#include "sol/sol.hpp"

extern "C" {
    #include <lua.h>
    #include <lauxlib.h>
}

#define TIME_NEAR_SHIFT     8
#define TIME_LEVEL_SHIFT    6
#define TIME_NEAR           (1 << TIME_NEAR_SHIFT)
#define TIME_LEVEL          (1 << TIME_LEVEL_SHIFT)
#define TIME_NEAR_MASK      (TIME_NEAR-1)
#define TIME_LEVEL_MASK     (TIME_LEVEL-1)

namespace ltimer {

    struct timer_node {
        size_t expire;
        uint64_t timer_id;
    };

    using timer_list = std::list<timer_node>;
    using integer_vector = std::vector<uint64_t>;

    class lua_timer {
    public:
        integer_vector update(size_t elapse);
        void insert(uint64_t timer_id, size_t escape);

    protected:
        void shift();
        void add_node(timer_node& node);
        void execute(integer_vector& timers);
        void move_list(uint32_t level, uint32_t idx);

    protected:
        size_t time = 0;
        timer_list near[TIME_NEAR];
        timer_list t[4][TIME_LEVEL];
    };

    void lua_timer::add_node(timer_node& node) {
        size_t expire = node.expire;
        if ((expire | TIME_NEAR_MASK) == (time | TIME_NEAR_MASK)) {
            near[expire & TIME_NEAR_MASK].push_back(node);
            return;
        }
        uint32_t i;
        uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT;
        for (i = 0; i < 3; i++) {
            if ((expire | (mask - 1)) == (time | (mask - 1))) {
                break;
            }
            mask <<= TIME_LEVEL_SHIFT;
        }
        t[i][((expire >> (TIME_NEAR_SHIFT + i * TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)].push_back(node);
    }

    void lua_timer::insert(uint64_t timer_id, size_t escape) {
        timer_node node{ time + escape, timer_id };
        add_node(node);
    }

    void lua_timer::move_list(uint32_t level, uint32_t idx) {
        timer_list& list = t[level][idx];
        for (auto node : t[level][idx]) {
            add_node(node);
        }
        list.clear();
    }

    void lua_timer::shift() {
        size_t ct = ++time;
        if (ct == 0) {
            move_list(3, 0);
            return;
        }
        uint32_t i = 0;
        int mask = TIME_NEAR;
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

    void lua_timer::execute(integer_vector& timers) {
        uint32_t idx = time & TIME_NEAR_MASK;
        for (auto node : near[idx]) {
            timers.push_back(node.timer_id);
        }
        near[idx].clear();
    }

    integer_vector lua_timer::update(size_t elapse) {
        integer_vector timers;
        execute(timers);
        for (size_t i = 0; i < elapse; i++) {
            shift();
            execute(timers);
        }
        return timers;
    }

    sol::table open_ltimer(sol::this_state L) {
        sol::state_view lua(L);
        auto luatimer = lua.create_table();
        luatimer.set_function("now", []() { return now(); });
        luatimer.set_function("now_ms", []() { return now_ms(); });
        luatimer.set_function("steady", []() { return steady(); });
        luatimer.set_function("steady_ms", []() { return steady_ms(); });
        luatimer.set_function("sleep", [](uint64_t ms) { return sleep(ms); });
        luatimer.set_function("time", []() { return sol::as_returns(integer_vector({ now_ms(), now() })); });
        luatimer.new_usertype<lua_timer>("lua_timer"
            , "insert", &lua_timer::insert
            , "update", &lua_timer::update
            );
        return luatimer;
    }
}

extern "C" LUALIB_API int luaopen_ltimer(lua_State* L) {
    return sol::stack::call_lua(L, 1, ltimer::open_ltimer);
}
