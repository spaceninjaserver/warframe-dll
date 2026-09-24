#pragma once

#include <string>

#include <fwd.hpp>

#define BOOTSTRAPPER_TITLE "OpenWF Bootstrapper v0.13.8"

// Cache tunables for faster access
inline bool prohibit_skip_mission_start_timer = false;
inline bool prohibit_disable_profanity_filter = false;
inline bool prohibit_fov_override = false;
inline bool prohibit_freecam = false;
inline bool prohibit_teleport = false;
inline bool prohibit_scripts = false;

extern std::string get_bootstrapper_title();
extern void owf_set_build_hash(const char build_hash[22]);
extern bool set_server_tunables(const char* data, size_t size, bool delta = false);
extern void set_udp_proxy_upstream(const std::string& addr);
extern bool owf_command(const std::string& in, soup::JsonObject& out);
extern void start_bgscript();
extern void restart_bgscript();
extern void do_logout();
extern void on_got_server_host();
extern void populate_full_status(soup::JsonObject& obj);
extern void populate_autostart_scripts(soup::JsonObject& obj);
extern void broadcast_running_scripts_locked();
