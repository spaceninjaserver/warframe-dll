#pragma once

#include <cstdint>
#include <string>
#include <vector>


// Command line arguments
inline std::string fallback_language;
inline std::string fallback_languageVO;
inline std::string fallback_graphicsDriver;
inline int fallback_windowMode;
inline std::string fallback_cluster;

// General
inline std::string language;

// Server connection
inline std::string server_host;
inline uint16_t http_port;
inline uint16_t https_port;
inline bool secure_connections;
inline bool autologin;
inline std::string autologin_email;
inline std::string autologin_password;

// Patches
inline bool high_damage_numbers_patch;
inline bool simulacrum_blacklisted;
inline bool simulacrum_whitelisted;
inline bool pause_always_stops_time;
inline bool disable_firewall_prompt;

// Features
inline bool ee_log_in_console;
inline bool skip_mission_start_timer;
inline bool disable_profanity_filter;
inline bool logout_on_request_failure;
inline float fov_override;
inline std::string forced_profile_dir;
inline bool unencrypted_settings;
inline std::vector<std::string> auto_start_scripts;
inline bool alternative_loading;
inline bool save_all_metadata;
inline bool write_all_metadata_reads_to_console;
inline bool write_all_metadata_reads_to_ee_log;
inline bool write_patched_metadata_reads_to_console;
inline bool write_patched_metadata_reads_to_ee_log;
inline bool client_http_logging;
inline uint16_t client_http_port;
inline bool disable_overlay;
inline bool overlay_compatibility_mode;
inline bool keep_console_open;


// Effective arguments (not in Client Config.json)
inline std::string game_lang_code;
inline std::string webui_lang_code;

inline std::string auth_query; // e.g. "accountId=6633b81e9dba0b714f28ff02&nonce=8300464181160923&ct=MSI"
inline std::string dll_path_utf8;



#define CONFIG_LOADED_ONLY_ONCE true

struct owfConfig
{
	static bool isConsoleEnabled() noexcept;

	static void load();
	static void save();

	static void setAutologinPassword(std::string str);
};
