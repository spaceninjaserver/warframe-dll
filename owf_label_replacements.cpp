#include "owf_label_replacements.hpp"

#include <joaat.hpp>
#include <string.hpp>

#include <lauxlib.h>
#include <lstate.h>

#include "owf_repo.hpp"
#include "owf_scripting.hpp"

using namespace soup;

static lua_State* L;

void load_label_replacements()
{
	{
		size_t size;
		const char* data;
		{
			std::lock_guard lock(g_repo_mtx); // Cannot be locked during lua_pcall as it would make owf_repo_find fail because g_repo_mtx is not recursive.
			data = g_repo.find(soup::joaat::compileTimeHash("OpenWF/helpers/pre_load_label_replacements.pluto"), size);
		}
		auto L = luaL_newstate();
		owfScript::openLibs(L);
		if (luaL_loadbuffer(L, data, size, nullptr) != LUA_OK
			|| lua_pcall(L, 0, 0, 0) != LUA_OK
			)
		{
			owfScript::logNl(lua_type(L, -1) == LUA_TSTRING ? pluto_checkstring(L, -1) : "Non-string script error");
		}
		lua_close(L);
	}

	std::lock_guard lock(label_replacements_mtx);

	if (L)
	{
		lua_close(L);
	}

	L = luaL_newstate();
	owfScript::openLibs(L);

	if (luaL_loadfile(L, "OpenWF/Label Replacements.pluto") != LUA_OK
		|| lua_pcall(L, 0, 1, 0) != LUA_OK
		)
	{
		owfScript::logNl(lua_type(L, -1) == LUA_TSTRING ? pluto_checkstring(L, -1) : "Non-string script error");

		lua_close(L);
		L = nullptr;
	}
}

const char* do_label_replacements(const char* str_data, size_t str_size, const char* loctag_data, size_t loctag_size, size_t& out_size)
{
	for (const auto& [suffix, value] : { std::pair<std::string_view, std::string_view>{ "/Mobile/Cross_Save_Account", "about.openwf.io" }, { "/Mobile/Cross_Save_Link", "https://about.openwf.io/" } })
	{
		if (std::string_view(loctag_data, loctag_size).ends_with(suffix))
		{
			out_size = value.size();
			return value.data();
		}
	}

	const char* ret = nullptr;
	std::lock_guard lock(label_replacements_mtx);
	if (L)
	{
		// Stack now: func
		lua_pushvalue(L, -1);
		// Stack now: func, func
		lua_pushlstring(L, loctag_data, loctag_size);
		// Stack now: func, func, loctag
		lua_pushlstring(L, str_data, str_size);
		// Stack now: func, func, loctag, str
		lua_pcall(L, 2, 1, 0);
		// Stack now: func, res
		if (lua_type(L, -1) == LUA_TSTRING)
		{
			const char* data = lua_tolstring(L, -1, &out_size);
			if (out_size != str_size || memcmp(data, str_data, out_size) != 0)
			{
				if (out_size == loctag_size && memcmp(data, loctag_data, out_size) == 0)
				{
					out_size = -1; // Swap mark
				}
				else
				{
					ret = fossilise_string(data, out_size)->data;
				}
			}
		}
		lua_pop(L, 1);
		// Stack now: func
	}
	return ret;
}
