#pragma once

#include "owf_structs.hpp"

struct luau_State;
union luau_GCObject;

#define luau_CommonHeader uint8_t tt; uint8_t marked; uint8_t memcat

using luau_CFunction = int(*)(luau_State*);
using luau_Continuation = void*;

union luau_Value
{
	uintptr_t as_uintptr;
	uint32_t as_bool;
	float as_float;
	luau_GCObject* gc;
};

enum luau_Type
{
	LUAU_NIL = 0,
	LUAU_BOOL = 1,
	LUAU_LIGHTUSERDATA = 2,
	LUAU_NUMBER = 3,
	LUAU_STRING = 5,
	LUAU_TABLE = 6,
	LUAU_FUNCTION = 7,
	LUAU_USERDATA = 8,
	LUAU_TTHREAD = 9,
	LUAU_TBUFFER = 10,

	// values below this line are used in GCObject tags but may never show up in TValue type tags
	LUAU_TPROTO = 11,
	LUAU_TUPVAL = 12,
	LUAU_TDEADKEY = 13,
};


inline bool lua51 = false;
inline int32_t luau_type_shift = 0;
[[nodiscard]] inline uint32_t luau_tt(luau_Type t) noexcept
{
	return static_cast<uint32_t>(t) + (t >= LUAU_STRING ? luau_type_shift : 0);
}

struct luau_TValue
{
	/* 0x00 */ luau_Value value;
	/* 0x08 */ uint32_t tag_words[2]; // Luau: extra, tt; Lua 5.1: tt, padding

	[[nodiscard]] uint32_t& type() noexcept { return tag_words[lua51 ? 0 : 1]; }
	[[nodiscard]] uint32_t type() const noexcept { return tag_words[lua51 ? 0 : 1]; }
	[[nodiscard]] bool isType(luau_Type t) const noexcept { return type() == luau_tt(t); }
	void setType(luau_Type t) noexcept { type() = luau_tt(t); }

	[[nodiscard]] char* getString() noexcept
	{
		return reinterpret_cast<char*>(value.as_uintptr + 0x18);
	}

	[[nodiscard]] Object* getObject() const noexcept
	{
		if (game_version >= GV(38, 5, 0))
		{
			return **(Object***)(value.as_uintptr + 0x18);
		}
		return ***(Object****)(value.as_uintptr + (!lua51 ? 0x18 : game_version >= GV(26, 0, 0) ? 0x28 : game_version >= GV(25, 7, 0) ? 0x30 : 0x38));
	}
};
#if SOUP_BITS == 64
static_assert(sizeof(luau_TValue) == 0x10);
#endif

using luau_panic_func_t = void(*)(luau_State* L, int status);

/*struct luau_GlobalState_33_6
{
	PAD(0x000, 0x018) void* ud;
	PAD(0x020, 0xC08) void* error_longjump_data;
	PAD(0xC10, 0xC48) luau_panic_func_t panic_func;
};

// U35, U38
struct luau_GlobalState_38_0
{
	PAD(0x000, 0x018) void* ud;
	PAD(0x020, 0xC10) void* error_longjump_data;
	PAD(0xC18, 0xC50) luau_panic_func_t panic_func;
	PAD(0xC58, 0x1168);
};
#if SOUP_BITS == 64
static_assert(sizeof(luau_GlobalState_38_0_x) == 0x1168);
#endif

struct luau_GlobalState_38_5
{
	PAD(0x000, 0x018) void* ud;
	PAD(0x020, 0xCA8) void* error_longjump_data;
	PAD(0xCA8 + 8, 0xCE8) luau_panic_func_t panic_func;
};*/

struct luau_GlobalState
{
	PAD(0x000, 0x018) void* ud;

	inline static unsigned int error_longjump_data_offset;
	inline static unsigned int panic_func_offset;

	[[nodiscard]] SOUP_PURE void*& error_longjump_data() noexcept
	{
		return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(this) + error_longjump_data_offset);
	}

	[[nodiscard]] SOUP_PURE luau_panic_func_t& panic_func() noexcept
	{
		return *reinterpret_cast<luau_panic_func_t*>(reinterpret_cast<uintptr_t>(this) + panic_func_offset);
	}
};

using luau_StkId = luau_TValue*;

struct luau_CallInfo
{
	luau_StkId base;
	luau_StkId func;
	luau_StkId top;
};

#define luau_savestack(L, p) ((char*)(p) - (char*)L->stack())
#define luau_restorestack(L, n) ((luau_TValue*)((char*)L->stack() + (n)))

template <typename T>
[[nodiscard]] SOUP_FORCEINLINE T& luau_field(const void* base, unsigned int luau_off, unsigned int lua51_off) noexcept
{
	return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + (lua51 ? lua51_off : luau_off));
}

struct luau_State
{
	[[nodiscard]] luau_TValue*& outtop() noexcept { return luau_field<luau_TValue*>(this, 0x08, 0x10); }
	[[nodiscard]] luau_TValue*& intop() noexcept { return luau_field<luau_TValue*>(this, 0x10, 0x18); }
	[[nodiscard]] luau_GlobalState*& global_state() noexcept { return luau_field<luau_GlobalState*>(this, 0x18, 0x20); }
	[[nodiscard]] luau_CallInfo*& ci() noexcept { return luau_field<luau_CallInfo*>(this, 0x20, 0x28); }
	[[nodiscard]] luau_TValue*& stack_last() noexcept { return luau_field<luau_TValue*>(this, 0x28, 0x38); }
	[[nodiscard]] luau_TValue*& stack() noexcept { return luau_field<luau_TValue*>(this, 0x30, 0x40); }
	[[nodiscard]] uint16_t& nCcalls() noexcept { return luau_field<uint16_t>(this, 0x50, 0x60); }
	[[nodiscard]] void*& error_longjump_data() noexcept
	{
		return *reinterpret_cast<void**>((lua51 ? reinterpret_cast<uintptr_t>(this) : reinterpret_cast<uintptr_t>(global_state())) + luau_GlobalState::error_longjump_data_offset);
	}

	luau_TValue* getValue(int idx)
	{
		return idx < 0 ? &outtop()[idx] : &intop()[idx - 1];
	}
};

struct luau_CallState
{
	luau_State* const L;
	luau_CallInfo* const ci;
	const uint16_t nCcalls;

	explicit luau_CallState(luau_State* L) noexcept : L(L), ci(L->ci()), nCcalls(L->nCcalls()) {}

	void restore() const noexcept
	{
		L->ci() = ci;
		L->nCcalls() = nCcalls;
	}
};

struct luau_Closure
{
	[[nodiscard]] uint8_t isC() const noexcept { return luau_field<uint8_t>(this, 0x03, 0x0A); }
	[[nodiscard]] uint8_t nupvalues() const noexcept { return luau_field<uint8_t>(this, 0x04, 0x0B); }
	[[nodiscard]] luau_CFunction func() const noexcept { return luau_field<luau_CFunction>(this, 0x18, 0x20); }
	[[nodiscard]] luau_TValue* upvalue(int i) noexcept
	{
		if (lua51 && !isC())
		{
			const auto upval = reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(this) + 0x28)[i];
			return *reinterpret_cast<luau_TValue**>(upval + 0x10);
		}
		luau_TValue* tval = &reinterpret_cast<luau_TValue*>(reinterpret_cast<uintptr_t>(this) + (isC() ? (lua51 ? 0x28 : 0x30) : 0x20))[i];
		if (tval->isType(LUAU_TUPVAL))
		{
			tval = *reinterpret_cast<luau_TValue**>(tval->value.as_uintptr + 0x08); // Luau UpVal::v
		}
		return tval;
	}
};

/*using luau_Alloc = void*(*)(void* ud, void* ptr, size_t osize, size_t nsize);
inline void* luau_alloc_impl(void* ud, void* ptr, size_t osize, size_t nsize)
{
	if (nsize == 0)
	{
		soup::free(ptr);
		return nullptr;
	}
	else
	{
		return soup::realloc(ptr, nsize);
	}
}*/

/*using luau_newstate_t = luau_State*(*)(luau_Alloc f, void* ud, char);
inline luau_newstate_t luau_newstate = nullptr;*/

inline int luau_gettop(luau_State* L)
{
	return L->outtop() - L->intop();
}

inline bool luau_push_number(luau_State* luau_L, float value)
{
	SOUP_IF_LIKELY (luau_L->outtop() != luau_L->stack_last())
	{
		luau_L->outtop()->value.as_float = value;
		luau_L->outtop()->setType(LUAU_NUMBER);
		luau_L->outtop()++;
		return true;
	}
	return false;
}

inline bool luau_push_lightuserdata(luau_State* luau_L, void* value)
{
	SOUP_IF_LIKELY (luau_L->outtop() != luau_L->stack_last())
	{
		luau_L->outtop()->value.as_uintptr = reinterpret_cast<uintptr_t>(value);
		luau_L->outtop()->setType(LUAU_LIGHTUSERDATA);
		luau_L->outtop()++;
		return true;
	}
	return false;
}

using luau_pushstring_t = const char*(*)(luau_State*, const char*);
inline luau_pushstring_t luau_pushstring = nullptr;

using luau_pushpointer_t = void*(*)(luau_State*, void*);
inline luau_pushpointer_t luau_pushpointer = nullptr;

using luau_pushobject_t = Object*(*)(luau_State*, Object*);
inline luau_pushobject_t luau_pushobject = nullptr;

using luau_pushcclosurek_t = void(*)(luau_State* L, luau_CFunction func, const char* debugname, int nup, luau_Continuation cont);
inline luau_pushcclosurek_t luau_pushcclosurek = nullptr;

using luau_next_t = int(*)(luau_State* L, int idx);
inline luau_next_t luau_next = nullptr;

using luau_gettable_t = int(*)(luau_State*, int idx);
inline luau_gettable_t luau_gettable = nullptr;

using luau_createtable_t = void(*)(luau_State*, int, int);
inline luau_createtable_t luau_createtable = nullptr;

using luau_settable_t = void(*)(luau_State*, int);
inline luau_settable_t luau_settable = nullptr;

using luauD_call_t = int(*)(luau_State* L, luau_TValue* func, int nresults);
inline luauD_call_t luauD_call = nullptr;

using lua51_pushcclosure_t = void(*)(luau_State* L, luau_CFunction func, int nup);
inline lua51_pushcclosure_t lua51_pushcclosure = nullptr;

using lua51_pushlstring_t = void(*)(luau_State* L, const char* s, size_t len);
inline lua51_pushlstring_t lua51_pushlstring = nullptr;

using lua51_luaV_access_t = void(*)(luau_State* L, const luau_TValue* t, luau_TValue* key, luau_TValue* val);
inline lua51_luaV_access_t lua51_luaV_gettable = nullptr;
inline lua51_luaV_access_t lua51_luaV_settable = nullptr;

using lua51_luaH_next_t = int(*)(luau_State* L, void* table, luau_TValue* key);
inline lua51_luaH_next_t lua51_luaH_next = nullptr;

using lua51_newuserdata_t = uint8_t*(*)(luau_State* L, size_t size);
inline lua51_newuserdata_t lua51_newuserdata = nullptr;

using lua51_set_class_metatable_t = void(*)(luau_State* L, void* type);
inline lua51_set_class_metatable_t lua51_set_class_metatable = nullptr;

inline Object* lua51_pushobject_inlined(luau_State* L, Object* obj)
{
	if (!obj)
	{
		L->outtop()->setType(LUAU_NIL);
		L->outtop()++;
		return nullptr;
	}
	auto handle = *reinterpret_cast<uint8_t**>(reinterpret_cast<uintptr_t>(obj) + 0x28);
	auto ud = lua51_newuserdata(L, 0x20);
	*reinterpret_cast<void**>(ud + 0x18) = handle;
	*reinterpret_cast<void**>(ud + 0x10) = ud + 0x18;
	++*reinterpret_cast<int32_t*>(handle + 8);
	auto comp = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(obj) + 0x10);
	auto type = (*reinterpret_cast<void*(***)(void*)>(comp))[0x40 / 8](comp);
	*reinterpret_cast<void**>(ud) = type;
	*reinterpret_cast<uint32_t*>(ud + 8) = 2;
	lua51_set_class_metatable(L, type);
	return obj;
}

[[nodiscard]] inline luau_TValue* lua51_index2adr(luau_State* L, int idx) noexcept
{
	if (idx == -10000)
	{
		return reinterpret_cast<luau_TValue*>(reinterpret_cast<uintptr_t>(L->global_state()) + 0xA0); // global_State::l_registry
	}
	if (idx == -10002)
	{
		return reinterpret_cast<luau_TValue*>(reinterpret_cast<uintptr_t>(L) + 0x78); // lua_State::l_gt
	}
	return L->getValue(idx);
}

inline luau_State* luau_L = nullptr;
//inline Object*** luau_obj_buf[4];
inline std::string luau_error_msg;

using wf_hash_t = uint32_t(*)(const char*);
inline wf_hash_t wf_hash; // owfScript::init

inline bool swig_names_are_strings = false; // before 2018.05.17.16.28 SWIG tables hold name pointers, not hashes

struct SwigNamedEntry
{
	uint32_t hash;

	[[nodiscard]] bool isEnd() const noexcept { return swig_names_are_strings ? !*reinterpret_cast<const char* const*>(this) : hash == 0; }
	[[nodiscard]] uint32_t getHash() const noexcept { return swig_names_are_strings ? wf_hash(*reinterpret_cast<const char* const*>(this)) : hash; }
};

struct SwigMethod : SwigNamedEntry
{
	luau_CFunction func;
};
#if SOUP_BITS == 64
static_assert(sizeof(SwigMethod) == 0x10);
#endif

struct SwigAttribute : SwigNamedEntry
{
	luau_CFunction getter;
	luau_CFunction setter;
};
#if SOUP_BITS == 64
static_assert(sizeof(SwigAttribute) == 0x18);
#endif

struct SwigTypeDesc
{
	union
	{
		/* 0x00 */ const char* name_str; // < U40, e.g. "Object"
		/* 0x00 */ uint64_t name_unk; // >= U40
	};
	PAD(0x08, 0x10) luau_CFunction ctor;
	PAD(0x18, 0x20) SwigMethod* methods;
	/* 0x28 */ SwigAttribute* attributes;
	PAD(0x30, 0x38) const char** parent_ptr_name; // e.g. "Object *"

	luau_CFunction findMethod(uint32_t hash)
	{
		for (auto method = this->methods; !method->isEnd(); ++method)
		{
			if (method->getHash() == hash)
			{
				return method->func;
			}
		}
		return nullptr;
	}

	luau_CFunction findGetter(uint32_t hash)
	{
		for (auto attr = this->attributes; !attr->isEnd(); ++attr)
		{
			if (attr->getHash() == hash)
			{
				return attr->getter;
			}
		}
		return nullptr;
	}

	luau_CFunction findSetter(uint32_t hash)
	{
		for (auto attr = this->attributes; !attr->isEnd(); ++attr)
		{
			if (attr->getHash() == hash)
			{
				return attr->setter;
			}
		}
		return nullptr;
	}
};
#if SOUP_BITS == 64
static_assert(sizeof(SwigTypeDesc) == 0x40);
#endif

struct SwigTypeField
{
	/* 0x00 */ const char* field_name; // e.g. "_p_LotusHudStatusTypes__FlashMarker"
	/* 0x08 */ const char* type_name; // e.g. "LotusHudStatusTypes::FlashMarker *"
	[[nodiscard]] SwigTypeDesc* type_desc() const noexcept
	{
		return *reinterpret_cast<SwigTypeDesc* const*>(reinterpret_cast<uintptr_t>(this) + (game_version >= GV(26, 0, 0) ? 0x18 : 0x20));
	}
};

inline std::unordered_map<uint32_t, SwigTypeDesc*> swig_types;
#if PRIVATE
inline std::vector<std::string> swig_type_names;
#endif

struct SwigEnum
{
	PAD(0, 0x08) const char* name;
	/* 0x10 */ int32_t value;
	PAD(0x14, 0x38);
};
#if SOUP_BITS == 64
static_assert(sizeof(SwigEnum) == 0x38);
#endif

inline std::vector<SwigEnum*> swig_enums1;

struct SwigEnumSelfAllocated
{
	const char* name;
	int32_t value;
};
inline std::vector<std::vector<SwigEnumSelfAllocated>> swig_enums2;