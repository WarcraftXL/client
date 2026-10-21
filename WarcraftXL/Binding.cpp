#include "WarcraftXL/Binding.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
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
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/Container.h"
#include "WoW/Source/Object/Corpse.h"
#include "WoW/Source/Object/DynamicObject.h"
#include "WoW/Source/Object/GameObject.h"
#include "WoW/Source/Object/Item.h"
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
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/WorldClient/MapArea.h"
#include "WoW/Source/WorldClient/World.h"
#include "wxl/events.h"
#include "wxl/services/script.h"

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

    // --- objectmgr.def --------------------------------------------------------------------------

    const void* WXL_CALL local_player_object(void) {
        return ClntObjMgrGetActivePlayerObj();
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
        // Method 0 is FILE_BEGIN: the offset is absolute, which is what read_at means.
        return SFile::SetFilePointer(File(file), static_cast<int32_t>(offset & 0xFFFFFFFFu), &high, 0)
                   ? WXL_TRUE
                   : WXL_FALSE;
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

    // --- unit.def: how one unit regards another ------------------------------------------------

    int32_t WXL_CALL unit_reaction(const void* unit, const void* other) {
        return static_cast<const CGUnit_C*>(unit)->UnitReaction(static_cast<const CGUnit_C*>(other));
    }

    // --- Gx: wxl.gx ---------------------------------------------------------------------------
    // Both D3D9 devices keep the same copy of what they last set (m_deviceStates, the current
    // declaration and streams); invalidating it makes the next IStateSync send everything again.

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

    void* WXL_CALL gx_object_model(const void* object) {
        return RootModel(Object(object)->m_worldModel);
    }

    wxl_bool WXL_CALL gx_model_draw(const void* render, wxl_gx_model_draw* out) {
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

    // --- M2: wxl.m2 -----------------------------------------------------------------------------

    wxl_bool WXL_CALL component_wearer(const void* component, uint32_t item_slot, wxl_m2_equipment* out) {
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
        static CStatus s_status;
        if (!model || !path || !*path) {
            return WXL_FALSE;
        }
        CTexture* texture = CreateTexture(path, &s_status);
        if (!texture) {
            return WXL_FALSE;
        }
        // The model takes a handle of its own, as CCharacterComponent::AddLink relies on.
        static_cast<CM2Model*>(model)->ReplaceTexture(type, texture);
        HandleClose(reinterpret_cast<HOBJECT>(texture));
        return WXL_TRUE;
    }

    // Which parent bone each child bone takes its pose from, for one pair of model files: by key
    // bone id first, then by name, and a bone matching neither takes its nearest matched
    // ancestor's. Each animate thread keeps the last few pairs it built; a pair is rebuilt when
    // either file's bone count no longer agrees, which is all a recycled file can be told by.
    struct BoneMap {
        const M2Data* child = nullptr;
        const M2Data* parent = nullptr;
        uint32_t childCount = 0;
        uint32_t parentCount = 0;
        bool matched = false;
        std::vector<uint16_t> parentOf;
    };

    const BoneMap& MapBones(const M2Data* child, const M2Data* parent) {
        thread_local BoneMap s_maps[8];
        thread_local uint32_t s_next = 0;

        const uint32_t childCount = child->m_bones.m_count;
        const uint32_t parentCount = parent->m_bones.m_count;
        for (const BoneMap& map : s_maps) {
            if (map.child == child && map.parent == parent && map.childCount == childCount && map.parentCount == parentCount) {
                return map;
            }
        }

        BoneMap& map = s_maps[s_next++ % 8];
        map.child = child;
        map.parent = parent;
        map.childCount = childCount;
        map.parentCount = parentCount;
        map.matched = false;
        map.parentOf.assign(childCount, 0xFFFF);

        for (uint32_t i = 0; i < childCount; i++) {
            const M2CompBone& bone = child->m_bones[i];
            uint16_t found = 0xFFFF;
            const int32_t key = static_cast<int32_t>(bone.m_boneId);
            if (key >= 0 && static_cast<uint32_t>(key) < parent->m_boneIndicesById.m_count) {
                const uint16_t index = parent->m_boneIndicesById[static_cast<uint32_t>(key)];
                if (index < parentCount) {
                    found = index;
                }
            }
            if (found == 0xFFFF && bone.m_boneNameCRC) {
                for (uint32_t j = 0; j < parentCount; j++) {
                    if (parent->m_bones[j].m_boneNameCRC == bone.m_boneNameCRC) {
                        found = static_cast<uint16_t>(j);
                        break;
                    }
                }
            }
            if (found != 0xFFFF) {
                map.matched = true;
            } else {
                // A retail piece names leg and arm bones by key bones a 3.3.5 body lacks (thighs,
                // knees, shins: 55-58, 70-75). The body has those bones under other ids, at the
                // same place: take the body bone nearest this bone's pivot among the descendants of
                // its nearest matched ancestor's counterpart (the whole body for none).
                const uint16_t ancestor = bone.m_parentIndex < i ? map.parentOf[bone.m_parentIndex] : 0xFFFF;
                float best = 3.4e38f;
                for (uint32_t j = 0; j < parentCount; j++) {
                    if (ancestor != 0xFFFF) {
                        uint32_t walk = j;
                        for (uint32_t depth = 0; walk != ancestor && walk < parentCount && depth < parentCount; depth++) {
                            walk = parent->m_bones[walk].m_parentIndex;
                        }
                        if (walk != ancestor) {
                            continue;
                        }
                    }
                    const C3Vector& a = bone.m_pivot;
                    const C3Vector& b = parent->m_bones[j].m_pivot;
                    const float d = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
                    if (d < best) {
                        best = d;
                        found = static_cast<uint16_t>(j);
                    }
                }
                if (found == 0xFFFF) {
                    found = ancestor;
                }
            }
            map.parentOf[i] = found;
        }
        return map;
    }

    wxl_bool WXL_CALL m2_pose_from_parent(void* model) {
        CM2Model* child = static_cast<CM2Model*>(model);
        CM2Model* parent = child ? child->m_attachParent : nullptr;
        if (!parent || !(child->m_fFlags & 1) || !(parent->m_fFlags & 1) || !child->m_boneMatrices || !parent->m_boneMatrices) {
            return WXL_FALSE;
        }
        const BoneMap& map = MapBones(child->m_shared->m_data, parent->m_shared->m_data);
        if (!map.matched) {
            return WXL_FALSE;
        }
        // Both are in view space: the parent's posed body is the child's, bone for bone.
        for (uint32_t i = 0; i < map.childCount; i++) {
            if (map.parentOf[i] != 0xFFFF) {
                child->m_boneMatrices[i] = parent->m_boneMatrices[map.parentOf[i]];
            }
        }
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

    uint16_t KeyBoneIndex(const CM2Model* model, uint32_t key) {
        const M2Data* data = model->m_shared->m_data;
        if (key >= data->m_boneIndicesById.m_count) {
            return 0xFFFF;
        }
        const uint16_t index = data->m_boneIndicesById[key];
        return index < data->m_bones.m_count ? index : 0xFFFF;
    }

    wxl_bool WXL_CALL m2_has_key_bone(const void* model, uint32_t key_bone) {
        const CM2Model* m = static_cast<const CM2Model*>(model);
        if (!m) {
            return WXL_FALSE;
        }
        return !(m->m_fFlags & 1) || KeyBoneIndex(m, key_bone) != 0xFFFF ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool WXL_CALL m2_pose_on_bone(void* model, uint32_t key_bone, const wxl_vec3* offset) {
        CM2Model* child = static_cast<CM2Model*>(model);
        CM2Model* parent = child ? child->m_attachParent : nullptr;
        if (!parent || !offset || !(child->m_fFlags & 1) || !(parent->m_fFlags & 1) || !child->m_boneMatrices || !parent->m_boneMatrices) {
            return WXL_FALSE;
        }
        const uint16_t bone = KeyBoneIndex(parent, key_bone);
        if (bone == 0xFFFF) {
            return WXL_FALSE;
        }
        // What CM2Model::AnimateAttachmentsMT hands an attachment: its bone, moved to the point.
        C44Matrix matrix = parent->m_boneMatrices[bone];
        matrix.Translate(C3Vector(offset->x, offset->y, offset->z));
        const uint32_t count = child->m_shared->m_data->m_bones.m_count;
        for (uint32_t i = 0; i < count; i++) {
            child->m_boneMatrices[i] = matrix;
        }
        return WXL_TRUE;
    }

    // --- M2: wxl.m2file -------------------------------------------------------------------------

    wxl_bool WXL_CALL m2_file_info(const void* file, wxl_m2file* out) {
        const CM2Shared* shared = static_cast<const CM2Shared*>(file);
        if (!shared || !out || !shared->m_data) {
            return WXL_FALSE;
        }
        out->name = shared->m_fileName;
        out->data = shared->m_data;
        out->size = shared->m_fileSize;
        out->skin = shared->m_skinData;
        return WXL_TRUE;
    }

    void WXL_CALL m2_file_set_size(void* file, uint32_t size) {
        CM2Shared* shared = static_cast<CM2Shared*>(file);
        if (shared && size <= shared->m_fileSize) {
            shared->m_fileSize = size;
        }
    }

    void WXL_CALL m2_file_animate_bones(void* file) {
        if (CM2Shared* shared = static_cast<CM2Shared*>(file)) {
            shared->m_billboardBoneCount++;
        }
    }
}
