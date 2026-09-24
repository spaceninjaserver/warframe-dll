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

// U43 added a type at tag 5 shifting LUAU_STRING, up by 1
inline uint32_t luau_type_shift = 0;
[[nodiscard]] inline uint32_t luau_tt(luau_Type t) noexcept
{
	return static_cast<uint32_t>(t) + (t >= LUAU_STRING ? luau_type_shift : 0);
}

struct luau_TValue
{
	/* 0x00 */ luau_Value value;
	PAD(0x08, 0x0C) uint32_t type;

	[[nodiscard]] bool isType(luau_Type t) const noexcept { return type == luau_tt(t); }
	void setType(luau_Type t) noexcept { type = luau_tt(t); }

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
		return ***(Object****)(value.as_uintptr + 0x18);
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

#define luau_savestack(L, p) ((char*)(p) - (char*)L->stack)
#define luau_restorestack(L, n) ((luau_TValue*)((char*)L->stack + (n)))

struct luau_State
{
	PAD(0, 0x08) luau_TValue* outtop;
	/* 0x10 */ luau_TValue* intop;
	/* 0x18 */ luau_GlobalState* global_state;
	/* 0x20 */ luau_CallInfo* ci;
	/* 0x28 */ luau_TValue* stack_last;
	/* 0x30 */ luau_TValue* stack;
	PAD(0x38, 0x90);

	luau_TValue* getValue(int idx)
	{
		return idx < 0 ? &outtop[idx] : &intop[idx - 1];
	}
};
#if SOUP_BITS == 64
static_assert(sizeof(luau_State) == 0x90);
#endif

struct luau_Closure
{
	/* 0x00 */ luau_CommonHeader;
	/* 0x03 */ uint8_t isC;
	/* 0x04 */ uint8_t nupvalues;
	/* 0x05 */ uint8_t stacksize;
	/* 0x06 */ uint8_t preload;
	/* 0x08 */ luau_GCObject* gclist;
	/* 0x10 */ void/*LuaTable*/* env;
	union
	{
		struct
		{
			/* 0x18 */ luau_CFunction func;
			/* 0x20 */ luau_Continuation cont;
			/* 0x28 */ const char* debugname;
			/* 0x30 */ luau_TValue upvals[1];
		} c;
		struct
		{
			void/*Proto*/* p;
			luau_TValue uprefs[1];
		} l;
	};
};
#if SOUP_BITS == 64
static_assert(offsetof(luau_Closure, isC) == 0x03);
static_assert(offsetof(luau_Closure, c.func) == 0x18);
#endif

struct luau_UpVal
{
	luau_CommonHeader;
	uint8_t markedopen;
	luau_TValue* v;
};

union luau_GCObject
{
	luau_Closure cl;
	luau_UpVal uv;
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
	return L->outtop - L->intop;
}

inline bool luau_push_number(luau_State* luau_L, float value)
{
	SOUP_IF_LIKELY (luau_L->outtop != luau_L->stack_last)
	{
		luau_L->outtop->value.as_float = value;
		luau_L->outtop->type = luau_tt(LUAU_NUMBER);
		luau_L->outtop++;
		return true;
	}
	return false;
}

inline bool luau_push_lightuserdata(luau_State* luau_L, void* value)
{
	SOUP_IF_LIKELY (luau_L->outtop != luau_L->stack_last)
	{
		luau_L->outtop->value.as_uintptr = reinterpret_cast<uintptr_t>(value);
		luau_L->outtop->type = luau_tt(LUAU_LIGHTUSERDATA);
		luau_L->outtop++;
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

inline luau_State* luau_L = nullptr;
//inline Object*** luau_obj_buf[4];
inline std::string luau_error_msg;

struct SwigMethod
{
	uint32_t hash;
	luau_CFunction func;
};
#if SOUP_BITS == 64
static_assert(sizeof(SwigMethod) == 0x10);
#endif

struct SwigAttribute
{
	uint32_t hash;
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
		for (auto method = this->methods; method->hash != 0; ++method)
		{
			if (method->hash == hash)
			{
				return method->func;
			}
		}
		return nullptr;
	}

	luau_CFunction findGetter(uint32_t hash)
	{
		for (auto attr = this->attributes; attr->hash != 0; ++attr)
		{
			if (attr->hash == hash)
			{
				return attr->getter;
			}
		}
		return nullptr;
	}

	luau_CFunction findSetter(uint32_t hash)
	{
		for (auto attr = this->attributes; attr->hash != 0; ++attr)
		{
			if (attr->hash == hash)
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
	PAD(0x10, 0x18) SwigTypeDesc* type_desc;
};
#if SOUP_BITS == 64
static_assert(sizeof(SwigTypeField) == 0x20);
#endif

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


using wf_hash_t = uint32_t(*)(const char*);
inline wf_hash_t wf_hash; // owfScript::init
