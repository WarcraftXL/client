#include "WarcraftXL/Binding.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Base/SIMD.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Mopaq/MPQGlobals.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Mopaq/SFileArchives.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/SoundEngine/fmodex/src/fmod.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Model2/CM2SceneRender.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/Status.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ChrRacesRec.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "Engine/Source/Tempest/CRect.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/Container.h"
#include "WoW/Source/Object/Corpse.h"
#include "WoW/Source/Object/DynamicObject.h"
#include "WoW/Source/Object/GameObject.h"
#include "WoW/Source/Object/Item.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/ObjectClient/AuraData.h"
#include "WoW/Source/Object/Object.h"
#include "WoW/Source/Object/ObjectClient/Container_C.h"
#include "WoW/Source/Object/ObjectClient/Corpse_C.h"
#include "WoW/Source/Object/ObjectClient/DynamicObject_C.h"
#include "WoW/Source/Object/ObjectClient/GameObject_C.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Types.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/ObjectClient/VisibleItem_C.h"
#include "WoW/Source/Object/Player.h"
#include "WoW/Source/Object/Unit.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/WowServices/Login.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Object/MovementShared.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/ChatFrame.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/UIUtil/InputControl.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "WoW/Source/Ui/DuelInfo.h"
#include "WoW/Source/Ui/LootFrame.h"
#include "WoW/Source/Ui/ReputationInfo.h"
#include "WoW/Source/Ui/TradeFrame.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/PartyFrame.h"
#include "WoW/Source/Ui/RaidInfo.h"
#include "WoW/Source/WorldClient/MapArea.h"
#include "WoW/Source/WorldClient/MapDoodadDef.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapObjGroup.h"
#include "WoW/Source/WorldClient/World.h"
#include "wxl/core/gpu.hpp"
#include "wxl/core/filestack.hpp"
#include "wxl/core/storm.hpp"
#include "wxl/events.h"
#include "wxl/objects/fields.hpp"
#include "wxl/services/script.h"

// The SDK's update-field table describes these blocks: each kind's fields end where the client's
// descriptor block for it does, and a player other than the active one keeps CGPlayerData only.
// core/tools/fields/generate_update_fields.py writes the table from this fork and checks it
// whole; these hold its block sizes to the structs this build compiles.
#define WXL_UPDATE_FIELD(kind, NAME, Name, index, size, value, flags, watch)
#define WXL_UPDATE_FIELD_KIND(kind, type, parent, first, end, others_end) \
    constexpr uint32_t kFieldsOf##kind[3] = { first, end, others_end };
#include "wxl/update_fields.def"
#undef WXL_UPDATE_FIELD_KIND
#undef WXL_UPDATE_FIELD
static_assert(sizeof(CGObjectData) == (kFieldsOfObject[1] - kFieldsOfObject[0]) * 4, "CGObjectData");
static_assert(sizeof(CGItemData) == (kFieldsOfItem[1] - kFieldsOfItem[0]) * 4, "CGItemData");
static_assert(sizeof(CGContainerData) == (kFieldsOfContainer[1] - kFieldsOfContainer[0]) * 4, "CGContainerData");
static_assert(sizeof(CGUnitData) == (kFieldsOfUnit[1] - kFieldsOfUnit[0]) * 4, "CGUnitData");
static_assert(sizeof(CGPlayerLocalData) == (kFieldsOfPlayer[1] - kFieldsOfPlayer[0]) * 4, "CGPlayerLocalData");
static_assert(sizeof(CGPlayerData) == (kFieldsOfPlayer[2] - kFieldsOfPlayer[0]) * 4, "CGPlayerData");
static_assert(sizeof(CGGameObjectData) == (kFieldsOfGameObject[1] - kFieldsOfGameObject[0]) * 4, "CGGameObjectData");
static_assert(sizeof(CGDynamicObjectData) == (kFieldsOfDynamicObject[1] - kFieldsOfDynamicObject[0]) * 4,
              "CGDynamicObjectData");
static_assert(sizeof(CGCorpseData) == (kFieldsOfCorpse[1] - kFieldsOfCorpse[0]) * 4, "CGCorpseData");
static_assert(offsetof(CGUnitData, m_unitFieldHealth) == (wxl::field::Unit::Health.index - kFieldsOfUnit[0]) * 4 &&
                  offsetof(CGPlayerData, m_playerChosenTitle) ==
                      (wxl::field::Player::ChosenTitle.index - kFieldsOfPlayer[0]) * 4,
              "UNIT_FIELD_HEALTH and PLAYER_CHOSEN_TITLE where update_fields.def puts them");

// The contract numbers object types exactly as the client does, so a type id crosses as is.
static_assert(ID_OBJECT == WXL_OBJECT_UNKNOWN && ID_ITEM == WXL_OBJECT_ITEM && ID_CONTAINER == WXL_OBJECT_CONTAINER
                  && ID_UNIT == WXL_OBJECT_UNIT && ID_PLAYER == WXL_OBJECT_PLAYER && ID_GAMEOBJECT == WXL_OBJECT_GAMEOBJECT
                  && ID_DYNAMICOBJECT == WXL_OBJECT_DYNAMIC && ID_CORPSE == WXL_OBJECT_CORPSE,
              "wxl_object_type numbers the client's OBJECT_TYPE_ID");

namespace {
    // The runtime hands back the CGObject_C* a seam gave it, and asks a kind's rows only for an
    // object of that kind, so each cast below is the one the client itself would make.
    const CGObject_C* Object(const void* object) {
        return static_cast<const CGObject_C*>(object);
    }

    const CGCorpse_C* Corpse(const void* object) {
        return static_cast<const CGCorpse_C*>(Object(object));
    }

    template <class T, size_t N>
    constexpr uint32_t Extent(const T (&)[N]) {
        return static_cast<uint32_t>(N);
    }

    wxl_bool Give(wxl_vec3* out, const C3Vector& v) {
        out->x = v.x;
        out->y = v.y;
        out->z = v.z;
        return WXL_TRUE;
    }

    lua_State* State(void* state) {
        return static_cast<lua_State*>(state);
    }

    // The runtime's mirror handler, called from the client's own MIRRORHANDLERFUNC: the guid
    // crosses as its 64 bits. The runtime sets one handler for every field it watches.
    wxl_core_mirror_fn s_mirror = nullptr;

    int32_t ForwardMirror(WOWGUID guid, uint32_t offset, uint32_t size, const void* oldValue, void* param) {
        return s_mirror ? s_mirror(guid.m_guid, offset, size, oldValue, param) : 0;
    }

    bool TakeMirror(wxl_core_mirror_fn handler) {
        if (!handler || (s_mirror && s_mirror != handler)) {
            return false;
        }
        s_mirror = handler;
        return true;
    }

    // A member as a binding row answers it: a guid as its 64 bits, a byte array as the integer it
    // packs, anything else converted to the row's type.
    template <class R, class V>
    R Fact(const V& value) {
        if constexpr (std::is_same_v<V, WOWGUID>) {
            return value.m_guid;
        } else if constexpr (std::is_array_v<V>) {
            static_assert(sizeof(V) == sizeof(R), "a byte array answers an integer of its size");
            R packed;
            std::memcpy(&packed, &value, sizeof packed);
            return packed;
        } else {
            return static_cast<R>(value);
        }
    }
}

// Defined beside the statics they reach, which their files keep to themselves.
void WarcraftXLAnswerDuel(WOWGUID arbiter, WOWGUID challenger, bool accept);
int32_t WarcraftXLNextLootRollId();
uint32_t WarcraftXLEvictModels(const char* leaf, void** out, uint32_t capacity);
uint32_t WarcraftXLReloadTextures(const char* name, uint32_t* busy);

namespace WarcraftXL::Binding {
    // --- the field rows, generated from the table --------------------------------------------

#define WXL_BINDING(name, ret, params, args, absent)

#define WXL_BINDING_FIELD(name, ret, Type, member, absent)                                              \
    ret WXL_CALL name(const void* object) {                                                             \
        return object ? Fact<ret>(static_cast<const Type*>(object)->member) : static_cast<ret>(absent); \
    }

#define WXL_BINDING_FIELD_VIA(name, ret, Type, via, ViaType, member, absent)             \
    ret WXL_CALL name(const void* object) {                                              \
        const ViaType* block = object ? static_cast<const Type*>(object)->via : nullptr; \
        return block ? Fact<ret>(block->member) : static_cast<ret>(absent);              \
    }

#define WXL_BINDING_FIELD_VIA_INDEX(name, ret, Type, via, ViaType, member)               \
    wxl_bool WXL_CALL name(const void* object, uint32_t index, ret* out) {               \
        const ViaType* block = object ? static_cast<const Type*>(object)->via : nullptr; \
        if (!block || !out || index >= Extent(block->member)) {                          \
            return WXL_FALSE;                                                            \
        }                                                                                \
        *out = Fact<ret>(block->member[index]);                                          \
        return WXL_TRUE;                                                                 \
    }

#define WXL_BINDING_FIELD_VIA_AT(name, ret, Type, via, ViaType, member, Elem, elem_member) \
    wxl_bool WXL_CALL name(const void* object, uint32_t index, ret* out) {                 \
        const ViaType* block = object ? static_cast<const Type*>(object)->via : nullptr;   \
        if (!block || !out || index >= Extent(block->member)) {                            \
            return WXL_FALSE;                                                              \
        }                                                                                  \
        *out = Fact<ret>(block->member[index].elem_member);                                \
        return WXL_TRUE;                                                                   \
    }

#include "wxl/core/binding.def"

#undef WXL_BINDING
#undef WXL_BINDING_FIELD
#undef WXL_BINDING_FIELD_VIA
#undef WXL_BINDING_FIELD_VIA_INDEX
#undef WXL_BINDING_FIELD_VIA_AT

    // --- the call rows, one body per WXL_BINDING row of the table ----------------------------

    // --- world.def ------------------------------------------------------------------------------

    int32_t WXL_CALL map_id(void) {
        return World::QueryMapId();
    }

    wxl_bool WXL_CALL world_position(wxl_vec3* out) {
        if (!out || World::QueryMapId() < 0) return WXL_FALSE;
        return Give(out, World::s_currentWorldPos);
    }

    // --- objectmgr.def --------------------------------------------------------------------------

    const void* WXL_CALL local_player_object(void) {
        return ClntObjMgrGetActivePlayerObj();
    }

    const void* WXL_CALL object_by_guid(uint64_t guid) {
        return ClntObjMgrObjectPtr(WOWGUID{ guid }, TYPE_OBJECT, __FILE__, __LINE__);
    }

    wxl_bool WXL_CALL object_watch_type(uint32_t type, uint32_t offset, uint32_t size, wxl_core_mirror_fn handler,
                                        void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !TakeMirror(handler)) {
            return WXL_FALSE;
        }
        ClntObjMgrSetTypeMirrorHandler(static_cast<OBJECT_TYPE_ID>(type), offset, size, &ForwardMirror, param,
                                       static_cast<HANDLER_PRIORITY>(0), 0);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL object_watch_one(uint64_t guid, uint32_t type, uint32_t offset, uint32_t size,
                                       wxl_core_mirror_fn handler, void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !object_by_guid(guid) || !TakeMirror(handler)) {
            return WXL_FALSE;
        }
        ClntObjMgrSetObjMirrorHandler(WOWGUID{ guid }, static_cast<OBJECT_TYPE_ID>(type), offset, size, &ForwardMirror,
                                      param, static_cast<HANDLER_PRIORITY>(0), 0);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL object_unwatch_type(uint32_t type, uint32_t offset, wxl_core_mirror_fn handler, void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !handler || handler != s_mirror) {
            return WXL_FALSE;
        }
        ClntObjMgrUnsetTypeMirrorHandler(static_cast<OBJECT_TYPE_ID>(type), offset, &ForwardMirror, param);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL object_unwatch_one(uint64_t guid, uint32_t type, uint32_t offset, wxl_core_mirror_fn handler,
                                         void* param) {
        if (type >= NUM_CLIENT_OBJECT_TYPES || !handler || handler != s_mirror || !object_by_guid(guid)) {
            return WXL_FALSE;
        }
        ClntObjMgrUnsetObjMirrorHandler(WOWGUID{ guid }, static_cast<OBJECT_TYPE_ID>(type), offset, &ForwardMirror,
                                        param);
        return WXL_TRUE;
    }

    // --- object.def -----------------------------------------------------------------------------

    wxl_bool WXL_CALL object_position(const void* object, wxl_vec3* out) {
        const CGObject_C* o = Object(object);
        switch (o->m_typeId) {
            case ID_UNIT:
            case ID_PLAYER:
            case ID_GAMEOBJECT:
            case ID_DYNAMICOBJECT:
                return Give(out, o->GetPosition());
            case ID_CORPSE:
                // The fork declares no CGCorpse_C::GetPosition; the corpse's CPassenger holds it.
                return Give(out, Corpse(object)->m_passenger.m_position);
            default:
                return WXL_FALSE;
        }
    }

    wxl_bool WXL_CALL object_facing(const void* object, float* out) {
        const CGObject_C* o = Object(object);
        switch (o->m_typeId) {
            case ID_UNIT:
            case ID_PLAYER:
            case ID_GAMEOBJECT:
            case ID_DYNAMICOBJECT:
            case ID_CORPSE:
                *out = o->GetFacing();
                return WXL_TRUE;
            default:
                return WXL_FALSE;
        }
    }

    const char* WXL_CALL object_name(const void* object) {
        const CGObject_C* o = Object(object);
        switch (o->m_typeId) {
            case ID_UNIT:
            case ID_PLAYER:
            case ID_GAMEOBJECT:
            case ID_ITEM:
            case ID_CONTAINER:
                return o->GetObjectName();
            default:
                return nullptr;
        }
    }

    // --- gameui.def -----------------------------------------------------------------------------

    uint64_t WXL_CALL ui_target_guid(void) {
        return CGGameUI::m_lockedTarget.m_guid;
    }

    int32_t WXL_CALL ui_initialized(void) {
        return CGGameUI::m_initialized;
    }

    uint8_t WXL_CALL ui_logging_in(void) {
        return CGGameUI::m_loggingIn;
    }

    uint8_t WXL_CALL ui_reloading(void) {
        return CGGameUI::m_currentlyReloadingUI;
    }

    // --- glue.def -------------------------------------------------------------------------------

    const char* WXL_CALL glue_current_screen(void) {
        return CGlueMgr::m_currentScreen;
    }

    int32_t WXL_CALL login_server_type(const void* login) {
        return login ? const_cast<Login*>(static_cast<const Login*>(login))->GetLoginServerType() : -1;
    }

    // --- chat.def -------------------------------------------------------------------------------

    const void* WXL_CALL chat_pending_last(void) {
        return CGChat::s_pendingChat.Tail();
    }

    // --- script.def -----------------------------------------------------------------------------

    void* WXL_CALL script_state(void) {
        return FrameScript_GetContext();
    }

    wxl_bool WXL_CALL script_global_is_set(void* state, const char* name) {
        lua_State* L = State(state);
        lua_getfield(L, LUA_GLOBALSINDEX, name);
        const int type = lua_type(L, -1);
        lua_pop(L, 1);
        return type > LUA_TNIL ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL script_set_global_closure(void* state, const char* name, wxl_script_native_fn fn, uint32_t slot) {
        lua_State* L = State(state);
        lua_pushnumber(L, static_cast<lua_Number>(slot));
        lua_pushcclosure(L, reinterpret_cast<lua_CFunction>(fn), 1);
        lua_setfield(L, LUA_GLOBALSINDEX, name);
    }

    void WXL_CALL script_clear_global(void* state, const char* name) {
        lua_State* L = State(state);
        lua_pushnil(L);
        lua_setfield(L, LUA_GLOBALSINDEX, name);
    }

    uint32_t WXL_CALL script_closure_slot(void* state) {
        return static_cast<uint32_t>(lua_tonumber(State(state), lua_upvalueindex(1)));
    }

    int32_t WXL_CALL script_top(void* state) {
        return lua_gettop(State(state));
    }

    uint32_t WXL_CALL script_value_type(void* state, int32_t index) {
        switch (lua_type(State(state), index)) {
            case LUA_TNUMBER:
                return WXL_VALUE_NUMBER;
            case LUA_TSTRING:
                return WXL_VALUE_STRING;
            case LUA_TBOOLEAN:
                return WXL_VALUE_BOOL;
            default:
                return WXL_VALUE_NIL;
        }
    }

    double WXL_CALL script_to_number(void* state, int32_t index) {
        return lua_tonumber(State(state), index);
    }

    const char* WXL_CALL script_to_string(void* state, int32_t index, uint32_t* length) {
        size_t size = 0;
        const char* text = lua_tolstring(State(state), index, &size);
        if (length) {
            *length = static_cast<uint32_t>(size);
        }
        return text;
    }

    wxl_bool WXL_CALL script_to_bool(void* state, int32_t index) {
        return lua_toboolean(State(state), index) ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL script_push_number(void* state, double value) {
        lua_pushnumber(State(state), value);
    }

    void WXL_CALL script_push_string(void* state, const char* text, uint32_t length) {
        lua_pushlstring(State(state), text ? text : "", text ? length : 0u);
    }

    void WXL_CALL script_push_bool(void* state, wxl_bool value) {
        lua_pushboolean(State(state), value ? 1 : 0);
    }

    void WXL_CALL script_push_nil(void* state) {
        lua_pushnil(State(state));
    }

    void WXL_CALL script_pop(void* state, int32_t count) {
        if (count > 0) {
            lua_pop(State(state), count);
        }
    }

    void WXL_CALL script_error(void* state, const char* message) {
        lua_State* L = State(state);
        lua_pushstring(L, message ? message : "WarcraftXL: error");
        lua_error(L);
    }

    void WXL_CALL script_run(const char* source, const char* name) {
        FrameScript_Execute(source, name, nullptr);
    }

    void WXL_CALL script_create_events(const char** names, uint32_t count) {
        FrameScript_CreateEvents(names, count);
    }

    void WXL_CALL script_signal_event(uint32_t index, void* state, int32_t arg_count) {
        FrameScript_SignalEvent(index, State(state), arg_count);
    }

    // The Storage rows are the client's own file layer, called where it lives. A `file` is the
    // SFileImpl the client handed back; nothing above this knows what it is.
    SFileImpl* File(void* file) {
        return static_cast<SFileImpl*>(file);
    }

    wxl_bool WXL_CALL file_exists(const char* path) {
        return path && SFile::FileExists(path) ? WXL_TRUE : WXL_FALSE;
    }

    void* WXL_CALL file_open(const char* path) {
        SFileImpl* file = nullptr;
        return path && SFileOpenFile(path, &file) ? file : nullptr;
    }

    uint64_t WXL_CALL file_size(void* file) {
        if (!file) {
            return 0;
        }
        uint32_t high = 0;
        uint32_t low = SFileGetFileSize(File(file), &high);
        return (static_cast<uint64_t>(high) << 32) | low;
    }

    wxl_bool WXL_CALL file_seek(void* file, uint64_t offset) {
        if (!file) {
            return WXL_FALSE;
        }
        int32_t high = static_cast<int32_t>(offset >> 32);
        // Method 0 is FILE_BEGIN: the offset is absolute, which is what read_at means. The answer
        // is the cursor afterwards, 0 for a seek to the start.
        const int32_t at = SFile::SetFilePointer(File(file), static_cast<int32_t>(offset & 0xFFFFFFFFu), &high, 0);
        return wxl::core::storm::SeekLanded(static_cast<uint32_t>(at), offset) ? WXL_TRUE : WXL_FALSE;
    }

    uint32_t WXL_CALL file_read(void* file, void* buffer, uint32_t count) {
        if (!file || !buffer || !count) {
            return 0;
        }
        uint32_t read = 0;
        SFileReadFile(File(file), buffer, count, &read, nullptr, 0);
        return read;
    }

    void WXL_CALL file_close(void* file) {
        if (file) {
            SFileCloseFile(File(file));
        }
    }

    wxl_bool WXL_CALL archive_mount(const char* path, int32_t priority) {
        SArchive* archive = nullptr;
        return path && SFile::OpenArchive(path, priority, 0, &archive) ? WXL_TRUE : WXL_FALSE;
    }

    const char* WXL_CALL storage_base_path() {
        return SFile::m_basePath;
    }

    // The file stack, as the runtime's file layer reads it: its declarations are the client's,
    // member for member, which these hold to the client's own types.
    namespace fsk = wxl::core::filestack;
    static_assert(sizeof(fsk::FileInfo) == sizeof(Blizzard::File::FileInfo) &&
                  offsetof(fsk::FileInfo, size) == offsetof(Blizzard::File::FileInfo, m_size) &&
                  offsetof(fsk::FileInfo, existence) == offsetof(Blizzard::File::FileInfo, m_existence) &&
                  offsetof(fsk::FileInfo, regular_file) == offsetof(Blizzard::File::FileInfo, m_regularFile));
    static_assert(sizeof(fsk::Filesystem) == sizeof(Blizzard::File::Filesystem) &&
                  offsetof(fsk::Filesystem, verbs) == offsetof(Blizzard::File::Filesystem, m_verbs) &&
                  offsetof(fsk::Functions, open) == offsetof(Blizzard::File::Functions, m_open) &&
                  offsetof(fsk::Functions, dirwalk) == offsetof(Blizzard::File::Functions, m_dirwalk) &&
                  offsetof(fsk::Functions, shutdown) == offsetof(Blizzard::File::Functions, m_shutdown));
    static_assert(sizeof(fsk::StreamRecord) == sizeof(Blizzard::File::StreamRecord) &&
                  offsetof(fsk::StreamRecord, info) == offsetof(Blizzard::File::StreamRecord, m_info) &&
                  offsetof(fsk::StreamRecord, pathname) == offsetof(Blizzard::File::StreamRecord, m_pathname));
    static_assert(sizeof(fsk::ProcessDirParms) == sizeof(Blizzard::File::ProcessDirParms) &&
                  offsetof(fsk::ProcessDirParms, is_dir) == offsetof(Blizzard::File::ProcessDirParms, m_isDir));
    static_assert(sizeof(fsk::FileParms) == sizeof(System_File::Stacked::FileParms) &&
                  offsetof(fsk::FileParms, stream) == offsetof(System_File::Stacked::FileParms, m_stream) &&
                  offsetof(fsk::FileParms, info2) == offsetof(System_File::Stacked::FileParms, m_info2) &&
                  offsetof(fsk::FileParms, mode) == offsetof(System_File::Stacked::FileParms, m_mode) &&
                  offsetof(fsk::FileParms, buffer) == offsetof(System_File::Stacked::FileParms, m_buffer) &&
                  offsetof(fsk::FileParms, amount) == offsetof(System_File::Stacked::FileParms, m_amount) &&
                  offsetof(fsk::FileParms, pos) == offsetof(System_File::Stacked::FileParms, m_pos) &&
                  offsetof(fsk::FileParms, pos_mode) == offsetof(System_File::Stacked::FileParms, m_posMode) &&
                  offsetof(fsk::FileParms, cookie) == offsetof(System_File::Stacked::FileParms, m_cookie) &&
                  offsetof(fsk::FileParms, dircallback) == offsetof(System_File::Stacked::FileParms, m_dircallback));

    void** WXL_CALL filestack_top() {
        return reinterpret_cast<void**>(&System_File::Stacked::s_manager);
    }

    const void* WXL_CALL filestack_initial() {
        return &System_File::Stacked::s_initstack;
    }

    uint32_t WXL_CALL archive_list(wxl_core_name_fn fn, void* user) {
        if (!fn) {
            return 0;
        }
        // The records first: the listing opens files, which may reach the set.
        std::vector<System_SFile::ARCHIVEREC*> records;
        for (System_SFile::ARCHIVEREC* record : *System_MPQGlobals::GetArchiveList()) {
            records.push_back(record);
        }
        uint32_t listed = 0;
        for (System_SFile::ARCHIVEREC* record : records) {
            SArchive archive = {};
            archive.m_type = SARCHIVE_MPQ;
            archive.m_archive = record;
            if (record && SFile::List(&archive, fn, user)) {
                ++listed;
            }
        }
        return listed;
    }

    // --- Gx ---------------------------------------------------------------------------------
    // GxHasContext and GxIsWindowVisible test the device pointer themselves.
    // GxCapsWindowSizeInScreenCoords does not, so the size answers false without a device.

    wxl_bool WXL_CALL gx_device_exists() {
        return g_theGxDevicePtr ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL gx_has_context() {
        return GxHasContext() ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL gx_window_visible() {
        return GxIsWindowVisible() ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL gx_window_size(uint32_t* width, uint32_t* height) {
        if (!width || !height || !g_theGxDevicePtr) {
            return WXL_FALSE;
        }
        CRect rect = {};
        GxCapsWindowSizeInScreenCoords(rect);
        const float w = rect.maxX - rect.minX;
        const float h = rect.maxY - rect.minY;
        *width = w > 0.0f ? static_cast<uint32_t>(w + 0.5f) : 0u;
        *height = h > 0.0f ? static_cast<uint32_t>(h + 0.5f) : 0u;
        return WXL_TRUE;
    }

    // --- Gx: wxl.gx, ModelBatchDrawn -----------------------------------------------------------
    // Both D3D9 devices keep the same copy of what they last set (m_deviceStates, the current
    // declaration and streams); spoiling it makes the next IStateSync send everything again.

    void* WXL_CALL gx_d3d9_device(void) {
        if (!g_theGxDevicePtr) {
            return nullptr;
        }
        switch (g_theGxDevicePtr->DeviceApi()) {
            case GxApi_D3d9:
                return static_cast<CGxDeviceD3d*>(g_theGxDevicePtr)->m_d3dDevice;
            case GxApi_D3d9Ex:
                return static_cast<CGxDeviceD3d9Ex*>(g_theGxDevicePtr)->m_d3dDevice;
            default:
                return nullptr;
        }
    }

    template <class Device>
    void InvalidateD3d(Device* device) {
        device->IRsForceUpdate();
        std::memset(device->m_deviceStates, 0xA5, sizeof device->m_deviceStates);
        device->m_d3dCurrentVertexDecl = nullptr;
        std::memset(device->m_d3dVertexStreamBuf, 0, sizeof device->m_d3dVertexStreamBuf);
        device->m_viewportDirty = 1;
        device->m_scissorRectDirty = 1;
    }

    void WXL_CALL gx_invalidate_state(void) {
        if (!g_theGxDevicePtr) {
            return;
        }
        switch (g_theGxDevicePtr->DeviceApi()) {
            case GxApi_D3d9:
                InvalidateD3d(static_cast<CGxDeviceD3d*>(g_theGxDevicePtr));
                break;
            case GxApi_D3d9Ex:
                InvalidateD3d(static_cast<CGxDeviceD3d9Ex*>(g_theGxDevicePtr));
                break;
            default:
                break;
        }
    }

    CM2Model* RootModel(CM2Model* model) {
        // Bounded: an attachment chain is a few links (weapon, rider, mount).
        for (int32_t i = 0; model && model->m_attachParent && i < 16; i++) {
            model = model->m_attachParent;
        }
        return model;
    }

    wxl_bool WXL_CALL gx_model_draw(const void* render, wxl_model_batch_args* out) {
        // A batch the device did not draw (no context, a lost device) leaves the serial as it was.
        static uint32_t s_reported = 0;
        const CM2SceneRender* r = static_cast<const CM2SceneRender*>(render);
        if (!out || !r->m_curModel || g_gxLastIndexedDraw.serial == s_reported) {
            return WXL_FALSE;
        }
        s_reported = g_gxLastIndexedDraw.serial;
        out->model = r->m_curModel;
        out->root_model = RootModel(r->m_curModel);
        out->primitive_type = g_gxLastIndexedDraw.primType;
        out->base_vertex = g_gxLastIndexedDraw.baseVertex;
        out->min_index = g_gxLastIndexedDraw.minIndex;
        out->vertex_count = g_gxLastIndexedDraw.vertexCount;
        out->start_index = g_gxLastIndexedDraw.startIndex;
        out->primitive_count = g_gxLastIndexedDraw.primCount;
        out->blend_mode = r->m_curMaterial ? r->m_curMaterial->m_blendMode : 0u;
        out->viewport_x = g_gxLastIndexedDraw.viewport[0];
        out->viewport_y = g_gxLastIndexedDraw.viewport[1];
        out->viewport_width = g_gxLastIndexedDraw.viewport[2];
        out->viewport_height = g_gxLastIndexedDraw.viewport[3];
        out->viewport_min_z = g_gxLastIndexedDraw.viewportZ[0];
        out->viewport_max_z = g_gxLastIndexedDraw.viewportZ[1];
        return WXL_TRUE;
    }

    // --- M2: wxl.m2, AppearanceChanged ----------------------------------------------------------

    wxl_bool WXL_CALL component_wearer(const void* component, uint32_t item_slot, wxl_appearance_args* out) {
        if (!component || !out) {
            return WXL_FALSE;
        }
        const CCharacterComponent* c = static_cast<const CCharacterComponent*>(component);
        out->wearer = const_cast<void*>(component);
        out->model = c->m_data.m_model;
        out->race_id = c->m_data.m_preferences.m_raceID;
        out->sex_id = c->m_data.m_preferences.m_sexID;
        out->item_slot = item_slot;
        out->display_id = item_slot < NUM_ITEM_SLOTS ? c->m_itemDisplayID[item_slot] : 0u;
        out->npc = (c->m_data.m_flags & 0x1) ? WXL_TRUE : WXL_FALSE;
        return WXL_TRUE;
    }

    void* WXL_CALL m2_object_model(const void* object) {
        return RootModel(Object(object)->m_worldModel);
    }

    const char* Text(const char* text) {
        return text ? text : "";
    }

    wxl_bool WXL_CALL m2_item_display(uint32_t id, wxl_m2_item_display* out) {
        ItemDisplayInfoRec rec;
        if (!out || !id || !g_itemDisplayInfoDB.GetRecord(static_cast<int32_t>(id), &rec)) {
            return WXL_FALSE;
        }
        out->id = static_cast<uint32_t>(rec.m_id);
        for (int32_t i = 0; i < 2; i++) {
            out->model[i] = Text(rec.m_modelName[i]);
            out->model_texture[i] = Text(rec.m_modelTexture[i]);
            out->icon[i] = Text(rec.m_inventoryIcon[i]);
            out->helmet_geoset_vis[i] = static_cast<uint32_t>(rec.m_helmetGeosetVisID[i]);
        }
        for (int32_t i = 0; i < 3; i++) {
            out->geoset_group[i] = static_cast<uint32_t>(rec.m_geosetGroup[i]);
        }
        for (int32_t i = 0; i < 8; i++) {
            out->texture[i] = Text(rec.m_texture[i]);
        }
        out->flags = static_cast<uint32_t>(rec.m_flags);
        out->spell_visual_id = static_cast<uint32_t>(rec.m_spellVisualID);
        out->group_sound_index = static_cast<uint32_t>(rec.m_groupSoundIndex);
        out->item_visual = rec.m_itemVisual;
        out->particle_color_id = static_cast<uint32_t>(rec.m_particleColorID);
        return WXL_TRUE;
    }

    const char* WXL_CALL m2_race_prefix(uint32_t race) {
        const ChrRacesRec* rec = g_chrRacesDB.GetRecord(static_cast<int32_t>(race));
        return rec ? rec->m_clientPrefix : nullptr;
    }

    void* WXL_CALL m2_create_model(void* scene_model, const char* path) {
        CM2Model* parent = static_cast<CM2Model*>(scene_model);
        if (!parent || !parent->m_scene || !path || !*path) {
            return nullptr;
        }
        return parent->m_scene->CreateModel(path, 0);
    }

    void WXL_CALL m2_release_model(void* model) {
        if (model) {
            static_cast<CM2Model*>(model)->Release();
        }
    }

    wxl_bool WXL_CALL m2_attach_model(void* model, void* parent, uint32_t attachment, wxl_bool to_root) {
        CM2Model* child = static_cast<CM2Model*>(model);
        CM2Model* to = static_cast<CM2Model*>(parent);
        if (!child || !to || child == to) {
            return WXL_FALSE;
        }
        // AttachToParent drops a request for a point a loaded parent lacks without saying so.
        if (!to_root && (to->m_fFlags & 1)) {
            M2Data* data = to->m_shared->m_data;
            uint32_t index = attachment < data->m_attachmentIndicesById.m_count ? data->m_attachmentIndicesById[attachment] : 0xFFFF;
            if (index == 0xFFFF) {
                return WXL_FALSE;
            }
        }
        child->AttachToParent(to, attachment, nullptr, to_root ? 1 : 0);
        return child->m_attachParent == to ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL m2_detach_model(void* model) {
        CM2Model* child = static_cast<CM2Model*>(model);
        if (child && child->m_attachParent) {
            child->DetachFromParent();
        }
    }

    void* WXL_CALL m2_model_parent(const void* model) {
        return model ? static_cast<const CM2Model*>(model)->m_attachParent : nullptr;
    }

    void* WXL_CALL m2_model_source(const void* model) {
        return model ? static_cast<const CM2Model*>(model)->m_model30 : nullptr;
    }

    void WXL_CALL m2_set_geosets(void* model, uint32_t first, uint32_t last, wxl_bool visible) {
        if (model) {
            static_cast<CM2Model*>(model)->SetGeometryVisible(first, last, visible ? 1 : 0);
        }
    }

    wxl_bool WXL_CALL m2_replace_texture(void* model, uint32_t type, const char* path) {
        if (!model || !path || !*path) {
            return WXL_FALSE;
        }
        // The texture loads later and reports into its status then: the client's own.
        CTexture* texture = CreateTexture(path, &GetGlobalStatusObj());
        if (!texture) {
            return WXL_FALSE;
        }
        // The model takes a handle of its own, as CCharacterComponent::AddLink relies on.
        static_cast<CM2Model*>(model)->ReplaceTexture(type, texture);
        HandleClose(reinterpret_cast<HOBJECT>(texture));
        return WXL_TRUE;
    }

    uint32_t WXL_CALL m2_children(const void* model, void** out, uint32_t capacity) {
        uint32_t count = 0;
        for (CM2Model* child = model ? static_cast<const CM2Model*>(model)->m_attachList : nullptr; child; child = child->m_attachNext) {
            if (out && count < capacity) {
                out[count] = child;
            }
            count++;
        }
        return count;
    }

    wxl_bool WXL_CALL m2_attach_info(const void* model, uint32_t* attachment, wxl_bool* to_root) {
        const CM2Model* child = static_cast<const CM2Model*>(model);
        if (!child || !child->m_attachParent || !attachment || !to_root) {
            return WXL_FALSE;
        }
        *attachment = child->m_attachmentId;
        *to_root = (child->m_fFlags & 0x40000) ? WXL_TRUE : WXL_FALSE;
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL m2_attach_copy(void* source, void* parent, uint32_t attachment, wxl_bool to_root) {
        CM2Model* from = static_cast<CM2Model*>(source);
        CM2Model* to = static_cast<CM2Model*>(parent);
        if (!from || !to || !to->m_scene) {
            return WXL_FALSE;
        }
        CM2Model* copy = to->m_scene->DuplicateModel(from, 0);
        if (!copy) {
            return WXL_FALSE;
        }
        copy->AttachToParent(to, attachment, nullptr, to_root ? 1 : 0);
        const wxl_bool attached = copy->m_attachParent == to ? WXL_TRUE : WXL_FALSE;
        copy->Release(); // the parent holds it now, as CM2Model::Initialize leaves its copies
        return attached;
    }

    void* WXL_CALL m2_model_file(const void* model) {
        return model ? static_cast<const CM2Model*>(model)->m_shared : nullptr;
    }

    wxl_bool WXL_CALL m2_attachment_point(const void* model, uint32_t attachment, wxl_vec3* out) {
        const CM2Model* m = static_cast<const CM2Model*>(model);
        if (!m || !out || !(m->m_fFlags & 1)) {
            return WXL_FALSE;
        }
        const M2Data* data = m->m_shared->m_data;
        const uint32_t index = attachment < data->m_attachmentIndicesById.m_count ? data->m_attachmentIndicesById[attachment] : 0xFFFF;
        if (index >= data->m_attachments.m_count) {
            return WXL_FALSE;
        }
        const C3Vector& position = data->m_attachments[index].m_position;
        out->x = position.x;
        out->y = position.y;
        out->z = position.z;
        return WXL_TRUE;
    }

    static_assert(sizeof(M2CompBone) == 0x58 && offsetof(M2CompBone, m_boneId) == 0x00 && offsetof(M2CompBone, m_parentIndex) == 0x08
                      && offsetof(M2CompBone, m_boneNameCRC) == 0x0C && offsetof(M2CompBone, m_pivot) == 0x4C,
                  "the bones are laid out as the file is, which the runtime's posing reads");
    static_assert(sizeof(C44Matrix) == 16 * sizeof(float), "a bone matrix is 16 floats");

    wxl_bool WXL_CALL m2_skeleton(void* model, wxl_core_m2_skeleton* out) {
        CM2Model* m = static_cast<CM2Model*>(model);
        if (!m || !out || !(m->m_fFlags & 1) || !m->m_boneMatrices) {
            return WXL_FALSE;
        }
        const M2Data* data = m->m_shared->m_data;
        out->file = data;
        out->bones = data->m_bones.m_count ? reinterpret_cast<const uint8_t*>(&data->m_bones[0]) : nullptr;
        out->bone_count = data->m_bones.m_count;
        out->bone_stride = sizeof(M2CompBone);
        out->key_bones = data->m_boneIndicesById.m_count ? &data->m_boneIndicesById[0] : nullptr;
        out->key_bone_count = data->m_boneIndicesById.m_count;
        out->matrices = reinterpret_cast<float*>(m->m_boneMatrices);
        return out->bones ? WXL_TRUE : WXL_FALSE;
    }

    // --- Memory -----------------------------------------------------------------------------
    // Each row walks the client's own lists, as wxl.footprint asks on the main thread.

    uint32_t WXL_CALL gx_resources(wxl_core_gpu_fn fn, void* user) {
        CGxDevice* device = g_theGxDevicePtr;
        if (!fn || !device || (device->m_api != GxApi_D3d9 && device->m_api != GxApi_D3d9Ex)) {
            return 0;
        }
        uint32_t told = 0;
        // A texture waiting to be created again holds a placeholder, not its texture.
        for (CGxTex* texture = device->m_textures.Head(); texture; texture = device->m_textures.Next(texture)) {
            wxl_core_gpu_resource resource = {};
            if (!texture->m_needsCreation && texture->m_apiSpecificData &&
                wxl::core::gpu::DescribeTexture(static_cast<IDirect3DBaseTexture9*>(texture->m_apiSpecificData), &resource)) {
                fn(&resource, user);
                ++told;
            }
        }
        for (CGxPool* pool = device->m_poolList.Head(); pool; pool = device->m_poolList.Next(pool)) {
            wxl_core_gpu_resource resource = {};
            if (pool->m_apiSpecific &&
                wxl::core::gpu::DescribeBuffer(static_cast<IDirect3DResource9*>(pool->m_apiSpecific), &resource)) {
                fn(&resource, user);
                ++told;
            }
        }
        return told;
    }

    wxl_bool WXL_CALL sound_heap(uint64_t* current, uint64_t* peak) {
        int now = 0;
        int most = 0;
        if (!current || !peak || FMOD_Memory_GetStats(&now, &most) != FMOD_OK) {
            return WXL_FALSE;
        }
        *current = static_cast<uint32_t>(now);
        *peak = static_cast<uint32_t>(most);
        return WXL_TRUE;
    }

    uint64_t WXL_CALL script_heap() {
        lua_State* L = FrameScript_GetContext();
        if (!L) {
            return 0;
        }
        return static_cast<uint64_t>(lua_gc(L, LUA_GCCOUNT, 0)) * 1024u + static_cast<uint64_t>(lua_gc(L, LUA_GCCOUNTB, 0));
    }

    namespace {
        template <class T>
        uint64_t Reserved(const std::vector<T>& vector) {
            return static_cast<uint64_t>(vector.capacity()) * sizeof(T);
        }

        uint64_t PackedBytes(const System_SFile::BitPacked* bits) {
            return bits ? (static_cast<uint64_t>(bits->m_bitCount) + 7) / 8 + sizeof(*bits) : 0;
        }

        void Describe(const System_SFile::ARCHIVEREC* rec, wxl_core_archive* out) {
            out->path = wxl::core::storm::ArchiveName(rec->m_name, rec->m_directPath.c_str());
            out->priority = rec->m_priority;
            out->hash_entries = rec->m_header.m_hashTableSize;
            out->block_entries = rec->m_header.m_blockTableSize;
            uint64_t bytes = Reserved(rec->m_hashTable) + Reserved(rec->m_blockTable);
            if (const System_SFile::BlockEntryTable* blocks = rec->m_blockEntryTable) {
                bytes += sizeof(*blocks) + Reserved(blocks->m_flagTable) + Reserved(blocks->m_fileTimes) +
                         Reserved(blocks->m_crcs) + Reserved(blocks->m_md5s) + PackedBytes(blocks->m_hashes) +
                         PackedBytes(blocks->m_entries);
            }
            if (const System_SFile::HashEntryTable* hashes = rec->m_hashEntryTable) {
                bytes += sizeof(*hashes) + hashes->m_slotCount + PackedBytes(hashes->m_indices);
            }
            // A listfile name: a node of the map, and the name's own block when it outgrows the string.
            for (const auto& name : rec->m_listfile) {
                bytes += sizeof(name) + 4 * sizeof(void*) + (name.first.capacity() > 15 ? name.first.capacity() + 1 : 0);
            }
            out->bytes = bytes;
        }
    }

    uint32_t WXL_CALL archive_tables(wxl_core_archive_fn fn, void* user) {
        if (!fn) {
            return 0;
        }
        uint32_t told = 0;
        for (System_SFile::ARCHIVEREC* record : *System_MPQGlobals::GetArchiveList()) {
            if (!record) {
                continue;
            }
            wxl_core_archive archive = {};
            Describe(record, &archive);
            fn(&archive, user);
            ++told;
        }
        return told;
    }

    wxl_bool WXL_CALL archive_of(void* archive, wxl_core_archive* out) {
        const SArchive* handle = static_cast<const SArchive*>(archive);
        if (!handle || !out || handle->m_type != SARCHIVE_MPQ || !handle->m_archive) {
            return WXL_FALSE;
        }
        Describe(handle->m_archive, out);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL archive_record_of(void* record, wxl_core_archive* out) {
        if (!record || !out) {
            return WXL_FALSE;
        }
        Describe(static_cast<const System_SFile::ARCHIVEREC*>(record), out);
        return WXL_TRUE;
    }

    namespace {
        template <class Rec>
        void TellDb(const WowClientDB<Rec>& store, const char* file, wxl_core_db_fn fn, void* user) {
            const uint64_t records = store.m_numRecords > 0 ? static_cast<uint64_t>(store.m_numRecords) : 0;
            const uint64_t ids = records && store.m_maxID >= store.m_minID ? static_cast<uint64_t>(store.m_maxID - store.m_minID) + 1 : 0;
            wxl_core_db db = {};
            db.file = file;
            db.loaded = store.m_loaded ? WXL_TRUE : WXL_FALSE;
            db.records = static_cast<uint32_t>(records);
            db.record_size = sizeof(Rec);
            db.blocks[0] = store.m_records;
            db.estimates[0] = records * sizeof(Rec);
            db.blocks[1] = store.m_strings;
            db.blocks[2] = store.m_recordsById;
            db.estimates[2] = ids * sizeof(Rec*);
            fn(&db, user);
        }

        template <class Rec>
        void TellDb(const WowClientDB_RLE<Rec>& store, const char* file, wxl_core_db_fn fn, void* user) {
            const uint64_t records = store.m_numRecords > 0 ? static_cast<uint64_t>(store.m_numRecords) : 0;
            const uint64_t ids = records && store.m_maxID >= store.m_minID ? static_cast<uint64_t>(store.m_maxID - store.m_minID) + 1 : 0;
            wxl_core_db db = {};
            db.file = file;
            db.loaded = store.m_loaded ? WXL_TRUE : WXL_FALSE;
            db.records = static_cast<uint32_t>(records);
            db.record_size = sizeof(Rec);
            db.blocks[0] = store.m_data;
            db.estimates[0] = records * sizeof(Rec);
            db.blocks[1] = store.m_strings;
            // The row pointers, and the index by id after them in the same block.
            db.blocks[2] = store.m_records;
            db.estimates[2] = (records + ids) * sizeof(Rec*);
            fn(&db, user);
        }
    }

    uint32_t WXL_CALL client_dbs(wxl_core_db_fn fn, void* user) {
        if (!fn) {
            return 0;
        }
        uint32_t told = 0;
#define WXL_CLIENT_DB(global, Rec, store, file, row_size) \
    TellDb(global, file, fn, user);                        \
    ++told;
#include "wxl/core/client_dbs.def"
#undef WXL_CLIENT_DB
        return told;
    }

    // --- Asset: models, map objects, doodads (asset.def) -------------------------------------

    const char* WXL_CALL model_path(const void* shared) {
        return shared ? static_cast<const CM2Shared*>(shared)->m_fileName : nullptr;
    }

    const void* WXL_CALL model_bytes(const void* shared, uint32_t* size) {
        if (!shared) {
            return nullptr;
        }
        const CM2Shared* model = static_cast<const CM2Shared*>(shared);
        if (size) {
            *size = model->m_fileSize;
        }
        return model->m_data;
    }

    wxl_bool WXL_CALL model_replace_bytes(void* shared, const void* bytes, uint32_t size) {
        if (!shared || !bytes || !size) {
            return WXL_FALSE;
        }
        void* copy = SIMD::AllocA16(size, __FILE__, __LINE__);
        if (!copy) {
            return WXL_FALSE;
        }
        std::memcpy(copy, bytes, size);
        CM2Shared* model = static_cast<CM2Shared*>(shared);
        M2Data* read = model->m_data;
        model->m_data = static_cast<M2Data*>(copy);
        model->m_fileSize = size;
        if (read) {
            SIMD::FreeA16(read);
        }
        return WXL_TRUE;
    }

    // --- M2: the model loader (ModelParsed, SkinProfileParsed, ModelSequenceLoad, wxl.m2file) ---

    wxl_bool WXL_CALL m2_file_info(const void* file, wxl_core_m2_file* out) {
        const CM2Shared* shared = static_cast<const CM2Shared*>(file);
        if (!shared || !out || !shared->m_data) {
            return WXL_FALSE;
        }
        out->path = shared->m_fileName;
        out->header = shared->m_data;
        out->size = shared->m_fileSize;
        out->skin = shared->m_skinData;
        return WXL_TRUE;
    }

    void WXL_CALL m2_file_animate_bones(void* file) {
        if (CM2Shared* shared = static_cast<CM2Shared*>(file)) {
            shared->m_billboardBoneCount++;
        }
    }

    const char* WXL_CALL map_obj_path(const void* map_obj) {
        return map_obj ? static_cast<const CMapObj*>(map_obj)->m_wmoName : nullptr;
    }

    const void* WXL_CALL map_area_bytes(const void* area, int32_t* x, int32_t* y, void** file, uint32_t* size) {
        if (!area) {
            return nullptr;
        }
        const CMapArea* tile = static_cast<const CMapArea*>(area);
        if (x) {
            *x = tile->m_index.x;
        }
        if (y) {
            *y = tile->m_index.y;
        }
        if (file) {
            *file = tile->m_file;
        }
        if (size) {
            *size = tile->m_fileSize;
        }
        return tile->m_fileBuffer;
    }

    wxl_bool WXL_CALL file_name(void* file, char* buffer, uint32_t capacity) {
        if (!file || !buffer || !capacity) {
            return WXL_FALSE;
        }
        buffer[0] = 0;
        return SFile::GetActualFileName(static_cast<SFileImpl*>(file), buffer, capacity) ? WXL_TRUE : WXL_FALSE;
    }

    const void* WXL_CALL map_obj_group_bytes(const void* group, void** parent, uint32_t* index, uint32_t* size) {
        if (!group) {
            return nullptr;
        }
        const CMapObjGroup* g = static_cast<const CMapObjGroup*>(group);
        if (parent) {
            *parent = g->m_parent;
        }
        if (size) {
            *size = g->m_fileSize;
        }
        if (index) {
            *index = 0;
            const CMapObj* root = g->m_parent;
            const uint32_t count = root ? static_cast<uint32_t>(root->m_mapObjGroupCount) : 0;
            for (uint32_t i = 0; i < count && i < 512; i++) {
                if (root->m_mapObjGroupArray[i] == g) {
                    *index = i;
                    break;
                }
            }
        }
        return g->m_filePtr;
    }

    const void* WXL_CALL model_skin_bytes(const void* shared) {
        return shared ? static_cast<const CM2Shared*>(shared)->m_skinData : nullptr;
    }

    const char* WXL_CALL texture_name(const void* texture) {
        return texture ? static_cast<const CTexture*>(texture)->m_filename : nullptr;
    }

    uint32_t WXL_CALL m2_cache_evict(const char* leaf, void** out, uint32_t capacity) {
        return WarcraftXLEvictModels(leaf, out, capacity);
    }

    wxl_bool WXL_CALL unit_update_display(void* object) {
        if (!object) {
            return WXL_FALSE;
        }
        CGObject_C* unit = static_cast<CGObject_C*>(object);
        if (unit->m_typeId != ID_UNIT && unit->m_typeId != ID_PLAYER) {
            return WXL_FALSE;
        }
        unit->UpdateDisplayInfo(1);
        return WXL_TRUE;
    }

    uint32_t WXL_CALL texture_reload(const char* name, uint32_t* busy) {
        return WarcraftXLReloadTextures(name, busy);
    }

    const void* WXL_CALL map_obj_bytes(const void* map_obj, uint32_t* size) {
        if (!map_obj) {
            return nullptr;
        }
        const CMapObj* root = static_cast<const CMapObj*>(map_obj);
        if (size) {
            *size = static_cast<uint32_t>(root->m_wmoFileSize);
        }
        return root->m_pWmoData;
    }

    wxl_bool WXL_CALL doodad_position(const void* def, wxl_vec3* out) {
        if (!def || !out) {
            return WXL_FALSE;
        }
        return Give(out, static_cast<const CMapDoodadDef*>(def)->m_position);
    }

    // --- gameplay.def ---------------------------------------------------------------------------

    int32_t WXL_CALL unit_aura_slot(const void* unit, const void* aura) {
        if (!unit || !aura) {
            return -1;
        }
        CGUnit_C* owner = const_cast<CGUnit_C*>(static_cast<const CGUnit_C*>(unit));
        const uint32_t count = owner->GetAuraCount();
        if (!count) {
            return -1;
        }
        const AuraData* first = &owner->m_auras[0];
        const AuraData* slot = static_cast<const AuraData*>(aura);
        if (slot < first || slot >= first + count) {
            return -1;
        }
        return static_cast<int32_t>(slot - first);
    }

    uint32_t WXL_CALL os_time_ms(void) {
        return OsGetAsyncTimeMs();
    }

    uint64_t WXL_CALL loot_object_guid(void) {
        return CGLootInfo::m_lootObj.m_guid;
    }

    uint32_t WXL_CALL loot_kind(void) {
        return static_cast<uint32_t>(CGLootInfo::m_lootAcquire);
    }

    uint32_t WXL_CALL loot_money(void) {
        return static_cast<uint32_t>(CGLootInfo::m_coins);
    }

    wxl_bool WXL_CALL loot_slot(uint32_t index, int32_t* out) {
        if (!out || index >= Extent(CGLootInfo::m_slots) || !CGLootInfo::m_slots[index].m_itemID) {
            return WXL_FALSE;
        }
        const LootSlot& slot = CGLootInfo::m_slots[index];
        out[0] = slot.m_itemID;
        out[1] = slot.m_count;
        out[2] = slot.m_displayId;
        out[3] = slot.m_randomSuffix;
        out[4] = slot.m_randomPropertyId;
        out[5] = slot.m_slot;
        out[6] = slot.m_slotType;
        return WXL_TRUE;
    }

    uint32_t WXL_CALL loot_next_roll_id(void) {
        return static_cast<uint32_t>(WarcraftXLNextLootRollId());
    }

    int32_t WXL_CALL ui_map_id(void) {
        return CGGameUI::m_iCurrentMapID;
    }

    wxl_bool WXL_CALL faction_standing(uint32_t list_id, int32_t* faction_id, int32_t* base, int32_t* standing) {
        if (list_id >= Extent(CGReputationInfo::m_factionStandings)) {
            return WXL_FALSE;
        }
        const FactionStanding& entry = CGReputationInfo::m_factionStandings[list_id];
        if (faction_id) {
            *faction_id = entry.m_factionId;
        }
        if (base) {
            *base = entry.m_base;
        }
        if (standing) {
            *standing = entry.m_standing;
        }
        return WXL_TRUE;
    }

    uint64_t WXL_CALL ui_interact_target(void) {
        return CGGameUI::m_interactTarget.m_guid;
    }

    uint64_t WXL_CALL trade_partner(void) {
        return CGTradeInfo::m_tradingPlayer.m_guid;
    }

    void WXL_CALL group_invite_answer(wxl_bool accept) {
        if (accept) {
            CGPlayer_C::AcceptGroup(0);
        } else {
            CGPlayer_C::DeclineGroup();
        }
    }

    void WXL_CALL resurrect_answer(uint64_t offerer, uint32_t sickness, uint32_t use_timer, wxl_bool accept) {
        CGPlayer_C::s_resurrectOfferer = WOWGUID{ offerer };
        CGPlayer_C::s_resurrectOfferSickness = sickness;
        CGPlayer_C::s_resurrectOfferUseTimer = use_timer;
        CGPlayer_C::AcceptResurrectRequest(accept ? 1 : 0);
    }

    void WXL_CALL duel_answer(uint64_t arbiter, uint64_t challenger, wxl_bool accept) {
        WarcraftXLAnswerDuel(WOWGUID{ arbiter }, WOWGUID{ challenger }, accept != WXL_FALSE);
    }

    // --- unit.def, item.def: what the client works out of an object ----------------------------

    wxl_bool WXL_CALL unit_speed(const void* object, uint32_t index, float* out) {
        const CGUnit_C* unit = static_cast<const CGUnit_C*>(Object(object));
        const CMovementShared* movement = unit ? unit->m_movement : nullptr;
        if (!movement || !out) {
            return WXL_FALSE;
        }
        const float speeds[] = { movement->m_walkSpeed,   movement->m_runSpeed,       movement->m_runBackSpeed,
                                 movement->m_swimSpeed,   movement->m_swimBackSpeed,  movement->m_flightSpeed,
                                 movement->m_flightBackSpeed, movement->m_turnRate, movement->m_pitchRate };
        if (index >= Extent(speeds)) {
            return WXL_FALSE;
        }
        *out = speeds[index];
        return WXL_TRUE;
    }

    int32_t WXL_CALL unit_reaction(const void* object, const void* other) {
        if (!object || !other) {
            return -1;
        }
        return static_cast<int32_t>(static_cast<const CGUnit_C*>(Object(object))
                                        ->UnitReaction(static_cast<const CGUnit_C*>(Object(other))));
    }

    int32_t WXL_CALL unit_creature_type(const void* object) {
        return object ? static_cast<const CGUnit_C*>(Object(object))->GetCreatureType() : 0;
    }

    uint32_t WXL_CALL unit_creature_family(const void* object) {
        return object ? static_cast<const CGUnit_C*>(Object(object))->GetCreatureFamily() : 0;
    }

    uint32_t WXL_CALL unit_classification(const void* object) {
        return object ? static_cast<const CGUnit_C*>(Object(object))->GetCreatureClassification() : 0;
    }

    wxl_bool WXL_CALL unit_knows_spell(const void* object, uint32_t spell) {
        if (!object) {
            return WXL_FALSE;
        }
        CGUnit_C* unit = const_cast<CGUnit_C*>(static_cast<const CGUnit_C*>(Object(object)));
        return unit->IsSpellKnown(static_cast<int32_t>(spell)) ? WXL_TRUE : WXL_FALSE;
    }

    uint32_t WXL_CALL unit_aura_count(const void* object) {
        return object ? static_cast<const CGUnit_C*>(Object(object))->GetAuraCount() : 0;
    }

    const void* WXL_CALL unit_aura_at(const void* object, uint32_t index) {
        if (!object) {
            return nullptr;
        }
        return const_cast<CGUnit_C*>(static_cast<const CGUnit_C*>(Object(object)))->GetAura(index);
    }

    wxl_bool WXL_CALL unit_is_outdoors(const void* object) {
        return object && static_cast<const CGUnit_C*>(Object(object))->IsOutdoors() ? WXL_TRUE : WXL_FALSE;
    }

    const void* WXL_CALL item_stats(const void* object) {
        return object ? static_cast<const CGItem_C*>(Object(object))->GetStats() : nullptr;
    }

    // --- action.def -----------------------------------------------------------------------------

    uint64_t WXL_CALL party_member_guid(uint32_t index) {
        return index < Extent(CGPartyInfo::m_members) ? CGPartyInfo::m_members[index].m_guid : 0;
    }

    uint32_t WXL_CALL raid_member_count(void) {
        return CGRaidInfo::m_numMembers;
    }

    uint64_t WXL_CALL raid_member_guid(uint32_t index) {
        if (index >= Extent(CGRaidInfo::m_members) || !CGRaidInfo::m_members[index]) {
            return 0;
        }
        return CGRaidInfo::m_members[index]->m_guid.m_guid;
    }

    wxl_bool WXL_CALL action_target(uint64_t guid) {
        CGGameUI::Target(WOWGUID{ guid });
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL action_interact(uint64_t guid) {
        return CGGameUI::OnSpriteRightClick(WOWGUID{ guid }) ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL action_cast_spell(uint32_t spell, uint64_t target) {
        return Spell_C::CastSpell(static_cast<int32_t>(spell), nullptr, WOWGUID{ target }, false) ? WXL_TRUE
                                                                                                    : WXL_FALSE;
    }

    wxl_bool WXL_CALL action_stop_casting(void) {
        // Script_SpellStopCasting: the cast in progress, else the repeating spell.
        if (Spell_C_IsActivePlayerCasting()) {
            Spell_C_StopCasting();
            return WXL_TRUE;
        }
        if (!Spell_C_GetAutoRepeatingSpell()) {
            return WXL_FALSE;
        }
        Spell_C_CancelAutoRepeat(true);
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL action_use_item(const void* item, uint64_t target) {
        if (!item) {
            return WXL_FALSE;
        }
        WOWGUID on{ target };
        CGItem_C* used = const_cast<CGItem_C*>(static_cast<const CGItem_C*>(Object(item)));
        return used->UseItem(&on, 0) ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL action_follow(const void* mover, uint64_t guid) {
        if (!mover) {
            return WXL_FALSE;
        }
        CGUnit_C* unit = const_cast<CGUnit_C*>(static_cast<const CGUnit_C*>(Object(mover)));
        return unit->SetTrackingTarget(WOWGUID{ guid }, TRACKTYPE_FOLLOW) ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL action_move_to(const void* player, wxl_vec3 point) {
        if (!player) {
            return WXL_FALSE;
        }
        CGPlayer_C* mover = const_cast<CGPlayer_C*>(static_cast<const CGPlayer_C*>(Object(player)));
        const WOWGUID none{};
        const C3Vector where{ point.x, point.y, point.z };
        return mover->ClickToMove(TRACKTYPE_MOVE, &none, &where, 0.0f) ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL action_face(const void* player, float facing) {
        if (!player) {
            return WXL_FALSE;
        }
        CGUnit_C* unit = const_cast<CGUnit_C*>(static_cast<const CGUnit_C*>(Object(player)));
        if (CGUnit_C::s_activeMover != unit->m_obj->m_guid || !CGInputControl::CanTurn(unit)) {
            return WXL_FALSE;
        }
        unit->OnSetRawFacingLocal(OsGetAsyncTimeMs(), facing);
        return WXL_TRUE;
    }

    // --- camera.def -----------------------------------------------------------------------------

    void* WXL_CALL camera_world(void) {
        return CGWorldFrame::GetActiveCamera();
    }

    wxl_bool WXL_CALL camera_read(const void* camera, wxl_core_camera* out) {
        if (!camera || !out) {
            return WXL_FALSE;
        }
        const CGCamera* cam = static_cast<const CGCamera*>(camera);
        Give(&out->position, cam->m_position);
        Give(&out->forward, cam->Forward());
        Give(&out->left, cam->Right());
        Give(&out->up, cam->Up());
        out->fov = cam->FOV();
        out->fov_base = cam->m_fov;
        out->near_z = cam->m_nearZ;
        out->far_z = cam->m_farZ;
        out->aspect = cam->m_aspect;
        Give(&out->pivot, cam->m_targetPosition);
        out->pivot.z += cam->m_height;
        out->followed = cam->m_targetGUID.m_guid;
        out->cinematic = cam->m_model ? 1u : 0u;
        return WXL_TRUE;
    }

    void WXL_CALL camera_write(void* camera, const wxl_core_camera* view) {
        if (!camera || !view) {
            return;
        }
        CGCamera* cam = static_cast<CGCamera*>(camera);
        const C3Vector forward{ view->forward.x, view->forward.y, view->forward.z };
        if (forward.x * forward.x + forward.y * forward.y + forward.z * forward.z < 0.00000023841858f) {
            return;
        }
        cam->m_position = C3Vector{ view->position.x, view->position.y, view->position.z };
        cam->SetFacing(forward);
        if (cam->m_relativeTo) {
            // The facing is kept in the parent's frame, which Forward() takes back to the world:
            // each row times the transpose of ParentToWorld.
            const C33Matrix parent = cam->ParentToWorld();
            C33Matrix& m = cam->m_facing;
            const C3Vector rows[3] = { { m.a0, m.a1, m.a2 }, { m.b0, m.b1, m.b2 }, { m.c0, m.c1, m.c2 } };
            C3Vector local[3];
            for (int i = 0; i < 3; ++i) {
                const C3Vector& w = rows[i];
                local[i] = C3Vector{ w.x * parent.a0 + w.y * parent.a1 + w.z * parent.a2,
                                     w.x * parent.b0 + w.y * parent.b1 + w.z * parent.b2,
                                     w.x * parent.c0 + w.y * parent.c1 + w.z * parent.c2 };
            }
            m.a0 = local[0].x; m.a1 = local[0].y; m.a2 = local[0].z;
            m.b0 = local[1].x; m.b1 = local[1].y; m.b2 = local[1].z;
            m.c0 = local[2].x; m.c1 = local[2].y; m.c2 = local[2].z;
        }
        cam->m_fov = view->fov_base;
    }

    wxl_bool WXL_CALL world_trace(wxl_vec3 start, wxl_vec3 finish, uint32_t flags, wxl_vec3* hit) {
        if (!hit) {
            return WXL_FALSE;
        }
        const C3Vector from{ start.x, start.y, start.z };
        const C3Vector to{ finish.x, finish.y, finish.z };
        C3Vector point{ 0.0f, 0.0f, 0.0f };
        float fraction = 1.0f;
        if (!World::Intersect(from, to, &point, &fraction, flags, nullptr)) {
            return WXL_FALSE;
        }
        return Give(hit, point);
    }

    // --- input.def ------------------------------------------------------------------------------

    wxl_bool WXL_CALL input_cursor_visible(void) {
        return g_theGxDevicePtr && g_theGxDevicePtr->m_cursorVisible ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL input_cursor_set_visible(wxl_bool visible) {
        if (g_theGxDevicePtr) {
            g_theGxDevicePtr->CursorSetVisible(visible ? 1 : 0);
        }
    }

    void WXL_CALL input_cursor_restore(void) {
        if (CGInputControl::s_inputControl) {
            CGInputControl::s_inputControl->UpdateMouseMode(1);
        } else if (g_theGxDevicePtr) {
            g_theGxDevicePtr->CursorSetVisible(1);
        }
    }

    wxl_bool WXL_CALL input_pointer_over_world(void) {
        CGWorldFrame* frame = CGWorldFrame::s_currentWorldFrame;
        return frame && CSimpleTop::s_instance && CSimpleTop::s_instance->m_mouseFocus == frame ? WXL_TRUE : WXL_FALSE;
    }
}
