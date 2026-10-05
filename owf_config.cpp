#include "owf_config.hpp"

#include <joaat.hpp>
#include <json.hpp>
#include <os.hpp>
#include <string.hpp>
#include <whirlpool.hpp>

#include "owf_repo.hpp"
#include "owf_structs.hpp" // game_version

using namespace soup;

[[nodiscard]] static bool is_valid_whirlpool_hex_digest(const std::string& str) noexcept
{
	if (str.size() != 128)
	{
		return false;
	}
	for (const auto c : str)
	{
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

bool owfConfig::isConsoleEnabled() noexcept
{
	return ee_log_in_console
		|| write_all_metadata_reads_to_console
		|| write_patched_metadata_reads_to_console
		|| client_http_logging
		|| keep_console_open
		;
}

void owfConfig::load()
{
	UniquePtr<JsonNode> config = json::decodeFile("OpenWF/Client Config.json");
	if (!config || !config->isObj())
	{
		config = soup::make_unique<JsonObject>();
	}

	if (auto it = config->reinterpretAsObj().findIt("fallback_language"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		fallback_language = it->second->reinterpretAsStr().value;
		string::lower(fallback_language);
	}
	else
	{
		if (std::filesystem::exists("Cache.Windows/B.Misc_en.cache"))
		{
#if !CONFIG_LOADED_ONLY_ONCE
			fallback_language.clear();
#endif
		}
		else if (std::filesystem::exists("Cache.Windows/B.Misc_zh.cache"))
		{
			fallback_language = "zh";
		}
		else if (std::filesystem::exists("Cache.Windows/B.Misc_ru.cache"))
		{
			fallback_language = "ru";
		}
	}

	if (auto it = config->reinterpretAsObj().findIt("fallback_languageVO"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		fallback_languageVO = it->second->reinterpretAsStr().value;
		string::lower(fallback_languageVO);
	}
	else
	{
#if !CONFIG_LOADED_ONLY_ONCE
		fallback_languageVO.clear();
#endif
	}

	if (auto it = config->reinterpretAsObj().findIt("fallback_graphicsDriver"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		fallback_graphicsDriver = it->second->reinterpretAsStr().value;
	}
	else
	{
		if (!std::filesystem::exists("Cache.Windows/B.ShaderDx11.cache")
			&& std::filesystem::exists("Cache.Windows/B.ShaderDx9.cache")
			)
		{
			fallback_graphicsDriver = "dx9";
		}
		else
		{
			fallback_graphicsDriver = "dx11";
		}
	}

	if (auto it = config->reinterpretAsObj().findIt("fallback_windowMode"); it != config->reinterpretAsObj().end())
	{
		if (it->second->isInt())
		{
			fallback_windowMode = it->second->reinterpretAsInt().value;
		}
		else
		{
			fallback_windowMode = -1;
		}
	}
	else
	{
#if !CONFIG_LOADED_ONLY_ONCE
		std::lock_guard lock(g_repo_mtx);
#endif
		fallback_windowMode = static_cast<int>(g_repo.getVersionedI64(joaat::compileTimeHash("OpenWF/vv/default_windowMode.json"), game_version));
	}

#if !CONFIG_LOADED_ONLY_ONCE
	fallback_cluster.clear();
#endif
	if (auto it = config->reinterpretAsObj().findIt("fallback_cluster"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		fallback_cluster = it->second->reinterpretAsStr().value;
	}
	if (fallback_cluster.empty())
	{
		fallback_cluster = "public";
	}

	if (auto it = config->reinterpretAsObj().findIt("language"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		language = it->second->reinterpretAsStr().value;
		string::lower(language);
	}
	else
	{
		language = fallback_language;
	}

	if (auto it = config->reinterpretAsObj().findIt("server_host"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		server_host = it->second->reinterpretAsStr().value;
	}
	else
	{
		server_host = "127.0.0.1";
	}

	if (auto it = config->reinterpretAsObj().findIt("http_port"); it != config->reinterpretAsObj().end() && it->second->isInt())
	{
		http_port = it->second->reinterpretAsInt().value;
	}
	else
	{
		http_port = 80;
	}

	if (auto it = config->reinterpretAsObj().findIt("https_port"); it != config->reinterpretAsObj().end() && it->second->isInt())
	{
		https_port = it->second->reinterpretAsInt().value;
	}
	else
	{
		https_port = 443;
	}

	if (auto it = config->reinterpretAsObj().findIt("high_damage_numbers_patch"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		high_damage_numbers_patch = it->second->reinterpretAsBool().value;
	}
	else
	{
		high_damage_numbers_patch = true;
	}

	if (auto it = config->reinterpretAsObj().findIt("skip_mission_start_timer"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		skip_mission_start_timer = it->second->reinterpretAsBool().value;
	}
	else
	{
		skip_mission_start_timer = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("disable_profanity_filter"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		disable_profanity_filter = it->second->reinterpretAsBool().value;
	}
	else
	{
		disable_profanity_filter = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("logout_on_request_failure"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		logout_on_request_failure = it->second->reinterpretAsBool().value;
	}
	else
	{
		logout_on_request_failure = true;
	}

	if (auto it = config->reinterpretAsObj().findIt("fov_override"); it != config->reinterpretAsObj().end())
	{
		if (it->second->isFloat())
		{
			fov_override = it->second->reinterpretAsFloat().value;
		}
		else if (it->second->isInt())
		{
			fov_override = it->second->reinterpretAsInt().value;
		}
		else
		{
			fov_override = 0.0f;
		}
	}
	else
	{
		fov_override = 0.0f;
	}

	if (auto it = config->reinterpretAsObj().findIt("simulacrum_blacklisted"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		simulacrum_blacklisted = it->second->reinterpretAsBool().value;
	}
	else
	{
		simulacrum_blacklisted = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("simulacrum_whitelisted"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		simulacrum_whitelisted = it->second->reinterpretAsBool().value;
	}
	else
	{
		simulacrum_whitelisted = true;
	}

	if (auto it = config->reinterpretAsObj().findIt("pause_always_stops_time"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		pause_always_stops_time = it->second->reinterpretAsBool().value;
	}
	else
	{
		pause_always_stops_time = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("disable_firewall_prompt"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		disable_firewall_prompt = it->second->reinterpretAsBool().value;
	}
	else
	{
		disable_firewall_prompt = (game_version < GV(29, 0, 0)); // Starting with U29, an empty NRS array will not cause a popup.
	}

	if (auto it = config->reinterpretAsObj().findIt("secure_connections"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		secure_connections = it->second->reinterpretAsBool().value;
	}
	else
	{
		secure_connections = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("autologin"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		autologin = it->second->reinterpretAsBool().value;
	}
	else
	{
		autologin = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("autologin_email"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		autologin_email = it->second->reinterpretAsStr().value;
		string::lower(autologin_email);
	}
	else
	{
#if !CONFIG_LOADED_ONLY_ONCE
		autologin_email.clear();
#endif
	}

	if (auto it = config->reinterpretAsObj().findIt("autologin_password"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		setAutologinPassword(it->second->reinterpretAsStr().value);
	}
	else
	{
#if !CONFIG_LOADED_ONLY_ONCE
		autologin_password.clear();
#endif
	}

	if (auto it = config->reinterpretAsObj().findIt("auto_start_scripts"); it != config->reinterpretAsObj().end() && it->second->isArr())
	{
		for (const auto& node : it->second->reinterpretAsArr().children)
		{
			if (node->isStr())
			{
				auto_start_scripts.emplace_back(node->reinterpretAsStr());
			}
		}
	}
	else
	{
		auto_start_scripts = { "samples/Chat Commands.pluto" };
	}

	if (auto it = config->reinterpretAsObj().findIt("forced_profile_dir"); it != config->reinterpretAsObj().end() && it->second->isStr())
	{
		forced_profile_dir = it->second->reinterpretAsStr().value;
		if (!forced_profile_dir.empty())
		{
			const auto path = std::filesystem::absolute(forced_profile_dir);
			std::filesystem::create_directories(path);
			forced_profile_dir = string::fixType(path.u8string());
		}
	}
	else
	{
#if !CONFIG_LOADED_ONLY_ONCE
		forced_profile_dir.clear();
#endif
	}

	if (auto it = config->reinterpretAsObj().findIt("ee_log_in_console"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		ee_log_in_console = it->second->reinterpretAsBool().value;
	}
	else
	{
		ee_log_in_console = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("alternative_loading"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		alternative_loading = it->second->reinterpretAsBool().value;
	}
	else
	{
		alternative_loading = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("save_all_metadata"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		save_all_metadata = it->second->reinterpretAsBool().value;
	}
	else
	{
		save_all_metadata = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("write_all_metadata_reads_to_console"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		write_all_metadata_reads_to_console = it->second->reinterpretAsBool().value;
	}
	else
	{
		write_all_metadata_reads_to_console = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("write_all_metadata_reads_to_ee_log"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		write_all_metadata_reads_to_ee_log = it->second->reinterpretAsBool().value;
	}
	else
	{
		write_all_metadata_reads_to_ee_log = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("write_patched_metadata_reads_to_console"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		write_patched_metadata_reads_to_console = it->second->reinterpretAsBool().value;
	}
	else
	{
		write_patched_metadata_reads_to_console = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("write_patched_metadata_reads_to_ee_log"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		write_patched_metadata_reads_to_ee_log = it->second->reinterpretAsBool().value;
	}
	else
	{
		write_patched_metadata_reads_to_ee_log = false;
	}

	if (auto it = config->reinterpretAsObj().findIt("client_http_logging"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		client_http_logging = it->second->reinterpretAsBool().value;
	}
	else
	{
		client_http_logging = PRIVATE;
	}

	if (auto it = config->reinterpretAsObj().findIt("client_http_port"); it != config->reinterpretAsObj().end() && it->second->isInt())
	{
		static_assert(CONFIG_LOADED_ONLY_ONCE);
		client_http_port = it->second->reinterpretAsInt().value;
	}
	else
	{
		client_http_port = 6155;
	}

	if (auto it = config->reinterpretAsObj().findIt("disable_overlay"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		disable_overlay = it->second->reinterpretAsBool().value;
	}
	else
	{
		disable_overlay = os::isWine();
	}

	if (auto it = config->reinterpretAsObj().findIt("overlay_compatibility_mode"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		overlay_compatibility_mode = it->second->reinterpretAsBool().value;
	}
	else
	{
		overlay_compatibility_mode = os::isWine();
	}

	if (auto it = config->reinterpretAsObj().findIt("keep_console_open"); it != config->reinterpretAsObj().end() && it->second->isBool())
	{
		keep_console_open = it->second->reinterpretAsBool().value;
	}
	else
	{
		keep_console_open = !have_scripting;
	}
}

void owfConfig::save()
{
	JsonObject config;

	config.add("fallback_language", fallback_language);
	config.add("fallback_languageVO", fallback_languageVO);
	config.add("fallback_graphicsDriver", fallback_graphicsDriver);
	config.add("fallback_windowMode", fallback_windowMode);
	config.add("fallback_cluster", fallback_cluster);

	config.add("language", language);

	config.add("server_host", server_host);
	config.add("http_port", http_port);
	config.add("https_port", https_port);
	config.add("secure_connections", secure_connections);
	config.add("autologin", autologin);
	config.add("autologin_email", autologin_email);
	config.add("autologin_password", autologin_password);

	config.add("high_damage_numbers_patch", high_damage_numbers_patch);
	config.add("simulacrum_blacklisted", simulacrum_blacklisted);
	config.add("simulacrum_whitelisted", simulacrum_whitelisted);
	config.add("pause_always_stops_time", pause_always_stops_time);
	config.add("disable_firewall_prompt", disable_firewall_prompt);

	config.add("ee_log_in_console", ee_log_in_console);
	config.add("skip_mission_start_timer", skip_mission_start_timer);
	config.add("disable_profanity_filter", disable_profanity_filter);
	config.add("logout_on_request_failure", logout_on_request_failure);
	config.add("fov_override", fov_override);
	config.add("forced_profile_dir", forced_profile_dir);
	{
		auto arr = soup::make_unique<JsonArray>();
		for (const auto& path : auto_start_scripts)
		{
			arr->children.emplace_back(soup::make_unique<JsonString>(path));
		}
		config.add("auto_start_scripts", std::move(arr));
	}
	config.add("alternative_loading", alternative_loading);
	config.add("save_all_metadata", save_all_metadata);
	config.add("write_all_metadata_reads_to_console", write_all_metadata_reads_to_console);
	config.add("write_all_metadata_reads_to_ee_log", write_all_metadata_reads_to_ee_log);
	config.add("write_patched_metadata_reads_to_console", write_patched_metadata_reads_to_console);
	config.add("write_patched_metadata_reads_to_ee_log", write_patched_metadata_reads_to_ee_log);
	config.add("client_http_logging", client_http_logging);
	config.add("client_http_port", client_http_port);
	config.add("disable_overlay", disable_overlay);
	config.add("overlay_compatibility_mode", overlay_compatibility_mode);
	config.add("keep_console_open", keep_console_open);

	string::toFile("OpenWF/Client Config.json", config.encodePretty());
}

void owfConfig::setAutologinPassword(std::string str)
{
	autologin_password = std::move(str);
	if (!autologin_password.empty() && !is_valid_whirlpool_hex_digest(autologin_password))
	{
		soup::whirlpool::State st;
		uint8_t hash[soup::whirlpool::DIGEST_BYTES];
		st.append(autologin_password.data(), autologin_password.size());
		st.finalise();
		st.getDigest(hash);
		autologin_password = string::bin2hexLower((const char*)hash, soup::whirlpool::DIGEST_BYTES);
	}
}
