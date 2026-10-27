// The script rows of the binding that reach inside the client's Lua state: a mark of where its
// calls stand and the unwinding back to it, as luaD_pcall keeps and restores them (ldo.c). Apart
// from Binding.cpp, since Lua's internal headers define macros (cast, G, ...) its other rows would
// see.

#include "WarcraftXL/Binding.h"

#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/ldo.h"
#include "Engine/Source/lua-5.1/src/lfunc.h"
#include "Engine/Source/lua-5.1/src/lobject.h"
#include "Engine/Source/lua-5.1/src/lstate.h"

namespace {
    // The words of a wxl_script_mark on this side.
    enum MarkWord : unsigned {
        kMarkTop,            // L->top, as an offset from L->stack
        kMarkCall,           // L->ci, as an offset from L->base_ci
        kMarkCCalls,         // L->nCcalls
        kMarkAllowHook,      // L->allowhook
        kMarkErrFunc,        // L->errfunc
        kMarkErrorJmp,       // L->errorJmp
        kMarkTainted,        // lua_tainted
        kMarkTaintExpected,  // lua_taintexpected
        kMarkTaintedClosure, // lua_taintedclosure
    };
}

namespace WarcraftXL::Binding {
    void WXL_CALL script_mark(void* state, wxl_script_mark* mark) {
        if (!mark) return;
        *mark = wxl_script_mark{};
        lua_State* L = static_cast<lua_State*>(state);
        if (!L) return;
        mark->words[kMarkTop]            = static_cast<uintptr_t>(savestack(L, L->top));
        mark->words[kMarkCall]           = static_cast<uintptr_t>(saveci(L, L->ci));
        mark->words[kMarkCCalls]         = L->nCcalls;
        mark->words[kMarkAllowHook]      = L->allowhook;
        mark->words[kMarkErrFunc]        = static_cast<uintptr_t>(L->errfunc);
        mark->words[kMarkErrorJmp]       = reinterpret_cast<uintptr_t>(L->errorJmp);
        mark->words[kMarkTainted]        = reinterpret_cast<uintptr_t>(lua_tainted);
        mark->words[kMarkTaintExpected]  = static_cast<uintptr_t>(lua_taintexpected);
        mark->words[kMarkTaintedClosure] = static_cast<uintptr_t>(lua_taintedclosure);
    }

    wxl_bool WXL_CALL script_unwind(void* state, const wxl_script_mark* mark) {
        lua_State* L = static_cast<lua_State*>(state);
        if (!L || !mark) return WXL_FALSE;
        CallInfo* const call = restoreci(L, static_cast<ptrdiff_t>(mark->words[kMarkCall]));
        // A protected call still open shows in its call, its C-call depth or its recovery point
        // (the parser's runs at the caller's call level).
        if (L->ci == call && L->nCcalls == mark->words[kMarkCCalls] &&
            reinterpret_cast<uintptr_t>(L->errorJmp) == mark->words[kMarkErrorJmp])
            return WXL_FALSE;

        // A profiled interface counts how deep each Lua function runs; the calls dropped leave it.
        if (G(L)->profile) {
            CallInfo* ci = L->ci;
            while (ci > call + 1) {
                ci--;
                if (isLua(ci)) clvalue(ci->func)->l.profile->depth--;
            }
        }
        StkId top = restorestack(L, static_cast<ptrdiff_t>(mark->words[kMarkTop]));
        luaF_close(L, top);
        L->top       = top;
        L->nCcalls   = static_cast<unsigned short>(mark->words[kMarkCCalls]);
        L->ci        = call;
        L->base      = call->base;
        L->savedpc   = call->savedpc;
        L->allowhook = static_cast<lu_byte>(mark->words[kMarkAllowHook]);
        L->errfunc   = static_cast<ptrdiff_t>(mark->words[kMarkErrFunc]);
        L->errorJmp  = reinterpret_cast<lua_longjmp*>(mark->words[kMarkErrorJmp]);
        // restore_stack_limit: a call array grown past its limit by an overflow goes back to it.
        if (L->size_ci > LUAI_MAXCALLS && static_cast<int>(call - L->base_ci) + 1 < LUAI_MAXCALLS)
            luaD_reallocCI(L, LUAI_MAXCALLS);

        lua_tainted        = reinterpret_cast<const char*>(mark->words[kMarkTainted]);
        lua_taintexpected  = static_cast<int>(mark->words[kMarkTaintExpected]);
        lua_taintedclosure = static_cast<int>(mark->words[kMarkTaintedClosure]);
        return WXL_TRUE;
    }
}
