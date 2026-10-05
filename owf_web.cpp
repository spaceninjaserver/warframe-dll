#include "owf_web.hpp"

#include <CertStore.hpp>
#include <filesystem.hpp>
#include <HttpRequestTask.hpp>
#include <JsonArray.hpp>
#include <JsonString.hpp>
#include <JsonObject.hpp>
#include <ServerWebService.hpp>
#include <Socket.hpp>
#include <Thread.hpp>
#include <unicode.hpp>
#include <urlenc.hpp>
#include <WebSocketMessage.hpp>
#include <X509Certchain.hpp>

#include "modules/ee-notation-parser/EeNotationParser.hpp"

#include "main.hpp"
#include "owf_config.hpp"
#include "owf_console.hpp"
#include "owf_hotkeys.hpp"
#include "owf_irc.hpp"
#include "owf_label_replacements.hpp"
#include "owf_metadata_patches.hpp"
#include "owf_overlay.hpp"
#include "owf_repo.hpp"
#include "owf_scripting.hpp"
#include "owf_structs.hpp"
#include "owf_tunables.hpp"

using namespace soup;

struct owfWebsocketTag
{
	static inline uint32_t last_id = 0;

	uint32_t id;
};

struct owfContentTask : public Task
{
	SharedPtr<Worker> s;
	HttpRequestTask hrt;

	owfContentTask(Socket& _s, HttpRequest&& hr)
		: s(Scheduler::get()->getShared(_s)), hrt(
			std::move(hr),
			secure_connections ? &Socket::certchain_validator_default : &Socket::certchain_validator_none // Technically, insecure connections are fine for content, but we want keep-alive connections.
		)
	{
		if (secure_connections)
		{
			hrt.require_ecdhe = true;
		}
		ServerWebService::setKeepAlive(_s, true);
	}

	void onTick()
	{
		if (hrt.tickUntilDone())
		{
#if LOGGING
			if (hrt.result.has_value())
			{
				conout << "owfContentTask: " << hrt.result->status_code << std::endl;
			}
			else
			{
				conout << "owfContentTask: " << hrt.getStatus() << std::endl;
			}
#endif
			if (hrt.result.has_value() && hrt.result->status_code == 200)
			{
				ServerWebService::sendContent(*static_cast<Socket*>(s.get()), std::move(*hrt.result));
			}
			else
			{
				if (!owfOverlay::isInited())
				{
					if (hrt.hr.path.find("/0/B.Cache.Windows_") != std::string::npos)
					{
						if (hrt.hr.path.substr(19, 2) == "xx")
						{
							auto msg = get_core_string("manfail");
							soup::string::replaceAll(msg, "|NAME|", hrt.hr.path.substr(11, 10));
							msg.append("\r\n");
							msg.append("\r\n").append(get_core_string("blame"));
							msg.append("\r\n").append(get_core_string("blamedl")); // The game download did not finish as expected.
							msg.append("\r\n").append(get_core_string("blameupd")); // The game ran without a client patch and partially updated itself.

							const auto msg_utf16 = soup::unicode::utf8_to_utf16(msg);
							const auto title_utf16 = soup::unicode::utf8_to_utf16(get_bootstrapper_title());
							MessageBoxW(0, msg_utf16.c_str(), title_utf16.c_str(), MB_OK | MB_ICONERROR);
						}
						else
						{
							auto msg = get_core_string("manfail");
							soup::string::replaceAll(msg, "|NAME|", hrt.hr.path.substr(11, 10));
							msg.append("\r\n");
							msg.append("\r\n").append(get_core_string("blame"));
							msg.append("\r\n").append(get_core_string("blamelcfg")); // This installation simply does not have this content. (Try another language?)
							msg.append("\r\n").append(get_core_string("blameupd")); // The game ran without a client patch and partially updated itself.

							const auto msg_utf16 = soup::unicode::utf8_to_utf16(msg);
							const auto title_utf16 = soup::unicode::utf8_to_utf16(get_bootstrapper_title());
							MessageBoxW(0, msg_utf16.c_str(), title_utf16.c_str(), MB_OK | MB_ICONERROR);
						}
						exit(1);
					}
					if (hrt.hr.path.find("/0/B.Cache.Dx") != std::string::npos)
					{
						auto msg = get_core_string("manfail");
						soup::string::replaceAll(msg, "|NAME|", hrt.hr.path.substr(11, 4));
						msg.append("\r\n");
						msg.append("\r\n").append(get_core_string("blame"));
						msg.append("\r\n").append(get_core_string("blamegcfg")); // This installation simply does not have this content. (Try another graphics driver?)
						msg.append("\r\n").append(get_core_string("blameupd")); // The game ran without a client patch and partially updated itself.

						const auto msg_utf16 = soup::unicode::utf8_to_utf16(msg);
						const auto title_utf16 = soup::unicode::utf8_to_utf16(get_bootstrapper_title());
						MessageBoxW(0, msg_utf16.c_str(), title_utf16.c_str(), MB_OK | MB_ICONERROR);

						exit(1);
					}
				}
				ServerWebService::send404(*static_cast<Socket*>(s.get()));
			}
			setWorkDone();
		}
	}
};

struct owfHttpReverseProxyTask : public Task
{
	SharedPtr<Worker> s;
	HttpRequestTask hrt;

	owfHttpReverseProxyTask(Socket& _s, HttpRequest&& hr)
		: s(Scheduler::get()->getShared(_s)), hrt(std::move(hr)/*, &Socket::certchain_validator_default */)
	{
		if (secure_connections)
		{
			hrt.require_ecdhe = true;
		}
		ServerWebService::setKeepAlive(_s, true);
	}

	void onTick()
	{
		if (hrt.tickUntilDone())
		{
			if (hrt.result.has_value())
			{
				ServerWebService::sendContent(*static_cast<Socket*>(s.get()), std::move(*hrt.result));
			}
			else
			{
				ServerWebService::sendContent(*static_cast<Socket*>(s.get()), "500 Internal Server Error", "HttpSendRequest failed\r\nStatus: " + hrt.getStatus());
			}
			setWorkDone();
		}
	}
};

void start_builtin_http_server()
{
	Thread thrd([](Capture&&)
	{
		ServerWebService srv([](soup::Socket& s, soup::HttpRequest&& req, soup::ServerWebService&)
		{
			if (client_http_logging)
			{
				conout << get_core_string("webonreq") << ": " << req.path << std::endl;
			}
			if (joaat::hash(req.path.substr(0, 8)) == joaat::compileTimeHash("/origin/"))
			{
				req.path.erase(0, 16);
			}
			if (req.path.size() > 2
				&& ((req.path[1] == '0' && req.path[2] == '/')
					|| (req.path[1] == '0' && req.path[2] == '_')
					|| (req.path[1] == '7' && req.path[2] == '/') // Dx11 (< U40)
					|| (req.path[1] == '8' && req.path[2] == '/') // Dx12 (< U40)
					|| (req.path[1] == '9' && req.path[2] == '/') // Dx11 (>= U40)
					|| (req.path[1] == 'A' && req.path[2] == '/') // Dx12 (>= U40)
					)
				)
			{
				// Try to locate file locally
				{
					if (auto data = string::fromFile("OpenWF/content" + req.path); !data.empty())
					{
						ServerWebService::sendText(s, std::move(data));
						return;
					}
				}

				// Continue in task to ask SNS
				HttpRequest hr(server_host + ":" + std::to_string(secure_connections ? https_port : http_port), req.path);
				hr.use_tls = secure_connections;
				hr.path_is_encoded = true;
				Scheduler::get()->add<owfContentTask>(s, std::move(hr));

				return;
			}
			auto arr = string::explode(req.path, '?');
			const auto route_hash = soup::joaat::hash(urlenc::decode(arr[0]));
			switch (route_hash)
			{
			case soup::joaat::compileTimeHash("/tls_proxy"):
				{
					std::string host = server_host;
					if (https_port != 443)
					{
						host.push_back(':');
						host.append(std::to_string(https_port));
					}
					req.setHeader("Host", std::move(host));
				}
				req.use_tls = true;
				req.path.erase(0, 11);
				Scheduler::get()->add<owfHttpReverseProxyTask>(s, std::move(req));
				break;

			case soup::joaat::compileTimeHash("/"):
				if (arr.size() > 1 && soup::joaat::hash(arr[1].substr(0, 5)) == soup::joaat::compileTimeHash("lang="))
				{
					webui_lang_code = arr[1].substr(5);
				}
				else
				{
					webui_lang_code = language;
				}
#if PRIVATE
				if (std::string html = string::fromFile("OpenWF/index.html"); !html.empty())
				{
					ServerWebService::sendHtml(s, html);
				}
				else
#endif
				{
					size_t size;
					const char* data = g_repo.find(soup::joaat::compileTimeHash("OpenWF/index.html"), size);
					ServerWebService::sendHtml(s, data, size);
				}
				break;

			case soup::joaat::compileTimeHash("/dict.js"):
				{
					JsonObject obj;
					auto dict = g_repo.getDict("webui", webui_lang_code);
					for (const auto& e : dict)
					{
						obj.add(std::move(e.first), std::move(e.second));
					}
					ServerWebService::sendData(s, "text/javascript;charset=utf-8", "dict=" + obj.encode());
				}
				break;

			case soup::joaat::compileTimeHash("/ping"):
				ServerWebService::sendText(s, "pong");
				break;

			case soup::joaat::compileTimeHash("/save_all_metadata"):
				if (arr.size() > 1)
				{
					save_all_metadata = (arr[1].size() == 4);
				}
				ServerWebService::sendText(s, std::to_string(save_all_metadata));
				break;

			case soup::joaat::compileTimeHash("/write_all_metadata_reads_to_console"):
				if (arr.size() > 1)
				{
					write_all_metadata_reads_to_console = (arr[1].size() == 4);
				}
				ServerWebService::sendText(s, std::to_string(write_all_metadata_reads_to_console));
				break;

			case soup::joaat::compileTimeHash("/write_all_metadata_reads_to_ee_log"):
				if (arr.size() > 1)
				{
					write_all_metadata_reads_to_ee_log = (arr[1].size() == 4);
				}
				ServerWebService::sendText(s, std::to_string(write_all_metadata_reads_to_ee_log));
				break;

			case soup::joaat::compileTimeHash("/write_patched_metadata_reads_to_console"):
				if (arr.size() > 1)
				{
					write_patched_metadata_reads_to_console = (arr[1].size() == 4);
				}
				ServerWebService::sendText(s, std::to_string(write_patched_metadata_reads_to_console));
				break;

			case soup::joaat::compileTimeHash("/write_patched_metadata_reads_to_ee_log"):
				if (arr.size() > 1)
				{
					write_patched_metadata_reads_to_ee_log = (arr[1].size() == 4);
				}
				ServerWebService::sendText(s, std::to_string(write_patched_metadata_reads_to_ee_log));
				break;

			case soup::joaat::compileTimeHash("/pause_always_stops_time"):
				ServerWebService::sendText(s, std::to_string(pause_always_stops_time));
				break;

			case soup::joaat::compileTimeHash("/server_host"):
				if (arr.size() > 1
					&& server_host != arr[1]
					)
				{
					do_logout();
					server_host = arr[1];
					on_got_server_host();
				}
				ServerWebService::sendText(s, server_host);
				break;

			/*case soup::joaat::compileTimeHash("/freecam"):
				if (regionmgr && !prohibit_freecam)
				{
					if (auto local_player = regionmgr->GetLocalPlayer())
					{
						local_player->controlling_camera = true;
						local_player->getAvatar()->followed_by_camera() = false;
					}
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/lockcam"):
				if (regionmgr && !prohibit_freecam)
				{
					if (auto local_player = regionmgr->GetLocalPlayer())
					{
						local_player->controlling_camera = false;
						local_player->getAvatar()->followed_by_camera() = false;
					}
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/gamecam"):
				if (regionmgr && !prohibit_freecam)
				{
					if (auto local_player = regionmgr->GetLocalPlayer())
					{
						local_player->controlling_camera = false;
						local_player->getAvatar()->followed_by_camera() = true;
					}
				}
				ServerWebService::sendText(s, {});
				break;*/

			case soup::joaat::compileTimeHash("/status"):
				{
					JsonObject obj;
					populate_full_status(obj);
					ServerWebService::sendData(s, "application/json", obj.encodePretty());
				}
				break;

			case soup::joaat::compileTimeHash("/toggle_console"):
				if (owfConsole::active)
				{
					owfConsole::deactivate();
				}
				else
				{
					owfConsole::activate();
					if (!ee_log_in_console || game_version >= GV(23, 10, 0))
					{
						owfConsole::setExclusiveOutput();
					}
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/scripts"):
				ServerWebService::sendData(s, "application/json", get_available_scripts().encodePretty());
				break;

			case soup::joaat::compileTimeHash("/start_script"):
				if (!prohibit_scripts)
				{
					start_script_from_file(urlenc::decode(arr.at(1)));
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/start_script_inline"):
				if (!prohibit_scripts)
				{
					start_script_from_string(urlenc::decode(arr.at(1)));
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/stop_bgscript"): // Undocumented
				if (bgscript)
				{
					bgscript->stop_requested = true;
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/start_bgscript"): // Undocumented
				if (!bgscript)
				{
					start_bgscript();
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/restart_bgscript"): // Undocumented
				restart_bgscript();
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/autostart_scripts"):
				{
					JsonArray arr;
					for (const auto& name : auto_start_scripts)
					{
						arr.children.emplace_back(soup::make_unique<JsonString>(name));
					}
					ServerWebService::sendData(s, "application/json", arr.encodePretty());
				}
				break;

			case soup::joaat::compileTimeHash("/add_autostart_script"):
				if (auto name = urlenc::decode(arr.at(1)); std::find(auto_start_scripts.begin(), auto_start_scripts.end(), name) == auto_start_scripts.end())
				{
					auto_start_scripts.emplace_back(std::move(name));
					owfConfig::save();
				}
				{
					JsonObject obj;
					populate_autostart_scripts(obj);
					owf_broadcast_message(obj.encode());
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/remove_autostart_script"):
				if (auto it = std::find(auto_start_scripts.begin(), auto_start_scripts.end(), urlenc::decode(arr.at(1))); it != auto_start_scripts.end())
				{
					auto_start_scripts.erase(it);
					owfConfig::save();
				}
				{
					JsonObject obj;
					populate_autostart_scripts(obj);
					owf_broadcast_message(obj.encode());
				}
				ServerWebService::sendText(s, {});
				break;

			case soup::joaat::compileTimeHash("/apply_hotfix"):
				{
					if (auto hotfix = string::fromFile("OpenWF/Hotfix.owf"); !hotfix.empty())
					{
						uint64_t timestamp;
						if (!owfRepo::readHotfixHeader(hotfix.data(), hotfix.size(), timestamp))
						{
							ServerWebService::sendText(s, "Failed to apply hotfix as it was made for a different DLL version");
							break;
						}
						if (timestamp == g_repo.timestamp)
						{
							ServerWebService::sendText(s, "No changes");
							break;
						}
						{
							std::lock_guard lock(g_repo_mtx);
							if (g_repo.hotfix) // Replacing one hotfix with another?
							{
								g_repo.loadBuiltinArchive();
							}
							g_repo.loadHotfix(hotfix.data(), hotfix.size());
						}
						ServerWebService::sendText(s, "Hotfix applied");
					}
					else
					{
						const auto prev_timestamp = g_repo.timestamp;
						{
							std::lock_guard lock(g_repo_mtx);
							g_repo.loadBuiltinArchive();
						}
						if (g_repo.timestamp == prev_timestamp)
						{
							ServerWebService::sendText(s, "No changes");
							break;
						}
						ServerWebService::sendText(s, "Reverting to pre-hotfix state");
					}

					{
						size_t size;
						auto data = g_repo.find(joaat::compileTimeHash("OpenWF/tunables.json"), size);
						std::lock_guard lock(g_client_tunables_mtx);
						g_client_tunables.loadMsgpack(data, size);
					}

					restart_bgscript();

					load_hotkeys();

					{
						size_t size = 0;
						auto data = g_repo.find(joaat::compileTimeHash("OpenWF/helpers/post_apply_hotfix.pluto"), size);
						if (size)
						{
							start_script_from_string(std::string(data, size));
						}
					}

					ServerWebService::sendText(s, {});
				}
				break;

#if PRIVATE
			case soup::joaat::compileTimeHash("/reload_tunables"): // Undocumented
				{
					size_t size;
					if (auto data = (const char*)filesystem::createFileMapping("OpenWF/tunables.json", size))
					{
						{
							std::lock_guard lock(g_client_tunables_mtx);
							g_client_tunables.load(data, size);
						}
						filesystem::destroyFileMapping(data, size);
					}
					else
					{
						size_t size;
						data = g_repo.find(joaat::compileTimeHash("OpenWF/tunables.json"), size);
						std::lock_guard lock(g_client_tunables_mtx);
						g_client_tunables.loadMsgpack(data, size);
					}
					ServerWebService::sendText(s, {});
				}
				break;
#endif

			case soup::joaat::compileTimeHash("/version"):
				ServerWebService::sendText(s, BOOTSTRAPPER_TITLE);
				break;

			case soup::joaat::compileTimeHash("/game_version"):
				{
					JsonObject obj;
					obj.add("build_version", std::string(build_version, 16));
					obj.add("build_hash", build_hash[0] ? std::string(build_hash, 22) : std::string());
					ServerWebService::sendData(s, "application/json", obj.encodePretty());
				}
				break;

			case soup::joaat::compileTimeHash("/memory"):
				{
					JsonObject obj;
					//obj.add("leaked", static_cast<int64_t>(leaked_memory.load()));
//#if LABEL_REPLACEMENTS
					obj.add("fossilised", static_cast<int64_t>(fossilised_memory.load()));
//#endif
					ServerWebService::sendData(s, "application/json", obj.encodePretty());
				}
				break;

#if METADATA_PATCHES && SOUP_BITS == 64
			case soup::joaat::compileTimeHash("/get_effective_metadata"):
				{
					std::lock_guard lock(metadata_patches_mtx);
					if (auto e = metadata_patches.find(joaat::hash(urlenc::decode(arr.at(1)))); e != metadata_patches.end())
					{
						if (e->second.applied)
						{
							ServerWebService::sendText(s, e->second.final_data);
						}
						else
						{
							ServerWebService::sendText(s, "patch not applied (yet)");
						}
					}
					else
					{
						ServerWebService::sendText(s, "no such patch");
					}
				}
				break;

			case soup::joaat::compileTimeHash("/get_effective_metadata_as_json"):
				{
					std::lock_guard lock(metadata_patches_mtx);
					if (auto e = metadata_patches.find(joaat::hash(urlenc::decode(arr.at(1)))); e != metadata_patches.end())
					{
						if (e->second.applied)
						{
							EeNotationParser par;
							auto json = par.parse(e->second.final_data);
							ServerWebService::sendData(s, "application/json", json->encodePretty());
						}
						else
						{
							ServerWebService::sendText(s, "patch not applied (yet)");
						}
					}
					else
					{
						ServerWebService::sendText(s, "no such patch");
					}
				}
				break;
#endif

			default:
				{
					// Try commands
					if (!req.path.empty())
					{
						if (JsonObject out; owf_command(urlenc::decode(req.path.begin() + 1, req.path.end()), out))
						{
							ServerWebService::sendData(s, "application/json", out.encodePretty());
							break;
						}
					}

					// Try script routes
					bool handled = false;
					std::lock_guard lock(running_scripts_mtx);
					for (auto& scr : running_scripts)
					{
						if (auto route = scr->findStaticCustomRoute(route_hash))
						{
							ServerWebService::sendData(s, route->mime.c_str(), route->content);
							scr->events.emplace_back(OWF_EVT_CUSTOM_ROUTE_SERVED, req.path);
							handled = true;
							break;
						}
						if (auto route = scr->handlesRouteDynamically(route_hash))
						{
							auto spTask = Scheduler::get()->add<owfScriptRouteTask>(s, scr->instance_id);
							scr->events.emplace_back(OWF_EVT_CUSTOM_ROUTE_REQUEST, reinterpret_cast<uint64_t>(spTask.toDumb()), req.path);
							handled = true;
							break;
						}
					}
					if (!handled && bgscript)
					{
						if (auto route = bgscript->findStaticCustomRoute(route_hash))
						{
							ServerWebService::sendData(s, route->mime.c_str(), route->content);
							bgscript->events.emplace_back(OWF_EVT_CUSTOM_ROUTE_SERVED, req.path);
							handled = true;
						}
						if (auto route = bgscript->handlesRouteDynamically(route_hash))
						{
							auto spTask = Scheduler::get()->add<owfScriptRouteTask>(s, bgscript->instance_id);
							bgscript->events.emplace_back(OWF_EVT_CUSTOM_ROUTE_REQUEST, reinterpret_cast<uint64_t>(spTask.toDumb()), req.path);
							handled = true;
						}
					}

					if (!handled)
					{
						ServerWebService::send404(s);
					}
				}
				break;
			}
		});
		srv.on_websocket_connection_established = [](Socket& s, const HttpRequest&, ServerWebService&)
		{
			s.custom_data.addStructToMap(owfWebsocketTag, owfWebsocketTag{ ++owfWebsocketTag::last_id });

			JsonObject obj;
			populate_full_status(obj);
			ServerWebService::wsSendText(s, obj.encode());
		};
		srv.on_websocket_message = [](WebSocketMessage& msg, Socket& s, ServerWebService&)
		{
			if (JsonObject out; owf_command(msg.data, out) && !out.empty())
			{
				ServerWebService::wsSendText(s, out.encode());
				return;
			}

			std::lock_guard lock(running_scripts_mtx);
			for (auto& scr : running_scripts)
			{
				if (scr->handlesWebsocketMessage(msg.data))
				{
					scr->events.emplace_back(OWF_EVT_WEBSOCKET_MESSAGE, s.custom_data.getStructFromMapConst(owfWebsocketTag).id, std::move(msg.data));
					return;
				}
			}
			if (bgscript)
			{
				if (auto route = bgscript->handlesWebsocketMessage(msg.data))
				{
					bgscript->events.emplace_back(OWF_EVT_WEBSOCKET_MESSAGE, s.custom_data.getStructFromMapConst(owfWebsocketTag).id, std::move(msg.data));
					return;
				}
			}
		};

		auto certstore = soup::make_shared<soup::CertStore>();
		{
			soup::X509Certchain certchain;
			{
				size_t size;
				const char* data = g_repo.find(soup::joaat::compileTimeHash("OpenWF/cert/cert.pem"), size);
				certchain.fromPem(std::string(data, size));
			}

			size_t size;
			const char* data = g_repo.find(soup::joaat::compileTimeHash("OpenWF/cert/key.pem"), size);
			auto private_key = soup::RsaPrivateKey::fromPem(std::string(data, size));

			certstore->add(std::move(certchain), std::move(private_key));
		}

		ServerService irc_srv([](Socket& s, ServerService&, Server&)
		{
#if LOGGING
			conout << "IRC connection from " << s.peer.toString() << std::endl;
#endif
			g_irc_downstream = Scheduler::get()->getShared(s);
			irc_downstream_recv(s);
			if (!g_irc_upstream)
			{
				Scheduler::get()->add<owfConnectToIrcTask>();
			}
		});
		g_irc_port = (game_version >= GV(15, 0, 0) ? g_serv.bindCrypto(0, &irc_srv, certstore) : g_serv.bind(0, &irc_srv));
#if LOGGING
		conout << "Bound IRC proxy on TCP/" << g_irc_port << std::endl;
#endif

		SOUP_IF_UNLIKELY (!g_serv.bindOptCrypto(client_http_port, &srv, std::move(certstore)))
		{
			conout << "Failed to bind TCP/";
			conout << client_http_port;
			conout << '.';
			if (game_version >= GV(33, 6, 0))
			{
				conout << " The game will fail to start.";
			}
			conout << std::endl;
		}

#if LOGGING
		g_serv.on_connection_lost = [](Socket& s, Scheduler&)
		{
			conout << "Lost connection: " << s.peer.toString() << std::endl;
		};
#endif

		g_serv.run();
		SOUP_ASSERT_UNREACHABLE;
	});
	thrd.detach();
}

struct owfBroadcastMessageTask final : public Task
{
	const std::string msg;
	const uint32_t recipient;

	owfBroadcastMessageTask(std::string&& msg, uint32_t recipient)
		: msg(std::move(msg)), recipient(recipient)
	{
	}

	void onTick() final
	{
		for (const auto& w : Scheduler::get()->workers)
		{
			if (w->type == soup::WORKER_TYPE_SOCKET
				&& static_cast<Socket*>(w.get())->custom_data.isStructInMap(owfWebsocketTag)
				&& (recipient == 0 || recipient == static_cast<Socket*>(w.get())->custom_data.getStructFromMapConst(owfWebsocketTag).id)
				)
			{
				ServerWebService::wsSendText(*static_cast<Socket*>(w.get()), msg);
			}
		}
		setWorkDone();
	}
};

void owf_broadcast_message(std::string&& msg, uint32_t recipient /*= 0*/)
{
	unicode::utf8_sanitise(msg);
	g_serv.add<owfBroadcastMessageTask>(std::move(msg), recipient);
}

owfScriptRouteTask::owfScriptRouteTask(soup::Socket& _s, size_t script_instance_id)
	: s(Scheduler::get()->getShared(_s)), script_instance_id(script_instance_id)
{
	ServerWebService::setKeepAlive(_s, true);
}

void owfScriptRouteTask::onTick() /*final*/
{
	if (auto response = this->response.load())
	{
		ServerWebService::sendData(*static_cast<Socket*>(s.get()), response->mime.c_str(), std::move(response->content));
		delete response;
		return setWorkDone();
	}
	if (static_cast<Socket*>(s.get())->isWorkDoneOrClosed())
	{
#if LOGGING
		conout << "owfScriptRouteTask: client socket is gone" << std::endl;
#endif
		return setWorkDone();
	}
	if (get_script_by_instance_id(script_instance_id) == nullptr)
	{
#if LOGGING
		conout << "owfScriptRouteTask: script instance is gone" << std::endl;
#endif
		ServerWebService::sendContent(*static_cast<Socket*>(s.get()), "500 Internal Server Error", "Sorry, this request was supposed to be handled by a script, but that script is no longer running now.");
		return setWorkDone();
	}
}
