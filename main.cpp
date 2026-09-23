#include "main.hpp"

#define VERIFY_EXE_SIG false
#define VERIFY_DLL_CHECKSUM false
#define ASK_SERVER_FOR_TUNABLES true
#define DISABLE_XP_BASED_LEVEL_CAPPING true
#define PROVIDE_VERSION_INFO true
#define DISABLE_WSINTCHK true
#define MINIMAL_HOOKS false // does not disable hooks with their own macros (metadata patches, label replacements)

// LOGGING should be true when using this
#define VERBOSE_RNG false
#define VERBOSE_CRC32 false
#define VERBOSE_CRC32C false
#define VERBOSE_MD5 false
#define VERBOSE_SERPROPTXT false
#define VERBOSE_OODLE false // made for U39
#define VERBOSE_SENDCNXLESS false
#define VERBOSE_LZF false
#define VERBOSE_UNCOMPRESSPKT false
#define VERBOSE_PKTCHKSUM false // made for U10

// Writes all IRC traffic to EE.log
#define VERBOSE_IRC false

#include <mutex>

#include <CallsiteHook.hpp>
#include <CompactDetourHook.hpp>
#if VERBOSE_CRC32
#include <crc32.hpp>
#endif
#include <DetourHook.hpp>
#include <HttpRequest.hpp>
#include <HttpRequestTask.hpp>
#include <joaat.hpp>
#include <json.hpp>
#include <memGuard.hpp>
#include <Module.hpp>
#include <Mutex.hpp>
#include <ObfusString.hpp>
#include <os.hpp>
#include <Pattern.hpp>
#include <pattern_macros.hpp>
#include <Process.hpp>
#include <ReplacementHook.hpp>
#include <ResolveIpAddrTask.hpp>
#include <sha256.hpp>
#include <Socket.hpp>
#include <string.hpp>
#include <structing.hpp>
#include <Thread.hpp>
#include <unicode.hpp>
#include <Uri.hpp>
#include <urlenc.hpp>

#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW
#if VERIFY_EXE_SIG
#include <wintrust.h>
#include <softpub.h>
#endif
//#include <wininet.h>
//#pragma comment(lib, "wininet")
#include <Lmcons.h> // UNLEN
#pragma comment(lib, "Advapi32.lib") // GetUserNameW

#include <lauxlib.h>

#include "modules/ee-notation-parser/EeNotationParser.hpp"

#include "owf_config.hpp"
#include "owf_console.hpp"
#include "owf_hotkeys.hpp"
#include "owf_irc.hpp"
#include "owf_label_replacements.hpp"
#include "owf_luau.hpp"
#include "owf_metadata_patches.hpp"
#include "owf_overlay.hpp"
#include "owf_repo.hpp"
#include "owf_scripting.hpp"
#include "owf_structs.hpp"
#include "owf_tunables.hpp"
#include "owf_udp_proxy.hpp"

using namespace soup;

static bool disabled_xp_based_level_cap = false;
static bool did_auto_login = false;
static bool metadata_patches_in_use = false;

// Exports for Ordis' old Helper.dll:
// ??4CExampleExport@@QEAAAEAV0@$$QEAV0@@Z
// ??4CExampleExport@@QEAAAEAV0@AEBV0@@Z
class __declspec(dllexport) CExampleExport
{
};

static HMODULE og_dwmapi;
static FARPROC og_DwmGetCompositionTimingInfo;
extern "C" __declspec(dllexport) void DwmGetCompositionTimingInfo() { og_DwmGetCompositionTimingInfo(); }

static HMODULE og_wtsapi32;
static FARPROC og_WTSRegisterSessionNotification;
static FARPROC og_WTSUnRegisterSessionNotification;
static FARPROC og_WTSFreeMemory;
static FARPROC og_WTSQuerySessionInformationA;
static FARPROC og_WTSQuerySessionInformationW;
extern "C" __declspec(dllexport) void WTSRegisterSessionNotification() { og_WTSRegisterSessionNotification(); }
extern "C" __declspec(dllexport) void WTSUnRegisterSessionNotification() { og_WTSUnRegisterSessionNotification(); }
extern "C" __declspec(dllexport) void WTSFreeMemory() { og_WTSFreeMemory(); }
extern "C" __declspec(dllexport) void WTSQuerySessionInformationA() { og_WTSQuerySessionInformationA(); }
extern "C" __declspec(dllexport) void WTSQuerySessionInformationW() { og_WTSQuerySessionInformationW(); }

using GetFileVersionInfoA_t = BOOL(*)(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
//using GetFileInformationByHandle_t = BOOL(*)(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation);
using GetFileVersionInfoExA_t = BOOL(*)(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
using GetFileVersionInfoExW_t = BOOL(*)(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
using GetFileVersionInfoSizeA_t = DWORD(*)(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
using GetFileVersionInfoSizeExA_t = DWORD(*)(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle);
using GetFileVersionInfoSizeExW_t = DWORD(*)(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle);
using GetFileVersionInfoSizeW_t = DWORD(*)(LPCWSTR lptstrFilename, LPDWORD lpdwHandle);
using GetFileVersionInfoW_t = BOOL(*)(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
using VerFindFileA_t = DWORD(*)(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen, LPSTR szDestDir, PUINT puDestDirLen);
using VerFindFileW_t = DWORD(*)(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen, LPWSTR szDestDir, PUINT puDestDirLen);
using VerInstallFileA_t = DWORD(*)(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT puTmpFileLen);
using VerInstallFileW_t = DWORD(*)(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT puTmpFileLen);
using VerLanguageNameA_t = DWORD(*)(DWORD wLang, LPSTR szLang, DWORD cchLang);
using VerLanguageNameW_t = DWORD(*)(DWORD wLang, LPWSTR szLang, DWORD cchLang);
using VerQueryValueA_t = BOOL(*)(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen);
using VerQueryValueW_t = BOOL(*)(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen);
static HMODULE og_version;
static GetFileVersionInfoA_t og_GetFileVersionInfoA;
//static GetFileInformationByHandle_t og_GetFileInformationByHandle;
static GetFileVersionInfoExA_t og_GetFileVersionInfoExA;
static GetFileVersionInfoExW_t og_GetFileVersionInfoExW;
static GetFileVersionInfoSizeA_t og_GetFileVersionInfoSizeA;
static GetFileVersionInfoSizeExA_t og_GetFileVersionInfoSizeExA;
static GetFileVersionInfoSizeExW_t og_GetFileVersionInfoSizeExW;
static GetFileVersionInfoSizeW_t og_GetFileVersionInfoSizeW;
static GetFileVersionInfoW_t og_GetFileVersionInfoW;
static VerFindFileA_t og_VerFindFileA;
static VerFindFileW_t og_VerFindFileW;
static VerInstallFileA_t og_VerInstallFileA;
static VerInstallFileW_t og_VerInstallFileW;
static VerLanguageNameA_t og_VerLanguageNameA;
static VerLanguageNameW_t og_VerLanguageNameW;
static VerQueryValueA_t og_VerQueryValueA;
static VerQueryValueW_t og_VerQueryValueW;
#pragma clang diagnostic ignored "-Wdll-attribute-on-redeclaration"
extern "C" __declspec(dllexport) BOOL GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) { return og_GetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData); }
//extern "C" __declspec(dllexport) BOOL GetFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation) { return og_GetFileInformationByHandle(hFile, lpFileInformation); }
extern "C" __declspec(dllexport) BOOL GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) { return og_GetFileVersionInfoExA(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData); }
extern "C" __declspec(dllexport) BOOL GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) { return og_GetFileVersionInfoExW(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData); }
extern "C" __declspec(dllexport) DWORD GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) { return og_GetFileVersionInfoSizeA(lptstrFilename, lpdwHandle); }
extern "C" __declspec(dllexport) DWORD GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) { return og_GetFileVersionInfoSizeExA(dwFlags, lpwstrFilename, lpdwHandle); }
extern "C" __declspec(dllexport) DWORD GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) { return og_GetFileVersionInfoSizeExW(dwFlags, lpwstrFilename, lpdwHandle); }
extern "C" __declspec(dllexport) DWORD GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) { return og_GetFileVersionInfoSizeW(lptstrFilename, lpdwHandle); }
extern "C" __declspec(dllexport) BOOL GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) { return og_GetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData); }
extern "C" __declspec(dllexport) DWORD VerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen, LPSTR szDestDir, PUINT puDestDirLen) { return og_VerFindFileA(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen); }
extern "C" __declspec(dllexport) DWORD VerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen, LPWSTR szDestDir, PUINT puDestDirLen) { return og_VerFindFileW(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen); }
extern "C" __declspec(dllexport) DWORD VerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT puTmpFileLen) { return og_VerInstallFileA(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen); }
extern "C" __declspec(dllexport) DWORD VerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT puTmpFileLen) { return og_VerInstallFileW(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen); }
extern "C" __declspec(dllexport) DWORD VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) { return og_VerLanguageNameA(wLang, szLang, cchLang); }
extern "C" __declspec(dllexport) DWORD VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) { return og_VerLanguageNameW(wLang, szLang, cchLang); }
extern "C" __declspec(dllexport) BOOL VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen) { return og_VerQueryValueA(pBlock, lpSubBlock, lplpBuffer, puLen); }
extern "C" __declspec(dllexport) BOOL VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID *lplpBuffer, PUINT puLen) { return og_VerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen); }


std::string get_bootstrapper_title()
{
	auto title = ObfusString(BOOTSTRAPPER_TITLE).str();
	if (const auto hotfix = g_repo.hotfix)
	{
		title.append(ObfusString(" hotfix ").str());
		title.append(std::to_string(hotfix));
	}
	return title;
}


/*struct ParsedUrl
{
	char pad[16];
	char host[256];
};

static DetourHook parse_url_hook;

static bool parse_url_detour(const char* in, ParsedUrl* out)
{
#if LOGGING
	conout << "parse_url " << in << std::endl;
#endif
	//in = "https://" SERVER "/origin/CAFEBABE"; // SpaceNinjaServer expects this kind of path prefix
	if (reinterpret_cast<decltype(&parse_url_detour)>(parse_url_hook.original)(in, out))
	{
		//strcpy(out->host, SERVER);
		return true;
	}
	return false;
}*/


/*struct LegacyParsedUrl
{
	PAD(0, 0x30) const wchar_t* hostname;
};

static DetourHook legacy_parse_url_hook;

static bool legacy_parse_url_detour(LegacyParsedUrl* out, LegacyGameString* in)
{
	conout << "legacy_parse_url: " << in->getData() << std::endl;

	LegacyGameString buf;
	strcpy(buf.data, "http://localhost");
	in = &buf;

	auto ret = reinterpret_cast<decltype(&legacy_parse_url_detour)>(legacy_parse_url_hook.original)(out, in);
	if (out->hostname)
	{
		//conout << "hostname = " << unicode::utf16_to_utf8(std::wstring(out->hostname)) << std::endl;
	}
	return ret;
}*/


/*static CompactDetourHook internet_connect_hook;

static void internet_connect_detour(uintptr_t a1)
{
	ObfusString localhost("127.0.0.1");
	*reinterpret_cast<HINTERNET*>(a1 + 104) = InternetConnectA(
		*reinterpret_cast<HINTERNET*>(a1 + 96),
		localhost.c_str(),
		client_http_port,
		"",
		"",
		INTERNET_SERVICE_HTTP,
		0,
		0
	);
}*/


static DetourHook resolve_addr_hook;

static bool resolve_addr_detour(sockaddr* sa, void* a2, void* a3)
{
	if (sa->sa_family == AF_INET)
	{
#if LOGGING
		conout << "resolve_addr called with IPv4, port " << Endianness::toNative(network_u16_t(reinterpret_cast<sockaddr_in*>(sa)->sin_port)) << std::endl;
#endif
		reinterpret_cast<sockaddr_in*>(sa)->sin_addr.s_addr = SOUP_IPV4_NWE(127, 0, 0, 1);
		return reinterpret_cast<decltype(&resolve_addr_detour)>(resolve_addr_hook.original)(sa, a2, a3);
	}
	else if (sa->sa_family == AF_INET6)
	{
#if LOGGING
		conout << "resolve_addr called with IPv6, port " << Endianness::toNative(network_u16_t(reinterpret_cast<sockaddr_in6*>(sa)->sin6_port)) << std::endl;
#endif
	}
	else
	{
#if LOGGING
		conout << "resolve_addr called with unknown address family" << std::endl;
#endif
	}
	return false;
}


static DetourHook winhttp_connect_hook;

static void* winhttp_connect_detour(void* a1, void* a2, int protocol, const char* host_1, uint16_t port, const char* host_2, const char* host_3)
{
#if LOGGING
	conout << "winhttp_connect for " << host_1 << ", port " << port << std::endl;
	if (host_2 && *host_2)
	{
		conout << "host_2 = " << host_2 << std::endl;
	}
	if (host_3 && *host_3)
	{
		conout << "host_3 = " << host_3 << std::endl;
	}
#endif

	protocol = 1; // 1 = HTTP, 2 = HTTPS
	ObfusString localhost("127.0.0.1");
	host_1 = localhost.c_str();
	port = client_http_port;

	return reinterpret_cast<decltype(&winhttp_connect_detour)>(winhttp_connect_hook.original)(a1, a2, protocol, host_1, port, nullptr, nullptr);
}


static CompactDetourHook game_http_request_hook;
static unsigned int GameHttpRequest_body_offset;

enum RequestType : uint8_t
{
	RT_NOT_CLASSIFIED = 0,
	RT_LOGIN,
	RT_HUB,
};

static bool strip_tls;

static void process_game_http_request(soup::Uri& uri, const char*& body_data, size_t& body_size, std::string& body_buf, RequestType& rt)
{
	if (secure_connections)
	{
		uri.scheme = ObfusString("http").str();
		uri.host = ObfusString("127.0.0.1").str();
		uri.port = client_http_port;
	}
	else
	{
		uri.host = server_host;
		if (strip_tls)
		{
			uri.scheme = ObfusString("http").str();
			uri.port = http_port;
		}
		else
		{
			if (uri.scheme.size() == 4) // "http"
			{
				if (http_port != 80)
				{
					uri.port = http_port;
				}
			}
			else
			{
				if (https_port != 443)
				{
					uri.port = https_port;
				}
			}
		}
	}
	if (uri.path == ObfusString("/api/inventory.php").str() || uri.path == ObfusString("/api/missionInventoryUpdate.php").str())
	{
		if constexpr (DISABLE_XP_BASED_LEVEL_CAPPING)
		{
			if (disabled_xp_based_level_cap)
			{
				uri.query.append(ObfusString("&xpBasedLevelCapDisabled=1").str());
			}
		}
	}
	else if (uri.path == ObfusString("/api/login.php").str())
	{
		rt = RT_LOGIN;
		if (auto jr = json::decode(body_data, body_size); jr && jr->isObj())
		{
			if (autologin && !did_auto_login)
			{
				did_auto_login = true;
				if (auto it = jr->reinterpretAsObj().findIt(ObfusString("email").str()); it != jr->reinterpretAsObj().end() && it->second->isStr())
				{
					it->second->reinterpretAsStr().value = autologin_email;
				}
				if (auto it = jr->reinterpretAsObj().findIt(ObfusString("password").str()); it != jr->reinterpretAsObj().end() && it->second->isStr())
				{
					it->second->reinterpretAsStr().value = autologin_password;
				}
				body_buf = jr->encode();
				body_data = body_buf.data();
				body_size = body_buf.size();
			}
		}
		if constexpr (PROVIDE_VERSION_INFO)
		{
			if (build_version[0])
			{
				if (!uri.query.empty())
				{
					uri.query.push_back('&');
				}
				uri.query.append(ObfusString("buildLabel=").str());
				uri.query.append(build_version, 16);
				uri.query.push_back('/');
				if (build_hash[0])
				{
					uri.query.append(build_hash, 22);
				}
			}

			if (!uri.query.empty())
			{
				uri.query.push_back('&');
			}
			uri.query.append(ObfusString("clientMod=").str());
			uri.query.append(urlenc::encode(ObfusString(BOOTSTRAPPER_TITLE).str()));
			if (metadata_patches_in_use)
			{
				uri.query.append(ObfusString("&metadataPatchesInUse=1").str());
			}
		}
		{
			std::lock_guard lock(g_server_tunables_mtx);
			if (auto e = g_server_tunables.strings.find(soup::joaat::compileTimeHash("token")); e != g_server_tunables.strings.end())
			{
				uri.query.append(ObfusString("&token=").str());
				uri.query.append(e->second);
			}
		}
	}
	else if (uri.path == ObfusString("/api/inbox.php").str())
	{
		auth_query = uri.query;
	}
	else if (uri.path.find(ObfusString("/worldState.php").str()) != std::string::npos
		|| uri.path == ObfusString("/api/hubInstances").str()
		)
	{
		if constexpr (PROVIDE_VERSION_INFO)
		{
			if (build_version[0])
			{
				if (!uri.query.empty())
				{
					uri.query.push_back('&');
				}
				uri.query.append(ObfusString("buildLabel=").str());
				uri.query.append(build_version, 16);
				uri.query.push_back('/');
				if (build_hash[0])
				{
					uri.query.append(build_hash, 22);
				}
			}

			if (!uri.query.empty())
			{
				uri.query.push_back('&');
			}
			uri.query.append(ObfusString("clientMod=").str());
			uri.query.append(urlenc::encode(ObfusString(BOOTSTRAPPER_TITLE).str()));
		}
	}
	else if (uri.path == ObfusString("/api/hub").str())
	{
		rt = RT_HUB;

		if constexpr (PROVIDE_VERSION_INFO)
		{
			if (build_version[0])
			{
				if (!uri.query.empty())
				{
					uri.query.push_back('&');
				}
				uri.query.append(ObfusString("buildLabel=").str());
				uri.query.append(build_version, 16);
				uri.query.push_back('/');
				if (build_hash[0])
				{
					uri.query.append(build_hash, 22);
				}
			}

			if (!uri.query.empty())
			{
				uri.query.push_back('&');
			}
			uri.query.append(ObfusString("clientMod=").str());
			uri.query.append(urlenc::encode(ObfusString(BOOTSTRAPPER_TITLE).str()));
		}
	}
	else if (uri.path == ObfusString("/api/logout.php").str())
	{
		owfOverlay::onLoggedOut();
		auth_query.clear();
	}
	if constexpr (true) // PS can be relatively sensitive data but is often shared alongside server logs.
	{
		if (auto jr = json::decode(body_data, body_size); jr && jr->isObj())
		{
			auto it = jr->reinterpretAsObj().findIt(ObfusString("PS").str());
			if (it == jr->reinterpretAsObj().end())
			{
				it = jr->reinterpretAsObj().findIt(ObfusString("processes").str());
			}
			if (it != jr->reinterpretAsObj().end() && it->second->isStr())
			{
				ObfusString msg("W0RFXVN0ZXZlIGxpa2VzIGJpZyBidXR0cw");
				if (auto sep = it->second->reinterpretAsStr().value.find(';'); sep != std::string::npos && it->second->reinterpretAsStr().value.c_str()[0] == '0') // If PS indicates an anti-cheat detection it will look like "0x1;..." so keep the prefix.
				{
					it->second->reinterpretAsStr().value.erase(sep + 1);
					it->second->reinterpretAsStr().value.append(msg.str());
				}
				else
				{
					it->second->reinterpretAsStr().value = std::move(msg.str());
				}
				body_buf = jr->encode();
				body_data = body_buf.data();
				body_size = body_buf.size();
			}
		}
	}
	if (secure_connections)
	{
		uri.path = ObfusString("/tls_proxy?").str() + uri.path;
	}
}

static std::string process_login_response(const char* data, size_t size)
{
	if (auto jr = json::decode(data, size); jr && jr->isObj())
	{
		const auto pjId = jr->reinterpretAsObj().find(ObfusString("id").str());
		const auto pjNonce = jr->reinterpretAsObj().find(ObfusString("Nonce").str());
		if (pjId && pjNonce && pjId->isStr() && pjNonce->isInt())
		{
			auth_query = ObfusString("accountId=").str() + pjId->reinterpretAsStr().value + ObfusString("&nonce=").str() + std::to_string(pjNonce->reinterpretAsInt().value);
#if LOGGING
			conout << "Constructed auth_query from login response: " << auth_query << std::endl;
#endif
			if (secure_connections)
			{
				const auto pjIRC = jr->reinterpretAsObj().find(ObfusString("IRC").str());
				if (pjIRC && pjIRC->isArr() && pjIRC->reinterpretAsArr().children.size() == 1 && pjIRC->reinterpretAsArr().children[0]->isStr())
				{
					g_irc_upstream_host = std::move(pjIRC->reinterpretAsArr().children[0]->reinterpretAsStr().value);
					pjIRC->reinterpretAsArr().children[0]->reinterpretAsStr().value = ObfusString("127.0.0.1:").str();
					pjIRC->reinterpretAsArr().children[0]->reinterpretAsStr().value.append(std::to_string(g_irc_port));
					return jr->encode();
				}
			}
		}
	}
	return {};
}

using string_resize_t = void(*)(GameString*, size_t);
static string_resize_t string_resize = nullptr;

template <typename Str>
static void replace_game_string(Str& str, const std::string& replacement)
{
	if ((replacement.size() + 1) <= str.getSize())
	{
		memcpy(str.getData(), replacement.c_str(), replacement.size() + 1);
		str.shrink(replacement.size());
		return;
	}

	if constexpr (std::is_same_v<Str, GameString>)
	{
		if (string_resize)
		{
			string_resize(&str, replacement.size());
			memcpy(str.getData(), replacement.data(), replacement.size());
			return;
		}
	}

	// Gotta grow the string but don't have string_resize...
	std::lock_guard lock(label_replacements_mtx);
	auto ps = fossilise_string(replacement.data(), replacement.size());
	str.setUnownedData(ps->data, ps->size);
}

template <typename Str>
static void* game_http_request_detour(void* a1, uintptr_t request, void* a3)
{
	Str& request_url = *reinterpret_cast<Str*>(request + 0x00);
	Str& request_body = *reinterpret_cast<Str*>(request + GameHttpRequest_body_offset);

#if LOGGING
	conout << "game_http_request for " << (const char*)request_url.getData() << std::endl;
	/*if (request_body.getSize() != 0)
	{
		conout << request_body.getData() << std::endl;
	}*/
#endif

	Uri uri((const char*)request_url.getData());
	const char* body_data = request_body.getData();
	size_t body_size = request_body.getSize();
	std::string body_buf;
	RequestType rt = RT_NOT_CLASSIFIED;
	process_game_http_request(uri, body_data, body_size, body_buf, rt);
	std::string url_buf = uri.toString();
	request_url.setUnownedData(url_buf.data(), url_buf.size());
	if (body_data != request_body.getData())
	{
		request_body.setUnownedData(body_data, body_size);
	}

	const auto ret = reinterpret_cast<decltype(&game_http_request_detour<Str>)>(game_http_request_hook.original)(a1, request, a3);

#if LOGGING
	// This now contains the response
	/*if (request_body.getSize() != 0)
	{
		conout << request_body.getData() << std::endl;
	}*/
#endif

	switch (rt)
	{
	case RT_NOT_CLASSIFIED:
		break;

	case RT_LOGIN:
#if LOGGING
		//conout << "login response: " << std::string(request_body.getData(), request_body.getSize()) << std::endl;
#endif
		for (size_t i = 0; i != request_body.getSize(); ++i)
		{
			if (request_body.getData()[i] == '\t')
			{
				set_server_tunables(request_body.getData() + (i + 1), request_body.getSize() - (i + 1));
				request_body.getData()[i] = '\0';
				request_body.shrink(i);
				break;
			}
		}
		if (auto replacement = process_login_response(request_body.getData(), request_body.getSize()); !replacement.empty())
		{
			//conout << "login response replacement: " << replacement << std::endl;
			replace_game_string(request_body, replacement);
		}
		break;

	case RT_HUB:
#if LOGGING
		//conout << "hub response: " << std::string(request_body.getData(), request_body.getSize()) << std::endl;
#endif
		if (
			ObfusString prefix("\"udp_proxy_upstream ");
				request_body.getSize() > prefix.size()
				&& memcmp(request_body.getData(), prefix.data(), prefix.size()) == 0
			)
		{
			for (size_t i = prefix.size(); i != request_body.getSize(); ++i)
			{
				if (request_body.getData()[i] == ' ' || request_body.getData()[i] == '\"')
				{
					set_udp_proxy_upstream(std::string(&request_body.getData()[prefix.size()], i - prefix.size()));

					std::string replacement = ObfusString("\"hub 127.0.0.1:6951").str();
					replacement.append(&request_body.getData()[i], request_body.getSize() - i);
					//conout << "hub response replacement: " << replacement << std::endl;
					replace_game_string(request_body, replacement);

					break;
				}
			}
		}
		break;
	}

	return ret;	
}


#if SOUP_BITS == 64
static DetourHook encstr_append_hook;
static EncryptedString::AppendData* last_enc_str = nullptr;
static std::string dec_buf;

static void encstr_append_detour(EncryptedString::AppendData* a1, int a2)
{
	//conout << "encstr_append: " << (void*)a1 << ", " << std::string(a1->data, a1->size) << std::endl;
	if (last_enc_str != a1)
	{
		last_enc_str = a1;
		dec_buf.clear();
	}
	dec_buf.append(a1->data, a1->size);
	return reinterpret_cast<decltype(&encstr_append_detour)>(encstr_append_hook.original)(a1, a2);
}

static ReplacementHook encstr_discharge_hook;

static void encstr_discharge_detour(EncryptedString* a1, GameString* out)
{
	//conout << "encstr_discharge: " << (void*)a1->app << std::endl;
	replace_game_string(*out, dec_buf);

	dec_buf.clear();
}
#endif


/*static DetourHook queue_http_request_internal_hook;

// called multiple times if flags=6
static void queue_http_request_internal_detour(void* a1, GameString* url, GameString* body, const char* encoding, void* callback_data, int flags)
{
	conout << "queue_http_request_internal: url=" << url->getData() << ", encoding=" << (encoding ? encoding : "NULL") << std::endl;
	if (encoding)
	{
		conout << "Decrypted body: " << dec_buf << std::endl;
	}
	reinterpret_cast<decltype(&queue_http_request_internal_detour)>(queue_http_request_internal_hook.original)(a1, url, body, encoding, callback_data, flags);
	dec_buf.clear();
}*/


#if PRIVATE || MINIMAL_HOOKS
static DetourHook Curl_resolv_hook;

static void* Curl_resolv_detour(void* a1, const char* hostname, int port, bool allowDOH, void* a5)
{
#if LOGGING
	conout << "Curl_resolv for " << hostname << ", port " << port << std::endl;
#endif

	const auto localhost = ObfusString("127.0.0.1").str();

	const std::string& expected_hostname = secure_connections ? localhost : server_host;

#if !MINIMAL_HOOKS
	if (expected_hostname != hostname)
	{
		MessageBoxA(0, "HOSTNAME MISMATCH", "HOSTNAME MISMATCH", 0);
	}
#endif

	return reinterpret_cast<decltype(&Curl_resolv_detour)>(Curl_resolv_hook.original)(a1, expected_hostname.c_str(), port, allowDOH, a5);
}
#endif


static ReplacementHook ssl_verify_internal_hook;

static int ssl_verify_internal_detour(void* a1, void* a2)
{
	//conout << "ssl_verify_internal called" << std::endl;
	/*auto ret = reinterpret_cast<decltype(&ssl_verify_internal_detour)>(ssl_verify_internal_hook.original)(a1, a2);
	conout << "ssl_verify_internal returned " << ret << std::endl;*/
	return 1; // "Verify success"
}


static ReplacementHook Curl_ossl_verifyhost_hook;
static volatile const uint8_t expected_dll_sha256[0x20] = { 0x6D, 0xF7, 0xA9, 0x52, 0x20, 0x76, 0x2E, 0x5F, 0xF9, 0x75, 0x01, 0xAA, 0x67, 0x37, 0x7F, 0xCA, 0x3C, 0x1B, 0x2C, 0xA0, 0x24, 0x73, 0xFB, 0xEA, 0xF4, 0xBD, 0xEF, 0x8B, 0x43, 0xDF, 0x1E, 0xAE };

static int verify_dll_integrity()
{
#if PRIVATE || !VERIFY_DLL_CHECKSUM
	return 0;
#else
	auto dll = string::fromFile(dll_path_utf8);
	string::replaceAll(dll, std::string((const char*)expected_dll_sha256, sizeof(expected_dll_sha256)), {});
	uint8_t actual_dll_sha256[0x20];
	{
		soup::sha256::State st;
		st.append(dll.data(), dll.size());
		st.finalise();
		st.getDigest(actual_dll_sha256);
	}
	//conout << "expected_dll_sha256 = " << string::bin2hex((const char*)expected_dll_sha256, sizeof(expected_dll_sha256)) << std::endl;
	//conout << "actual_dll_sha256 = " << string::bin2hex((const char*)actual_dll_sha256, sizeof(actual_dll_sha256)) << std::endl;
	return memcmp(actual_dll_sha256, (const void*)expected_dll_sha256, 0x20);
#endif
}

static int Curl_ossl_verifyhost_detour(void* a1, void* a2)
{
	//conout << "Curl_ossl_verifyhost_detour called" << std::endl;
	/*auto ret = reinterpret_cast<decltype(&Curl_ossl_verifyhost_detour)>(Curl_ossl_verifyhost_hook.original)(a1, a2);
	conout << "Curl_ossl_verifyhost returned " << ret << std::endl;*/
	static auto res = verify_dll_integrity();
	return res; // we want 0 here
}


static CompactDetourHook verify_worldstate_integrity_hook;
#if VERIFY_EXE_SIG
static bool exe_signed = true;
#endif

static bool verify_worldstate_integrity_detour(void* outStr, void* inStr)
{
	reinterpret_cast<decltype(&verify_worldstate_integrity_detour)>(verify_worldstate_integrity_hook.original)(outStr, inStr);
#if VERIFY_EXE_SIG
	return exe_signed;
#else
	return true;
#endif
}


/*static DetourHook int_rsa_verify_hook;

static int64_t int_rsa_verify_detour(void* a1, void* a2, void* a3, void* a4, size_t* a5, void* a6, void* a7, void* a8)
{
	//auto ret = reinterpret_cast<decltype(&int_rsa_verify_detour)>(int_rsa_verify_hook.original)(a1, a2, a3, a4, a5, a6, a7, a8);
	//conout << "int_rsa_verify returns " << ret << std::endl;
	if (a5)
	{
		*a5 = 1; // For > 0 return from pkey_rsa_verifyrecover
	}
	return 1;
}*/


struct owfResolveUdpProxyUpstreamAddressTask : public Task
{
	ResolveIpAddrTask resolve_task;
	native_u16_t port;

	owfResolveUdpProxyUpstreamAddressTask(std::string name, native_u16_t port)
		: resolve_task(std::move(name)), port(port)
	{
	}

	void onTick() final
	{
		if (resolve_task.tickUntilDone())
		{
			if (resolve_task.result.has_value())
			{
				SocketAddr newAddr(*resolve_task.result, port);
#if LOGGING
				//conout << "Resolved udp_proxy_upstream to " << newAddr.toString() << std::endl;
#endif
				owfUdpProxy::setUpstreamAddr(newAddr);
			}
			return setWorkDone();
		}
	}
};

static void populate_server_prohibitions_locked(JsonObject& obj)
{
	auto arr = soup::make_unique<JsonArray>();
	if (prohibit_skip_mission_start_timer) { arr->children.emplace_back(soup::make_unique<JsonString>(ObfusString("prohibit_skip_mission_start_timer").str())); }
	if (prohibit_disable_profanity_filter) { arr->children.emplace_back(soup::make_unique<JsonString>(ObfusString("prohibit_disable_profanity_filter").str())); }
	if (prohibit_fov_override) { arr->children.emplace_back(soup::make_unique<JsonString>(ObfusString("prohibit_fov_override").str())); }
	if (prohibit_freecam) { arr->children.emplace_back(soup::make_unique<JsonString>(ObfusString("prohibit_freecam").str())); }
	if (prohibit_teleport) { arr->children.emplace_back(soup::make_unique<JsonString>(ObfusString("prohibit_teleport").str())); }
	if (prohibit_scripts) { arr->children.emplace_back(soup::make_unique<JsonString>(ObfusString("prohibit_scripts").str())); }
	obj.add(ObfusString("prohibitions"), std::move(arr));
}

bool set_server_tunables(const char* data, size_t size, bool delta)
{
#if LOGGING
	conout << "set_server_tunables: " << std::string(data, size) << std::endl;
#endif

	std::lock_guard lock(g_server_tunables_mtx);
	bool ok = g_server_tunables.load(data, size, delta);

	prohibit_skip_mission_start_timer = g_server_tunables.getBool(joaat::compileTimeHash("prohibit_skip_mission_start_timer"));
	prohibit_disable_profanity_filter = g_server_tunables.getBool(joaat::compileTimeHash("prohibit_disable_profanity_filter"));
	prohibit_fov_override = g_server_tunables.getBool(joaat::compileTimeHash("prohibit_fov_override"));
	prohibit_freecam = g_server_tunables.getBool(joaat::compileTimeHash("prohibit_freecam"));
	prohibit_teleport = g_server_tunables.getBool(joaat::compileTimeHash("prohibit_teleport"));
	prohibit_scripts = g_server_tunables.getBool(joaat::compileTimeHash("prohibit_scripts"));

	if (auto e = g_server_tunables.strings.find(soup::joaat::compileTimeHash("udp_proxy_upstream")); e != g_server_tunables.strings.end())
	{
		set_udp_proxy_upstream(e->second);
	}

	if (auto e = g_server_tunables.strings.find(soup::joaat::compileTimeHash("irc")); e != g_server_tunables.strings.end())
	{
		g_irc_upstream_host = e->second;
	}

	{
		JsonObject obj;
		populate_server_prohibitions_locked(obj);
		owf_broadcast_message(obj.encode());
	}

	return ok;
}

void set_udp_proxy_upstream(const std::string& addr)
{
	if (SocketAddr newAddr; newAddr.fromString(addr) && !newAddr.ip.isZero())
	{
		owfUdpProxy::setUpstreamAddr(newAddr);
	}
	else
	{
#if LOGGING
		conout << "Got some garbage for udp_proxy_upstream: " << addr << std::endl;
#endif
		if (const size_t sep = addr.find_last_of(':'); sep != std::string::npos)
		{
			if (const auto opt = string::toIntOpt<uint16_t>(addr.substr(sep + 1), string::TI_FULL); opt.has_value())
			{
				g_serv.add<owfResolveUdpProxyUpstreamAddressTask>(addr.substr(0, sep), native_u16_t(*opt));
			}
		}
	}
}

#if ASK_SERVER_FOR_TUNABLES
struct owfTunablesTask : public soup::Task
{
	HttpRequestTask hrt;

	owfTunablesTask()
		: hrt(
			HttpRequest(
				server_host + ":" + std::to_string(strip_tls ? http_port : https_port),
				ObfusString("/custom/tunables.json?clientMod=" BOOTSTRAPPER_TITLE "&buildVersion=").str() + std::string(build_version, 16)
			),
			secure_connections ? &Socket::certchain_validator_default : &Socket::certchain_validator_none
		)
	{
		hrt.hr.use_tls = !strip_tls;
		if (secure_connections)
		{
			hrt.require_ecdhe = true;
		}
	}

	void onTick() final
	{
		if (hrt.tickUntilDone())
		{
			bool ok = false;
			if (hrt.result)
			{
				if (hrt.result->status_code == 200)
				{
					ok = set_server_tunables(hrt.result->body.data(), hrt.result->body.size());
				}
			}

			if (!ok)
			{
				auto msg = get_core_string(ObfusString("tunafail").str());
				soup::string::replaceAll(msg, ObfusString("|HOST|").str(), hrt.hr.getHost());
				conout << std::move(msg) << std::endl;
			}

			owfOverlay::onTunablesRequestFinished(ok);

			setWorkDone();
		}
	}
};
#endif

void on_got_server_host()
{
	string::lower(server_host);
	if (server_host.find(ObfusString("warframe.com").str()) != std::string::npos)
	{
		server_host = ObfusString("127.0.0.1").str();
	}

	{
		auto msg = get_core_string(ObfusString("gotsh").str());
		soup::string::replaceAll(msg, ObfusString("|HOST|").str(), server_host);
		conout << msg << std::endl;
	}
	if (autologin && !did_auto_login)
	{
		conout << get_core_string(ObfusString("alpend")) << std::endl;
	}

#if ASK_SERVER_FOR_TUNABLES
	g_serv.add<owfTunablesTask>();
#endif
}

void do_logout()
{
	if (!auth_query.empty())
	{
		HttpRequest hr(server_host + ":" + std::to_string(https_port), ObfusString("/api/logout.php?").str() + auth_query);
		hr.use_tls = true;
		SOUP_UNUSED(hr.execute(secure_connections ? &Socket::certchain_validator_default : &Socket::certchain_validator_none));
		auth_query.clear();

		owfOverlay::onLoggedOut();
	}
}


static DetourHook parse_arguments_hook;
static bool processed_args = false;
static uint64_t* device_id_ptr = nullptr;

static std::string process_args_str(const char* str)
{
	std::string arguments_to_inject;
	if (!processed_args)
	{
		processed_args = true;
		bool got_language = false;
		bool got_languageVO = false;
		bool got_graphicsDriver = false;
		bool got_windowMode = false;
		bool got_cluster = false;
		for (const auto& arg : string::explode<std::string>(str, ' '))
		{
			if (arg.size() > 10 && arg.substr(0, 10) == ObfusString("-language:").str())
			{
				got_language = true;
			}
			else if (arg.size() > 12 && arg.substr(0, 12) == ObfusString("-languageVO:").str())
			{
				got_languageVO = true;
			}
			else if (arg.size() > 16 && arg.substr(0, 16) == ObfusString("-graphicsDriver:").str())
			{
				got_graphicsDriver = true;
			}
			else if (arg.size() > 12 && (arg.substr(0, 12) == ObfusString("-windowMode:").str() || arg.substr(0, 12) == ObfusString("-fullscreen:").str()))
			{
				got_windowMode = true;
			}
			else if (arg.size() > 9 && arg.substr(0, 9) == ObfusString("-cluster:").str())
			{
				got_cluster = true;
			}
		}

		if (!got_language && !fallback_language.empty())
		{
			arguments_to_inject.append(ObfusString("-language:").str());
			arguments_to_inject.append(fallback_language);
			arguments_to_inject.push_back(' ');
		}
		if (game_version >= GV(39, 0, 0) && !got_languageVO && !fallback_languageVO.empty())
		{
			arguments_to_inject.append(ObfusString("-languageVO:").str());
			arguments_to_inject.append(fallback_languageVO);
			arguments_to_inject.push_back(' ');
		}
		if (!got_graphicsDriver && !fallback_graphicsDriver.empty())
		{
			if (game_version >= GV(28, 0, 0))
			{
				arguments_to_inject.append(ObfusString("-graphicsDriver:").str());
				arguments_to_inject.append(fallback_graphicsDriver);
				arguments_to_inject.push_back(' ');
			}
			else
			{
				arguments_to_inject.append(ObfusString("-dx11:").str());
				arguments_to_inject.push_back(fallback_graphicsDriver == ObfusString("dx11").str() ? '1' : '0');
				arguments_to_inject.push_back(' ');

				if (game_version >= GV(15, 0, 0))
				{
					arguments_to_inject.append(ObfusString("-dx10:").str());
					arguments_to_inject.push_back(fallback_graphicsDriver == ObfusString("dx10").str() ? '1' : '0');
					arguments_to_inject.push_back(' ');
				}
			}
		}
		if (!got_windowMode && fallback_windowMode >= 0)
		{
			const int max = (game_version >= GV(40, 0, 0)) ? 2 : 1;
			if (fallback_windowMode <= max)
			{
				arguments_to_inject.append(game_version >= GV(40, 0, 0) ? ObfusString("-windowMode:").str() : ObfusString("-fullscreen:").str());
				arguments_to_inject.append(std::to_string(fallback_windowMode));
				arguments_to_inject.push_back(' ');
			}
		}
		if (game_version >= GV(8, 0, 0))
		{
			if (!got_cluster)
			{
				arguments_to_inject.append(ObfusString("-cluster:").str());
				arguments_to_inject.append(fallback_cluster);
				arguments_to_inject.push_back(' ');
			}
		}
		else
		{
			arguments_to_inject.append(ObfusString("-webserver:http://dummy.openwf.io/api/ ").str());
		}

		// This prevents the game from modifying H.Misc.cache by pre-populating the "device id".
		// It needs to be done here because DllMain runs before static initialisers.
		if (device_id_ptr)
		{
			//conout << "The device id is a crispy obfuscated 0 aka. " << *device_id_ptr << std::endl;

			wchar_t name[MAX_COMPUTERNAME_LENGTH > UNLEN ? MAX_COMPUTERNAME_LENGTH + 1 : UNLEN + 1];

			DWORD size = sizeof(name) / sizeof(wchar_t);
			GetComputerNameW(name, &size);
			uint32_t computer_name_hash = soup::joaat::hashRange((const char*)name, size * sizeof(wchar_t));

			size = sizeof(name) / sizeof(wchar_t);
			GetUserNameW(name, &size);
			uint32_t user_name_hash = soup::joaat::hashRange((const char*)name, size * sizeof(wchar_t));

			*device_id_ptr = (static_cast<uint64_t>(computer_name_hash) << 32) | user_name_hash;
			// Because the device_id is an obfuscated int, the observed "date" will differ across game versions.
		}
	}
#if LOGGING
	if (!arguments_to_inject.empty())
	{
		conout << "parse_arguments (injected): " << arguments_to_inject << std::endl;
	}
	conout << "parse_arguments: " << str << std::endl;
#endif
	return arguments_to_inject;
}

template <typename Str/*, bool has_languageVO, bool has_graphicsDriver*/>
static void parse_arguments_detour(uintptr_t arguments, Str* str, void* a3)
{
	if (auto arguments_to_inject = process_args_str(str->getData()); !arguments_to_inject.empty())
	{
		Str tmp;
		tmp.setUnownedData(arguments_to_inject.data(), arguments_to_inject.size());
		reinterpret_cast<decltype(&parse_arguments_detour<Str/*, has_languageVO, has_graphicsDriver*/>)>(parse_arguments_hook.original)(arguments, &tmp, a3);
	}

	reinterpret_cast<decltype(&parse_arguments_detour<Str/*, has_languageVO, has_graphicsDriver*/>)>(parse_arguments_hook.original)(arguments, str, a3);
}


/*static DetourHook SquadSetCountdownTimer_hook;

static __int64 SquadSetCountdownTimer_detour(void* a1, float seconds)
{
	//conout << "SquadSetCountdownTimer(" << seconds << ")" << std::endl;
	if (skip_mission_start_timer && !prohibit_skip_mission_start_timer && seconds == 5.9f)
	{
		seconds = 0.0f;
	}
	return reinterpret_cast<decltype(&SquadSetCountdownTimer_detour)>(SquadSetCountdownTimer_hook.original)(a1, seconds);
}*/

static luau_CFunction lua_SquadSetCountdownTimer_og;

static int lua_SquadSetCountdownTimer_detour(luau_State* L)
{
	if (skip_mission_start_timer && !prohibit_skip_mission_start_timer && L->intop[1].isType(LUAU_NUMBER) && L->intop[1].value.as_float == 5.9f)
	{
		L->intop[1].value.as_float = 0.0f;
	}
	return lua_SquadSetCountdownTimer_og(L);
}


static luau_CFunction lua_SanitizeText_og;

static int lua_SanitizeText_detour(luau_State* L)
{
	// L->intop[1] - string
	// L->intop[2] - number (TextSanitizerCategory enum) - 0 (TSC_CHAT) or 1 (TSC_NAME)

	if (disable_profanity_filter && !prohibit_disable_profanity_filter)
	{
		L->outtop = &L->intop[1];
		return 1;
	}
	else
	{
		return lua_SanitizeText_og(L);
	}
}


static float last_dmg = 0.0f;

static float get_dmg_to_display(int dmg_int)
{
	if (!high_damage_numbers_patch)
	{
		return static_cast<float>(dmg_int);
	}

	float dmg_number = (dmg_int < 0 ? last_dmg : static_cast<float>(dmg_int));
#if LOGGING
	//conout << "get_dmg_to_display: " << dmg_int << " -> " << dmg_number << std::endl;
	if ((dmg_number - last_dmg) > 1.0f)
	{
		conout << "DAMAGE INCONGRUENCE: Converting " << dmg_int << " to " << dmg_number << ", last damage was " << last_dmg << std::endl;
	}
#endif
	return dmg_number;
}

static DetourHook get_total_damage_hook;

static float get_total_damage_detour(__int64 *a1, __int64 a2, float a3, unsigned __int8 a4, float *a5, float *a6)
{
	float ret = reinterpret_cast<decltype(&get_total_damage_detour)>(get_total_damage_hook.original)(a1, a2, a3, a4, a5, a6);
#if LOGGING
	//conout << "get_total_damage: " << ret << std::endl;
#endif
	if (ret != 0.0f)
	{
		last_dmg = ret;
	}
	//ret = FLT_MAX;
	return ret;
}


static DetourHook init_cache_fetching_hook;

static void init_cache_fetching_detour(void* a1, bool a2, bool is_stripped, bool a4, bool a5, bool a6, uint8_t a7)
{
	is_stripped = false;
	reinterpret_cast<decltype(&init_cache_fetching_detour)>(init_cache_fetching_hook.original)(a1, a2, is_stripped, a4, a5, a6, a7);
}


static DetourHook name_lookup_hook;

enum LookupAction
{
	LA_KEEP_AS_IS,
	LA_USE_SERVER_HOST,
	LA_USE_NRS_HOST,
	LA_USE_IRC_HOST,
};

static std::string process_name_lookup(const char* data, size_t size)
{
#if LOGGING
	conout << "name_lookup: " << data;
#endif

	LookupAction lookup_action;
	{
		std::lock_guard lock(g_client_tunables_mtx);
		if (g_client_tunables.isStringInArray(joaat::compileTimeHash("dns"), joaat::hashRange(data, size)))
		{
			lookup_action = LA_USE_SERVER_HOST;
		}
		else if (g_client_tunables.isStringInArray(joaat::compileTimeHash("dns_nrs"), joaat::hashRange(data, size)))
		{
			lookup_action = LA_USE_NRS_HOST;
		}
		else if (g_client_tunables.isStringInArray(joaat::compileTimeHash("dns_irc"), joaat::hashRange(data, size)))
		{
			lookup_action = LA_USE_IRC_HOST;
		}
		else
		{
			lookup_action = LA_KEEP_AS_IS;
		}
	}

	if (lookup_action != LA_KEEP_AS_IS)
	{
		std::string override = server_host;
		if (lookup_action == LA_USE_NRS_HOST)
		{
			std::lock_guard lock(g_server_tunables_mtx);
			if (auto e = g_server_tunables.strings.find(soup::joaat::compileTimeHash("nrs")); e != g_server_tunables.strings.end())
			{
				override = e->second;
			}
			if (const char* sep = strchr(data, ':'))
			{
				override.append(sep);
			}
		}
		else if (lookup_action == LA_USE_IRC_HOST)
		{
			if (secure_connections)
			{
				override = ObfusString("127.0.0.1:").str();
				override.append(std::to_string(g_irc_port));
			}
			else
			{
				std::lock_guard lock(g_server_tunables_mtx);
				if (auto e = g_server_tunables.strings.find(soup::joaat::compileTimeHash("irc")); e != g_server_tunables.strings.end())
				{
					override = e->second;
				}
				if (const char* sep = strchr(data, ':'))
				{
					override.append(sep);
				}
			}
		}
		else
		{
			if (const char* sep = strchr(data, ':'))
			{
				override.append(sep);
			}
		}
#if LOGGING
	conout << " -> " << override << std::endl;
#endif
		return override;
	}
#if LOGGING
	conout << std::endl;
#endif
	return {};
}

template <typename T>
static bool name_lookup_detour(void* out, T* name, bool a3)
{
	auto override = process_name_lookup(name->getData(), name->getSize());
	if (!override.empty())
	{
		name->setUnownedData(override.data(), override.size());
	}
	return reinterpret_cast<decltype(&name_lookup_detour<T>)>(name_lookup_hook.original)(out, name, a3);
}


static DetourHook write_to_log_file_hook;
static void* write_to_log_file_a1 = nullptr;
static ObfusString log_sep("]: ");

static void write_to_log_file_detour(void* const a1, char* const data, size_t _size)
{
	write_to_log_file_a1 = a1;
	SOUP_IF_LIKELY (_size > 15)
	{
		SOUP_IF_LIKELY (auto message = strstr(data + 15, log_sep.c_str()))
		{
			message += log_sep.size();
			size_t size = _size - (message - data);

			if (size > 10)
			{
				switch (soup::joaat::hashRange(message, 10))
				{
				case soup::joaat::compileTimeHash("Logged in "):
					owfOverlay::onLoggedIn();
					break;

				case soup::joaat::compileTimeHash("Cache mani"): // "Cache manifest hash "
					if (size == 43)
					{
						owf_set_build_hash(message + 20/*, 22*/);
					}
					break;

				case soup::joaat::compileTimeHash("Cache lang"): // "Cache languages enabled: _xx"
					if (size == 28)
					{
						game_lang_code = std::string(message + 26, 2);
					}
					break;

				case soup::joaat::compileTimeHash("Using lang"): // "Using language: _xx"
					if (size == 19)
					{
						game_lang_code = std::string(message + 17, 2);
					}
					break;

				case soup::joaat::compileTimeHash("InitMappin"): // "InitMapping for all devices with bindings ... and filter ..."
					if (size > 42)
					{
						ObfusString sep(" and filter ");
						if (auto filter = strstr(message + 42, sep.c_str()))
						{
							filter += 12;
							size -= (filter - message);
							size -= 1; // '\n'
							active_input_filter = std::string(filter, size);
							std::lock_guard lock(g_client_tunables_mtx);
							active_input_filter_allows_hotkeys = !g_client_tunables.isStringInArray(joaat::compileTimeHash("nhkif"), joaat::hash(active_input_filter));
						}
					}
					break;

				case soup::joaat::compileTimeHash("Failed to "): // "Failed to created child context for function OWF_..., script: /Lotus/Interface/PostCameraUpdateHud.lua"
					if (size > 100 && soup::joaat::hashRange(message + 10, 39) == soup::joaat::compileTimeHash("created child context for function OWF_"))
					{
						const auto name = std::string(message + 49, size - 100);
#if LOGGING
						conout << "OWF callback called: " << name << std::endl;
#endif
						std::lock_guard lock(running_scripts_mtx);
						for (auto& scr : running_scripts)
						{
							if (scr->callbacks.contains(name))
							{
								scr->callbacks.erase(name);
								scr->events.emplace_back(OWF_EVT_CALLBACK, std::move(name));
								break;
							}
						}
						return; // Don't log this
					}
					break;
				}
			}
		}
	}
	if (ee_log_in_console)
	{
		conout << std::string(data, _size);
	}
	reinterpret_cast<decltype(&write_to_log_file_detour)>(write_to_log_file_hook.original)(a1, data, _size);
}

static void write_to_ee_log(const char* data, size_t size)
{
	if (write_to_log_file_a1)
	{
		reinterpret_cast<decltype(&write_to_log_file_detour)>(write_to_log_file_hook.original)(write_to_log_file_a1, const_cast<char*>(data), size);
	}
}

static void write_to_ee_log(const char* str)
{
	return write_to_ee_log(str, strlen(str));
}


void owf_set_build_hash(const char _build_hash[22])
{
	if (memcmp(build_hash, _build_hash, 22) != 0)
	{
		memcpy(build_hash, _build_hash, 22);

		{
			std::lock_guard mtx(g_repo_mtx);
			if (auto expected_ver = g_repo.getExpectedCodeVersionForManifestHash(build_hash);
				expected_ver != nullptr && memcmp(expected_ver, build_version, 16) != 0
				)
			{
				auto msg = get_core_string(ObfusString("badbldlbl"));
				soup::string::replaceAll(msg, ObfusString("|EXPECTED_VER|").str(), std::string(expected_ver, 16));
				soup::string::replaceAll(msg, ObfusString("|FOUND_VER|").str(), std::string(build_version, 16));
				soup::string::replaceAll(msg, ObfusString("|HASH|").str(), std::string(build_hash, 22));

				const auto msg_utf16 = soup::unicode::utf8_to_utf16(msg);
				const auto title_utf16 = soup::unicode::utf8_to_utf16(get_bootstrapper_title());
				if (MessageBoxW(0, msg_utf16.c_str(), title_utf16.c_str(), MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
				{
					exit(1);
				}
			}
		}
	}
}


static luau_CFunction lua_FlashMgr_GetConfigBool_og;

static int lua_FlashMgr_GetConfigBool_detour(luau_State* L)
{
	SOUP_IF_LIKELY (L->intop[1].isType(LUAU_STRING))
	{
		if (autologin && !did_auto_login)
		{
			ObfusString str("Client.AutoLogin");
			if (strcmp(L->intop[1].getString(), str.c_str()) == 0)
			{
#if LOGGING
				conout << "Reporting Client.AutoLogin as true" << std::endl;
#endif
				L->outtop[-1].value.as_bool = true;
				L->outtop[-1].setType(LUAU_BOOL);
				return 1;
			}
		}

		if (alternative_loading)
		{
			ObfusString str("Server.FastLoad");
			if (strcmp(L->intop[1].getString(), str.c_str()) == 0)
			{
#if LOGGING
				conout << "Reporting Server.FastLoad as true" << std::endl;
#endif
				L->outtop[-1].value.as_bool = true;
				L->outtop[-1].setType(LUAU_BOOL);
				return 1;
			}
		}
	}

	return lua_FlashMgr_GetConfigBool_og(L);
}


static bool force_disable_overlay;
static bool did_console_to_overlay_transition = false;

static void do_console_to_overlay_transition()
{
	if (!did_console_to_overlay_transition)
	{
		did_console_to_overlay_transition = true;
		if (!disable_overlay && !force_disable_overlay)
		{
			owfOverlay::init();
		}
#if !LOGGING
		else if (!owfConfig::isConsoleEnabled()
			&& owfConsole::active
			)
		{
			owfConsole::deactivate();
		}
#endif
	}
}

static void handle_set_global(luau_State* L, uint32_t hash)
{
	ObfusString str_gRegion("gRegion");
	ObfusString str_gFlashMgr("gFlashMgr");
	ObfusString str_gGameData("gGameData");
	ObfusString str_gPlayerProfileMgr("gPlayerProfileMgr");
	ObfusString str_gClient("gClient");
	ObfusString str_gMatchingService("gMatchingService");

	if (hash == wf_hash(str_gRegion.c_str()))
	{
		regionmgr = L->outtop[-1].isType(LUAU_USERDATA) ? static_cast<RegionMgr*>(L->outtop[-1].getObject()) : nullptr;
#if LOGGING
		conout << " (gRegion) = " << regionmgr;
		//conout << " " << resolve_string_handle(regionmgr->type->getPathHandle());
		//conout << " " << resolve_string_handle(regionmgr->type->name_handle);
#endif
		do_console_to_overlay_transition();
	}
	else if (hash == wf_hash(str_gFlashMgr.c_str()))
	{
		flashmgr = L->outtop[-1].isType(LUAU_USERDATA) ? L->outtop[-1].getObject() : nullptr;
#if LOGGING
		conout << " (gFlashMgr) = " << flashmgr;
#endif
	}
	else if (hash == wf_hash(str_gGameData.c_str()))
	{
		gamedata = L->outtop[-1].isType(LUAU_USERDATA) ? L->outtop[-1].getObject() : nullptr;
#if LOGGING
		conout << " (gGameData) = " << gamedata;
#endif
	}
	else if (hash == wf_hash(str_gPlayerProfileMgr.c_str()))
	{
		profilemgr = L->outtop[-1].isType(LUAU_USERDATA) ? L->outtop[-1].getObject() : nullptr;
#if LOGGING
		conout << " (gPlayerProfileMgr) = " << profilemgr;
#endif
	}
	else if (hash == wf_hash(str_gClient.c_str()))
	{
		gClient = L->outtop[-1].isType(LUAU_USERDATA) ? L->outtop[-1].getObject() : nullptr;
#if LOGGING
		conout << " (gClient) = " << gClient;
#endif
	}
	else if (hash == wf_hash(str_gMatchingService.c_str()))
	{
		matchingservice = L->outtop[-1].isType(LUAU_USERDATA) ? *(void**)(L->outtop[-1].value.as_uintptr + 0x18) : nullptr;
#if LOGGING
		conout << " (gMatchingService) = " << matchingservice;
#endif
	}
#if LOGGING
	conout << std::endl;
#endif
}


static CompactDetourHook lua_set_global_by_hash_hook;

static void lua_set_global_by_hash_detour(luau_State* L, uint32_t hash)
{
#if LOGGING
	conout << "lua_set_global_by_hash: " << hash;
#endif
	handle_set_global(L, hash);
	return reinterpret_cast<decltype(&lua_set_global_by_hash_detour)>(lua_set_global_by_hash_hook.original)(L, hash);
}


static DetourHook lua_set_global_hook;

static void lua_set_global_detour(luau_State* L, const char* name)
{
#if LOGGING
	conout << "lua_set_global: " << name;
#endif
	handle_set_global(L, wf_hash(name));
	return reinterpret_cast<decltype(&lua_set_global_detour)>(lua_set_global_hook.original)(L, name);
}


void populate_autostart_scripts(JsonObject& obj)
{
	auto arr = soup::make_unique<JsonArray>();
	for (const auto& name : auto_start_scripts)
	{
		arr->children.emplace_back(soup::make_unique<JsonString>(name));
	}
	obj.add(ObfusString("autostart_scripts"), std::move(arr));
}

static void populate_running_scripts_locked(JsonObject& obj)
{
	auto arr = soup::make_unique<JsonArray>();
	for (const auto& scr : running_scripts)
	{
		arr->children.emplace_back(soup::make_unique<JsonString>(std::string(scr->name)));
	}
	obj.add(ObfusString("running_scripts"), std::move(arr));
}

static void populate_running_scripts(JsonObject& obj)
{
	std::lock_guard lock(running_scripts_mtx);
	return populate_running_scripts_locked(obj);
}

void broadcast_running_scripts_locked()
{
	JsonObject obj;
	populate_running_scripts_locked(obj);
	owf_broadcast_message(obj.encode());
}

static luau_CFunction lua_LotusHudStatus_UpdateFlashMarkers_og;

using raise_script_error_t = bool(*)(const char** err);
static raise_script_error_t* raise_script_error_fp = nullptr;

#define PROFILE_SCRIPT_TICKING false

static int lua_LotusHudStatus_UpdateFlashMarkers_detour(luau_State* L)
{
#if PROFILE_SCRIPT_TICKING
	auto t = soup::time::nanos();
#endif

#if true
	const auto og_outtop = luau_savestack(L, L->outtop);
	const auto og_intop = luau_savestack(L, L->intop);
	const auto og_lngjmp = L->global_state->error_longjump_data();
	const auto og_panic = L->global_state->panic_func();
	raise_script_error_t og_raise;

	luau_L = L;
	if (have_scripting)
	{
		// ivkr_call(ivkr_find_method("HumanPlayer", "IsFreeCameraActive"), 0)
		L->global_state->error_longjump_data() = nullptr;
		L->global_state->panic_func() = [](luau_State* L, int)
		{
#if LOGGING
			conout << "LuaU is panicking" << std::endl;
#endif
			luau_error_msg = (--L->outtop)->getString();
#if LOGGING
			conout << luau_error_msg << std::endl;
#endif
			throw 0;
		};
	}
	if (raise_script_error_fp)
	{
		// Handle script errors not being raised via luaL_error
		// E.g. `ent:Attach(Type("/Lotus/Characters/Civilian/NoraNight/NoraHair.fbx"))` will raise `Error (arg 2): expected 'Type<Entity>' got '/Lotus/Characters/Civilian/NoraNight/NoraHair.fbx'` like this. When we don't handle this, the error message/string will remain on the stack and result in a confusing follow-up error like `Error (arg 5): expected Euler * but got string`.
		og_raise = *raise_script_error_fp;
		*raise_script_error_fp = [](const char** err) -> bool
		{
#if LOGGING
			conout << "raise_script_error called" << std::endl;
#endif
			luau_error_msg = *err;
#if LOGGING
			conout << luau_error_msg << std::endl;
#endif
			throw 0;
		};
	}

	{
		std::lock_guard mtx(running_scripts_mtx);
		if (active_input_filter_allows_hotkeys && !prohibit_scripts)
		{
			if (DWORD pid; GetWindowThreadProcessId(GetForegroundWindow(), &pid), pid == GetCurrentProcessId())
			{
				if (hotkeys_mtx.tryLock())
				{
					for (auto& hk : hotkeys)
					{
						if (hk.wasJustPressed())
						{
							start_script_from_string(hk.script);
						}
					}
					hotkeys_mtx.unlock();
				}
			}
		}
		if (bgscript != nullptr)
		{
			SOUP_IF_UNLIKELY (!bgscript->tick())
			{
				delete bgscript;
				bgscript = nullptr;
			}
		}
		bool any_killed = false;
		for (auto i = running_scripts.begin(); i != running_scripts.end(); )
		{
			if (!prohibit_scripts && (*i)->tick())
			{
				++i;
			}
			else
			{
				delete &**i; static_assert(std::is_same_v<decltype(&**i), owfScript*>);
				i = running_scripts.erase(i);
				any_killed = true;
			}
		}
		SOUP_IF_UNLIKELY (any_killed)
		{
			broadcast_running_scripts_locked();
		}
	}

#if PRIVATE
	if (luau_savestack(L, L->outtop) != og_outtop)
	{
		owfScript::logNl("Not all values were popped from LuaU stack");
	}
#endif
	if (have_scripting)
	{
		L->outtop = luau_restorestack(L, og_outtop);
		L->intop = luau_restorestack(L, og_intop);
		L->global_state->error_longjump_data() = og_lngjmp;
		L->global_state->panic_func() = og_panic;
	}
	if (raise_script_error_fp)
	{
		*raise_script_error_fp = og_raise;
	}
	luau_L = nullptr;
#endif

#if PROFILE_SCRIPT_TICKING
	t = soup::time::nanos() - t;
	conout << "Ticking scripts took " << (static_cast<double>(t) / 1000000.0) << " ms\n";
#endif

	return lua_LotusHudStatus_UpdateFlashMarkers_og(L);
}


template <typename T>
struct GameRange
{
	T* begin;
	T* end;
};

static DetourHook register_enum_hook;

static void register_enum_detour(void* a1, GameRange<const char*>& names, GameRange<int32_t>& values)
{
	//conout << "register_enum" << std::endl;

	auto& vec = swig_enums2.emplace_back();
	vec.reserve(names.end - names.begin);

	const char** name = names.begin;
	int32_t* value = values.begin;
	for (; name != names.end; ++name, ++value)
	{
		//conout << "\t" << *name << ", " << *value << std::endl;
		vec.emplace_back(SwigEnumSelfAllocated{ *name, *value });
	}

	return reinterpret_cast<decltype(&register_enum_detour)>(register_enum_hook.original)(a1, names, values);
}


static ReplacementHook get_profile_dir_hook;
static uint32_t get_profile_dir_offset;

static GameString* get_profile_dir_detour(uintptr_t a1)
{
	auto str = reinterpret_cast<GameString*>(a1 + get_profile_dir_offset);
	str->setUnownedData(forced_profile_dir.data(), forced_profile_dir.size());
	return str;
}


static CompactDetourHook lua_AvatarEntry_excludedFromSimulacrum_get_hook;

static int lua_AvatarEntry_excludedFromSimulacrum_get_detour(luau_State* L)
{
	reinterpret_cast<luau_CFunction>(lua_AvatarEntry_excludedFromSimulacrum_get_hook.original)(L);
	//conout << "lua_AvatarEntry_excludedFromSimulacrum_get: " << L->outtop[-1].value.as_bool << std::endl;
	L->outtop[-1].value.as_bool = L->outtop[-1].value.as_bool ? !simulacrum_blacklisted : !simulacrum_whitelisted;
	return 1;
}


static DetourHook is_pause_allowed_hook;

static bool is_pause_allowed_detour(void* gamerules)
{
	return pause_always_stops_time
		|| reinterpret_cast<decltype(&is_pause_allowed_detour)>(is_pause_allowed_hook.original)(gamerules)
		;
}


static luau_CFunction lua_OpenWebBrowser_og;

static int lua_OpenWebBrowser_detour(luau_State* L)
{
#if LOGGING
	conout << "lua_OpenWebBrowser: " << L->intop[0].getString() << std::endl;
#endif
	ObfusString sub("warframe.com");
	if (strstr(L->intop[0].getString(), sub.c_str()) != nullptr)
	{
		// Purchases have a sku; other usages instead have redirect, e.g.:
		// ...&redirect=/patch-notes/...
		// ...&redirect=/updates/...
		ObfusString sub2("&redirect=");
		if (const auto redirect = strstr(L->intop[0].getString(), sub2.c_str()))
		{
			const auto path = redirect + sub2.size();
			if (luau_pushstring)
			{
				std::string new_url = ObfusString("https://www.warframe.com").str() + path;
				string::replaceAll(new_url, ObfusString("/updates/").str(), ObfusString("/patch-notes/").str()); // The old /updates/ links now 404 instead of just redirecting...
				L->outtop = &L->intop[0];
				luau_pushstring(L, new_url.c_str());
				return lua_OpenWebBrowser_og(L);
			}
		}
		return 0;
	}
	return lua_OpenWebBrowser_og(L);
}


static luau_CFunction lua_FlashInstance_GetStringVariable_og;

static int lua_FlashInstance_GetStringVariable_detour(luau_State* L)
{
	auto ret = lua_FlashInstance_GetStringVariable_og(L);
	//conout << "lua_FlashInstance_GetStringVariable: " << L->intop[1].getString() << " -> " << L->outtop[-1].getString() << std::endl;
	if (soup::joaat::hash(L->intop[1].getString()) == soup::joaat::compileTimeHash("Window.SendMessageBar.MessageBox"))
	{
		bool block = false;
		{
			const std::string current_draft = L->outtop[-1].getString();

			std::lock_guard lock(running_scripts_mtx);
			for (auto& scr : running_scripts)
			{
				if (auto pBlock = scr->findChatSendSubscription(current_draft))
				{
					block |= *pBlock;
					if (L->intop[-3].isType(LUAU_NIL)) // Heuristic to determine if the message was just submitted
					{
						scr->events.emplace_back(OWF_EVT_SUBMIT_CHAT_MESSAGE, (uint32_t)*pBlock, current_draft);
					}
				}
			}
		}

		if (block && luau_pushstring)
		{
			// Stop the game from processing this
			L->outtop--;
			luau_pushstring(L, " ");
		}

		if (luau_gettable)
		{
			int i = 0;
			while (--i > -20)
			{
				if (L->outtop[i].isType(LUAU_TABLE))
				{
					ObfusString name("mPanelList");
					luau_pushstring(L, name.c_str());
					if (luau_gettable(L, i - 1) > 0)
					{
						ChatRedux_table = L->outtop[i - 1].value.as_uintptr;
						L->outtop--;
						break;
					}
					L->outtop--;
				}
			}
		}
	}
	return ret;
}


#if LABEL_REPLACEMENTS
static CompactDetourHook check_string_substitutions_hook;

static void static_check_string_substitutions_detour(GameString* str, void* substitutions, GameString* loctag, bool dont_log)
{
	size_t size = 0;
	if (const char* data = do_label_replacements(str->getData(), str->getSize(), loctag->getData(), loctag->getSize(), size))
	{
		str->setUnownedData(data, size);
	}
	else if (size == -1)
	{
		std::swap(*str, *loctag);
	}
	return reinterpret_cast<decltype(&static_check_string_substitutions_detour)>(check_string_substitutions_hook.original)(str, substitutions, loctag, dont_log);
}

template <typename Str>
static void check_string_substitutions_detour(void* a1, Str* str, void* substitutions, Str* loctag, bool dont_log)
{
	size_t size = 0;
	if (const char* data = do_label_replacements(str->getData(), str->getSize(), loctag->getData(), loctag->getSize(), size))
	{
		str->setUnownedData(data, size);
	}
	else if (size == -1)
	{
		std::swap(*str, *loctag);
	}
	return reinterpret_cast<decltype(&check_string_substitutions_detour<Str>)>(check_string_substitutions_hook.original)(a1, str, substitutions, loctag, dont_log);
}
#endif


#if METADATA_PATCHES && SOUP_BITS == 64
static MetadataPatch* current_patch = nullptr;
static void load_metadata_patches()
{
	std::lock_guard lock(metadata_patches_mtx);
	metadata_patches.clear();

	auto L = luaL_newstate();
	owfScript::openLibs(L);

	lua_pushcfunction(L, [](lua_State* L) -> int
	{
		size_t len;
		const char* str = luaL_checklstring(L, 1, &len);
		const auto hash = soup::joaat::hashRange(str, len);
		if (auto e = metadata_patches.find(hash); e != metadata_patches.end())
		{
			current_patch = &e->second;
		}
		else
		{
			current_patch = &metadata_patches.emplace(hash, MetadataPatch{}).first->second;
		}
		current_patch->prefix.append(pluto_checkstring(L, 2));
		current_patch->discard_original = (current_patch->discard_original || lua_toboolean(L, 3));
		current_patch->debug = (current_patch->debug || lua_toboolean(L, 4));
		metadata_patches_in_use = true;
		return 0;
	});
	{ ObfusString name("new_patch"); lua_setglobal(L, name.c_str()); }

	lua_pushcfunction(L, [](lua_State* L) -> int
	{
		if (current_patch)
		{
			current_patch->replacements.emplace_back(pluto_checkstring(L, 1), pluto_checkstring(L, 2));
		}
		return 0;
	});
	{ ObfusString name("add_replacement"); lua_setglobal(L, name.c_str()); }

	lua_pushcfunction(L, [](lua_State* L) -> int
	{
		int pushed = 0;
		if (current_patch)
		{
			try
			{
				current_patch->substitutions.emplace_back(soup::Regex(pluto_checkstring(L, 1), luaL_checkstring(L, 3)), pluto_checkstring(L, 2));
			}
			catch (const std::exception& e)
			{
				lua_pushstring(L, e.what()); ++pushed;
			}
		}
		return pushed;
	});
	{ ObfusString name("add_substitution"); lua_setglobal(L, name.c_str()); }

	lua_pushcfunction(L, [](lua_State* L) -> int
	{
		if (current_patch)
		{
			current_patch->query_assignments.emplace_back(pluto_checkstring(L, 1), pluto_checkstring(L, 2));
		}
		return 0;
	});
	{ ObfusString name("add_query_assignment"); lua_setglobal(L, name.c_str()); }

	size_t size;
	auto data = g_repo.find(soup::joaat::compileTimeHash("OpenWF/helpers/load_metadata_patches.pluto"), size);
	if (luaL_loadbuffer(L, data, size, nullptr) != LUA_OK
		|| lua_pcall(L, 0, 0, 0) != LUA_OK
		)
	{
		owfScript::logNl(lua_type(L, -1) == LUA_TSTRING ? pluto_checkstring(L, -1) : ObfusString("Non-string script error").str());
	}

	lua_close(L);
}

static void handle_metadata_read(ObjectType* objectType, GameString* str)
{
	const char* path = resolve_string_handle(objectType->getPathHandle());
	const char* name = resolve_string_handle(objectType->name_handle);

	uint32_t hash = 0;
	hash = joaat::partialStr(path, hash);
	hash = joaat::partialStr(name, hash);
	joaat::finalise(hash);

	bool should_write_to_console = write_all_metadata_reads_to_console;
	bool should_write_to_ee_log = write_all_metadata_reads_to_ee_log;

	std::lock_guard lock(metadata_patches_mtx);
	if (auto e = metadata_patches.find(hash); e != metadata_patches.end())
	{
		auto& patch = e->second;
		auto& buf = patch.final_data;
		buf.clear();
		buf.reserve(patch.prefix.size() + str->getSize());
		buf.append(patch.prefix);
		if (patch.replacements.empty() && patch.substitutions.empty() && patch.query_assignments.empty())
		{
			if (!patch.discard_original)
			{
				buf.append(str->getData(), str->getSize());
			}
		}
		else
		{
			std::string text;
			if (!patch.discard_original)
			{
				text = std::string(str->getData(), str->getSize());
			}
			for (const auto& replacement : patch.replacements)
			{
				string::replaceAll(text, replacement.first, replacement.second);
			}
			for (const auto& substitution : patch.substitutions)
			{
				text = substitution.first.substituteAll(text, substitution.second);
			}
			if (!patch.query_assignments.empty())
			{
				try
				{
					EeNotationParser par;
					auto jr = par.parse(text);
					for (const auto& qa : patch.query_assignments)
					{
						if (UniquePtr<JsonNode>* upN = jr->queryUp(qa.first.c_str()))
						{
							JsonNode* n = *upN;
							if (n->isStr())
							{
								n->reinterpretAsStr().value = qa.second;
							}
							else if (n->isInt())
							{
								if (!soup::string::toIntOpt<int64_t>(qa.second, soup::string::TI_FULL).consume(n->reinterpretAsInt().value))
								{
									if (!soup::string::toIntOpt<int64_t>(qa.second).consume(n->reinterpretAsInt().value))
									{
										conout << ObfusString("[Metadata Patches] Invalid integer value: ").str() << qa.second << std::endl;
										conout << ObfusString("[Metadata Patches] - Object Type: ").str() << path << name << std::endl;
									}
									else
									{
										*upN = soup::make_unique<JsonFloat>(std::stod(qa.second));
									}
								}
							}
							else
							{
								n->asFloat().value = std::stod(qa.second);
							}
						}
						else if (patch.debug)
						{
							conout << ObfusString("[Metadata Patches] ").str() << qa.first << ObfusString(" did not resolve in ").str() << path << name << std::endl;
							conout << ObfusString("[Metadata Patches] - Object Type: ").str() << path << name << std::endl;
						}
					}
					text = EeNotationParser::unparse(*jr);
				}
				catch (const std::exception& e)
				{
					conout << ObfusString("[Metadata Patches] Error applying query assignment: ").str() << e.what() << std::endl;
					conout << ObfusString("[Metadata Patches] - Object Type: ").str() << path << name << std::endl;
				}
			}
			buf.append(text);
		}
		str->setUnownedData(buf.data(), buf.size());

		if (!patch.is_implicit)
		{
			should_write_to_console = write_patched_metadata_reads_to_console;
			should_write_to_ee_log = write_patched_metadata_reads_to_ee_log;
		}
		patch.applied = true;
	}
	else if (save_all_metadata)
	{
		metadata_patches.emplace(hash, MetadataPatch{
			.final_data = std::string(str->getData(), str->getSize()),
			.is_implicit = true,
			.applied = true,
		});
	}

	if (should_write_to_console)
	{
		ObfusString prefix("Reading metadata for ");
		conout.write(prefix.data(), prefix.size());
		conout << path << name << "\n";
	}
	if (should_write_to_ee_log)
	{
		ObfusString prefix("[OpenWF] Reading metadata for ");
		write_to_ee_log(prefix.data(), prefix.size());
		write_to_ee_log(path);
		write_to_ee_log(name);
		write_to_ee_log("\n", 1);
	}
}

static CallsiteHook object_type_serialise_propery_text_hook;

static void object_type_serialise_propery_text_detour(void* a1, GameString* str, int a3, char a4)
{
	ObjectType* objectType;
	__asm mov objectType, r11;

	handle_metadata_read(objectType, str);

	return reinterpret_cast<decltype(&object_type_serialise_propery_text_detour)>(object_type_serialise_propery_text_hook.original)(a1, str, a3, a4);
}
#endif


#if false
static DetourHook ScriptMgr_startInstance_hook;

static bool ScriptMgr_startInstance_detour(void* _this, ScriptInstance* inst/*, void* a3, void* a4*/)
{
	if (inst->script_type)
	{
		const char* path = resolve_string_handle(inst->script_type->getPathHandle());
		const char* name = resolve_string_handle(inst->script_type->name_handle);
		const char* func_name = resolve_string_handle(inst->func_name_handle);

#if LOGGING
		//conout << "ScriptMgr_startInstance: " << path << name << ", " << func_name << "\n";
#endif

		uint32_t hash = 0;
		hash = joaat::partialStr(path, hash);
		hash = joaat::partialStr(name, hash);
		hash = joaat::partialStr(func_name, hash);
		joaat::finalise(hash);

		bool block = false;
		{
			std::lock_guard lock(running_scripts_mtx);
			for (auto& scr : running_scripts)
			{
				if (auto e = scr->findSubscribedScriptTrigger(hash))
				{
					block |= *e;
					std::string data = path;
					data.append(name);
					data.push_back(':');
					data.append(func_name);
					scr->events.emplace_back(OWF_EVT_SCRIPT_TRIGGERED, std::move(data));
				}
			}
		}
		SOUP_IF_UNLIKELY (block)
		{
			return false;
		}
	}

	return reinterpret_cast<decltype(&ScriptMgr_startInstance_detour)>(ScriptMgr_startInstance_hook.original)(_this, inst/*, a3, a4*/);
}
#endif


static DetourHook irc_send_raw_hook;

static std::string create_token(const std::string& accountId, const std::string& nonce)
{
	soup::sha256::HmacState st(nonce);
	st.append("accountId=", 10);
	st.append(accountId.data(), accountId.size());
	st.append("&ct=IRC", 7);
	st.finalise();
	return string::bin2hexLower(st.getDigest());
}

static std::string process_irc_send(const char* data, size_t size)
{
#if LOGGING
	conout << "irc_send_raw: " << std::string(data, size) << std::endl;
#endif
	if (size > 36 && soup::joaat::hashRange(data, 4) == soup::joaat::compileTimeHash("NICK")) // NICK & USER are sent in the same message
	{
		auto arr = string::explode(auth_query, '&');
		if (arr.size() > 1)
		{
			const std::string accountId = arr[0].substr(10);
			const std::string nonce = arr[1].substr(6);
			const size_t realname_length = (game_version >= GV(9, 0, 0)) ? 40 : nonce.size();
			std::string replacement(data, size - realname_length);
			replacement.append(ObfusString("token=").str() + create_token(accountId, nonce));
			return replacement;
		}
	}
	if (size > 10 && soup::joaat::hashRange(data, 8) == soup::joaat::compileTimeHash("PRIVMSG "))
	{
		std::string_view sv(data, size);
		const auto sep = sv.find(ObfusString(" :").str());
		if (sep != std::string::npos)
		{
			std::string_view message(data + sep + 2, size - (sep + 2));
			//conout << "channel_name = " << sv.substr(8, sep - 8) << std::endl;
			//conout << "message = " << message << std::endl;
			{
				std::lock_guard lock(running_scripts_mtx);
				for (auto& scr : running_scripts)
				{
					if (scr->isSubscribedToOutgoingMessage(message))
					{
						scr->events.emplace_back(OWF_EVT_OUTGOING_CHAT_MESSAGE, std::string(data + 8, size - 8));
					}
				}
			}
		}
	}
	return {};
}

template <typename Str>
static void irc_send_raw_detour(void* a1, Str* str, bool bLogIt)
{
#if VERBOSE_IRC
	bLogIt = true;
#endif
	if (auto replacement = process_irc_send(str->getData(), str->getSize()); !replacement.empty())
	{
		Str tmp;
		tmp.setUnownedData(replacement.data(), replacement.size());
		return reinterpret_cast<decltype(&irc_send_raw_detour<Str>)>(irc_send_raw_hook.original)(a1, &tmp, bLogIt);
	}
	return reinterpret_cast<decltype(&irc_send_raw_detour<Str>)>(irc_send_raw_hook.original)(a1, str, bLogIt);
}


#if VERBOSE_RNG
static int64_t* lua_seed;
static luau_CFunction lua_SetSeed_og;
static luau_CFunction lua_ChurnSeed_og;
static luau_CFunction lua_SRandom_og;
static luau_CFunction lua_SRandomInt_og;
static luau_CFunction lua_HashCrc32_og;

static int lua_SetSeed_detour(luau_State* L)
{
	lua_SetSeed_og(L);
	conout << "lua_SetSeed: lua_seed is now " << (lua_seed ? std::to_string(*lua_seed) : "[unknown]") << std::endl;
	return 0;
}

static int lua_ChurnSeed_detour(luau_State* L)
{
	lua_ChurnSeed_og(L);
	conout << "lua_ChurnSeed: " << L->intop[1].value.as_float << " iterations; lua_seed is now " << (lua_seed ? std::to_string(*lua_seed) : "[unknown]") << std::endl;
	return 0;
}

static int lua_SRandom_detour(luau_State* L)
{
	lua_SRandom_og(L);
	conout << "lua_SRandom(" << L->intop[0].value.as_float << ", " << L->intop[1].value.as_float << "): generated " << L->outtop[-1].value.as_float << "; lua_seed is now " << (lua_seed ? std::to_string(*lua_seed) : "[unknown]") << std::endl;
	return 1;
}

static int lua_SRandomInt_detour(luau_State* L)
{
	lua_SRandomInt_og(L);
	conout << "lua_SRandomInt(" << L->intop[0].value.as_float << ", " << L->intop[1].value.as_float << "): generated " << L->outtop[-1].value.as_float << "; lua_seed is now " << (lua_seed ? std::to_string(*lua_seed) : "[unknown]") << std::endl;
	return 1;
}

static int lua_HashCrc32_detour(luau_State* L)
{
	lua_HashCrc32_og(L);
	conout << "lua_HashCrc32(" << L->intop[0].getString() << "): returned " << L->outtop[-1].value.as_float << std::endl;
	return 1;
}
#endif


#if VERBOSE_CRC32
static ReplacementHook crc32_impl_hook;

static uint32_t crc32_impl_detour(uint32_t initial, const char* data, size_t size)
{
	auto res = soup::crc32::hash((const uint8_t*)data, size, initial);
	conout << "CRC32: initial = " << initial << ", data = " << string::bin2hex(std::string(data, size)) << ", res = " << res << ", caller offset = " << Pointer(_ReturnAddress()).sub(Module(nullptr).range.base.as<uintptr_t>()).as<void*>() << std::endl;
	return res;
}
#endif


#if VERBOSE_CRC32C
static DetourHook crc32c_impl_hook;

static uint32_t crc32c_impl_detour(uint32_t initial, const char* data, size_t size)
{
	auto res = reinterpret_cast<decltype(&crc32c_impl_detour)>(crc32c_impl_hook.original)(initial, data, size);
	conout << "CRC32C: initial = " << initial << ", data = " << string::bin2hex(std::string(data, size)) << ", res = " << res << ", caller offset = " << Pointer(_ReturnAddress()).sub(Module(nullptr).range.base.as<uintptr_t>()).as<void*>() << std::endl;
	return res;
}
#endif


#if VERBOSE_MD5
static DetourHook MD5_append_hook;

static void MD5_append_detour(void* state, const char* data, size_t size)
{
	conout << "MD5_append: state = " << state << ", data = " << string::bin2hex(std::string(data, size)) << ", caller offset = " << Pointer(_ReturnAddress()).sub(Module(nullptr).range.base.as<uintptr_t>()).as<void*>() << std::endl;
	return reinterpret_cast<decltype(&MD5_append_detour)>(MD5_append_hook.original)(state, data, size);
}
#endif


#if VERBOSE_SERPROPTXT
static DetourHook serialise_propery_text_hook;

static void serialise_propery_text_detour(void* a1, GameString* str, int a3, char a4)
{
	conout << "serialise_propery_text: a3 = " << a3 << ", a4 = " << a4 << ", caller offset = " << Pointer(_ReturnAddress()).sub(Module(nullptr).range.base.as<uintptr_t>()).as<void*>() << std::endl;
	conout.write(str->getData(), str->getSize());
	return reinterpret_cast<decltype(&serialise_propery_text_detour)>(serialise_propery_text_hook.original)(a1, str, a3, a4);
}
#endif


#if VERBOSE_OODLE
struct GameOodleNetworkState
{
	PAD(0, 0x38) uint32_t htbits;
	/* 0x40 */ GameBuffer compacted_state;
	/* 0x50 */ GameBuffer window;
	/* 0x60 */ GameBuffer state;
	/* 0x70 */ GameBuffer shared;
};
#if SOUP_BITS == 64
static_assert(offsetof(GameOodleNetworkState, htbits) == 0x38);
static_assert(offsetof(GameOodleNetworkState, compacted_state) == 0x40);
static_assert(offsetof(GameOodleNetworkState, window) == 0x50);
static_assert(offsetof(GameOodleNetworkState, state) == 0x60);
static_assert(offsetof(GameOodleNetworkState, shared) == 0x70);
#endif

static DetourHook init_oodle_network_state_hook;

static __int64 init_oodle_network_state_detour(GameOodleNetworkState* st)
{
	conout << "compacted state size: " << st->compacted_state.size << " (allocated " << st->compacted_state.capacity << ")" << std::endl;
	string::toFile("net_compacted_state.bin", st->compacted_state.data, st->compacted_state.size);
	const auto ret = reinterpret_cast<decltype(&init_oodle_network_state_detour)>(init_oodle_network_state_hook.original)(st);
	conout << "htbits: " << st->htbits << std::endl;
	conout << "window size: " << st->window.size << " (allocated " << st->window.capacity << ")" << std::endl;
	string::toFile("net_window.bin", st->window.data, st->window.size);
	return ret;
}

static DetourHook compress_packet_oodle_net_hook;

static bool compress_packet_oodle_net_detour(GameBuffer* uncompressed, GameBuffer* compressed, GameOodleNetworkState* st)
{
	conout << "compress_packet_oodle_net: " << std::string(uncompressed->data, uncompressed->size) << std::endl;
	return reinterpret_cast<decltype(&compress_packet_oodle_net_detour)>(compress_packet_oodle_net_hook.original)(uncompressed, compressed, st);
}

static DetourHook compress_packet_oodle_lz_hook;

static bool compress_packet_oodle_lz_detour(GameBuffer* uncompressed, GameBuffer* compressed, int a3)
{
	conout << "compress_packet_oodle_lz: " << std::string(uncompressed->data, uncompressed->size) << std::endl;
	return reinterpret_cast<decltype(&compress_packet_oodle_lz_detour)>(compress_packet_oodle_lz_hook.original)(uncompressed, compressed, a3);
}

static DetourHook oodle_compress_hook;

static bool oodle_compress_detour(char* out, size_t* out_size, const char* data, size_t size, int a5)
{
	conout << "oodle_compress: " << std::string(data, size) << std::endl;
	return reinterpret_cast<decltype(&oodle_compress_detour)>(oodle_compress_hook.original)(out, out_size, data, size, a5);
}
#endif


#if VERBOSE_SENDCNXLESS
static DetourHook SendConnectionlessData_hook;

static __int64 SendConnectionlessData_detour(void* a1, GameBuffer* data, void* a3, char a4, char a5)
{
	conout << "SendConnectionlessData: " << string::bin2hex(data->data, data->size) << std::endl;
	return reinterpret_cast<decltype(&SendConnectionlessData_detour)>(SendConnectionlessData_hook.original)(a1, data, a3, a4, a5);
}
#endif


#if VERBOSE_LZF
static DetourHook lzf_compress_hook;

static unsigned int lzf_compress_detour(const char* uncompressed, unsigned int uncompressed_size, char* compressed, unsigned int compressed_size)
{
	conout << "lzf_compress input: " << string::bin2hex(uncompressed, uncompressed_size) << std::endl;
	compressed_size = reinterpret_cast<decltype(&lzf_compress_detour)>(lzf_compress_hook.original)(uncompressed, uncompressed_size, compressed, compressed_size);
	conout << "lzf_compress output: " << string::bin2hex(compressed, compressed_size) << std::endl;
	return compressed_size;
}

static DetourHook lzf_decompress_hook;

static unsigned int lzf_decompress_detour(const char* compressed, unsigned int compressed_size, char* uncompressed, unsigned int uncompressed_size)
{
	conout << "lzf_decompress input: " << string::bin2hex(compressed, compressed_size) << std::endl;
	uncompressed_size = reinterpret_cast<decltype(&lzf_decompress_detour)>(lzf_decompress_hook.original)(compressed, compressed_size, uncompressed, uncompressed_size);
	conout << "lzf_decompress output: " << string::bin2hex(uncompressed, uncompressed_size) << std::endl;
	return uncompressed_size;
}
#endif


#if VERBOSE_UNCOMPRESSPKT
struct PacketData
{
	PAD(0x00, 0x08) void* GameOodleNetworkState;
	PAD(0x10, 0x18) GameBuffer uncompressed;
};
#if SOUP_BITS == 64
static_assert(offsetof(PacketData, uncompressed) == 0x18);
#endif

static DetourHook UncompressPacket_hook;

static bool UncompressPacket_detour(PacketData* data, GameBuffer* buffer)
{
	conout << "UncompressPacket input: " << string::bin2hex(buffer->data, buffer->size) << std::endl;
	if (reinterpret_cast<decltype(&UncompressPacket_detour)>(UncompressPacket_hook.original)(data, buffer))
	{
		conout << "UncompressPacket output: " << string::bin2hex(buffer->data, buffer->size) << std::endl;
		return true;
	}
	return false;
}
#endif


#if VERBOSE_PKTCHKSUM
static CompactDetourHook verify_packet_sig_hook;

static bool verify_packet_sig_detour(void* a1, GameRange<const char>* data, GameRange<const char>* salt)
{
	conout << "verify_packet_sig: data=" << string::bin2hex(data->begin, data->end - data->begin) << ", salt=" << string::bin2hex(salt->begin, salt->end - salt->begin) << std::endl;
	return reinterpret_cast<decltype(&verify_packet_sig_detour)>(verify_packet_sig_hook.original)(a1, data, salt);
}
#endif


static ReplacementHook anticheat_sideloading_check_hook;
static ReplacementHook anticheat_timer_check_hook;

static void do_nothing()
{
}


// Called before core dict is initialised!
static bool check_ec(const std::error_code& ec)
{
	if (ec)
	{
		ObfusString msg("Filesystem error. It's likely your anti-virus is interfering; please ensure the game folder is excluded from it.");
		auto title = get_bootstrapper_title();
		MessageBoxA(0, msg.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
		return false;
	}
	return true;
}

static void log_optional_scan_failure(bool important)
{
	if (important)
	{
		conout << get_core_string(ObfusString("sigfailimp").str()) << std::endl;
	}
	else
	{
		conout << get_core_string(ObfusString("sigfailopt").str()) << std::endl;
	}
}

static void report_critical_failure(std::string msg)
{
	int ndlls = 0;
	if (std::filesystem::is_regular_file(ObfusString("wtsapi32.dll").str())) ++ndlls;
	if (std::filesystem::is_regular_file(ObfusString("dwmapi.dll").str())) ++ndlls;
	if (std::filesystem::is_regular_file(ObfusString("version.dll").str())) ++ndlls;
	if (ndlls > 1)
	{
		msg.push_back(' ');
		msg.append(get_core_string(ObfusString("appmdll").str()));
	}

	const auto msg_utf16 = soup::unicode::utf8_to_utf16(msg);
	const auto title_utf16 = soup::unicode::utf8_to_utf16(get_bootstrapper_title());
	MessageBoxW(0, msg_utf16.c_str(), title_utf16.c_str(), MB_OK | MB_ICONERROR);
}

static bool should_setup_optional_conditional_feature(void* ptr)
{
	if (!ptr)
	{
#if PRIVATE
		conout << "A conditional pattern scan has failed. This would be fatal in a public build." << std::endl;
		return false;
#else
		report_critical_failure(get_core_string(ObfusString("sigfailbad").str()));
#endif
	}
	return true;
}

void start_bgscript()
{
	std::string code;
#if PRIVATE
	code = string::fromFile("OpenWF/bgscript.pluto");
	if (code.empty())
#endif
	{
		size_t size;
		auto data = g_repo.find(soup::joaat::compileTimeHash("OpenWF/bgscript.pluto"), size);
		code = std::string(data, size);
	}

	std::lock_guard lock(running_scripts_mtx);
	bgscript = new owfScript();
	bgscript->openBgscriptLibs();
	bgscript->loadString(ObfusString("Background Script"), std::move(code));
	bgscript->tick();
}

void restart_bgscript()
{
	if (bgscript)
	{
		bgscript->stop_requested = true;
		while (bgscript)
		{
			Sleep(10);
		}
	}

	start_bgscript();
}

static void populate_full_script_log(JsonObject& obj)
{
	std::lock_guard lock(script_log_mtx);
	obj.add(ObfusString("script_log"), script_log);
	obj.add(ObfusString("script_log_len"), static_cast<int64_t>(script_log.size()));
}

void populate_full_status(JsonObject& obj)
{
	obj.add(ObfusString("server_host"), server_host);

	obj.add(ObfusString("high_damage_numbers_patch"), high_damage_numbers_patch);
	obj.add(ObfusString("skip_mission_start_timer"), skip_mission_start_timer);
	obj.add(ObfusString("disable_profanity_filter"), disable_profanity_filter);
	obj.add(ObfusString("simulacrum_blacklisted"), simulacrum_blacklisted);
	obj.add(ObfusString("simulacrum_whitelisted"), simulacrum_whitelisted);
	obj.add(ObfusString("pause_always_stops_time"), pause_always_stops_time);
	obj.add(ObfusString("alternative_loading"), alternative_loading);
	obj.add(ObfusString("ee_log_in_console"), ee_log_in_console);

	obj.add(ObfusString("fov_override"), fov_override);

	obj.add(ObfusString("console"), owfConsole::active);

	obj.add(ObfusString("available_scripts"), soup::make_unique<JsonArray>(get_available_scripts()));
	populate_running_scripts(obj);
	populate_autostart_scripts(obj);
	populate_full_script_log(obj);

	{
		std::lock_guard lock(g_server_tunables_mtx);
		populate_server_prohibitions_locked(obj);
	}
}

template <typename T>
static void owf_broadcast_value(std::string name, T value)
{
	JsonObject obj;
	obj.add(std::move(name), value);
	owf_broadcast_message(obj.encode());
}

bool owf_command(const std::string& in, JsonObject& out)
{
	auto args = string::explode(in, '?');
	SOUP_IF_UNLIKELY (args.empty())
	{
		return false;
	}
	switch (joaat::hash(args[0]))
	{
	case joaat::compileTimeHash("logout"):
		do_logout();
		return true;

	case joaat::compileTimeHash("save_config"):
		owfConfig::save();
		return true;

	case joaat::compileTimeHash("reload_hotkeys"):
		load_hotkeys();
		return true;

#if LABEL_REPLACEMENTS
	case joaat::compileTimeHash("reload_label_replacements"):
		if (check_string_substitutions_hook.isCreated())
		{
			load_label_replacements();
		}
		return true;
#endif

#if METADATA_PATCHES && SOUP_BITS == 64
	case joaat::compileTimeHash("reload_metadata_patches"):
		if (object_type_serialise_propery_text_hook.target)
		{
			load_metadata_patches();
		}
		return true;
#endif

	case joaat::compileTimeHash("available_scripts"):
		out.add(ObfusString("available_scripts"), soup::make_unique<JsonArray>(get_available_scripts()));
		return true;

	case joaat::compileTimeHash("running_scripts"):
		populate_running_scripts(out);
		return true;

	case joaat::compileTimeHash("autostart_scripts"):
		populate_autostart_scripts(out);
		return true;

	case joaat::compileTimeHash("script_log"):
		populate_full_script_log(out);
		return true;

	case joaat::compileTimeHash("clear_script_log"):
		{
			std::lock_guard lock(script_log_mtx);
			script_log.clear();
		}
		{
			JsonObject obj;
			populate_full_script_log(obj);
			owf_broadcast_message(obj.encode());
		}
		return true;

	case joaat::compileTimeHash("stop_script"):
		{
			std::lock_guard lock(running_scripts_mtx);
			if (auto scr = get_script_by_name(args.at(1)))
			{
				scr->stop_requested = true;
			}
		}
		return true;

	case soup::joaat::compileTimeHash("high_damage_numbers_patch"):
		if (args.size() > 1)
		{
			high_damage_numbers_patch = (args[1].size() == 4);
			owf_broadcast_value(ObfusString("high_damage_numbers_patch"), high_damage_numbers_patch);
		}
		else
		{
			out.add(ObfusString("high_damage_numbers_patch"), high_damage_numbers_patch);
		}
		return true;

	case soup::joaat::compileTimeHash("skip_mission_start_timer"):
		if (args.size() > 1)
		{
			skip_mission_start_timer = (args[1].size() == 4);
			owf_broadcast_value(ObfusString("skip_mission_start_timer"), skip_mission_start_timer);
		}
		else
		{
			out.add(ObfusString("skip_mission_start_timer"), skip_mission_start_timer);
		}
		return true;

	case soup::joaat::compileTimeHash("disable_profanity_filter"):
		if (args.size() > 1)
		{
			disable_profanity_filter = (args[1].size() == 4);
			owf_broadcast_value(ObfusString("disable_profanity_filter"), disable_profanity_filter);
		}
		else
		{
			out.add(ObfusString("disable_profanity_filter"), disable_profanity_filter);
		}
		return true;

	case soup::joaat::compileTimeHash("simulacrum_blacklisted"):
		if (args.size() > 1)
		{
			simulacrum_blacklisted = (args[1].size() == 4);
			owf_broadcast_value(ObfusString("simulacrum_blacklisted"), simulacrum_blacklisted);
		}
		else
		{
			out.add(ObfusString("simulacrum_blacklisted"), simulacrum_blacklisted);
		}
		return true;

	case soup::joaat::compileTimeHash("simulacrum_whitelisted"):
		if (args.size() > 1)
		{
			simulacrum_whitelisted = (args[1].size() == 4);
			owf_broadcast_value(ObfusString("simulacrum_whitelisted"), simulacrum_whitelisted);
		}
		else
		{
			out.add(ObfusString("simulacrum_whitelisted"), simulacrum_whitelisted);
		}
		return true;

	case soup::joaat::compileTimeHash("alternative_loading"):
		if (args.size() > 1)
		{
			alternative_loading = (args[1].size() == 4);
			owf_broadcast_value(ObfusString("alternative_loading"), alternative_loading);
		}
		else
		{
			out.add(ObfusString("alternative_loading"), alternative_loading);
		}
		return true;

	case soup::joaat::compileTimeHash("ee_log_in_console"):
		if (args.size() > 1)
		{
			ee_log_in_console = (args[1].size() == 4); // Will not take effect in versions where this is not driven by the write_to_log_file hook.
			owf_broadcast_value(ObfusString("ee_log_in_console"), ee_log_in_console);
		}
		else
		{
			out.add(ObfusString("ee_log_in_console"), ee_log_in_console);
		}
		return true;

	case soup::joaat::compileTimeHash("fov_override"):
		if (args.size() > 1)
		{
			fov_override = static_cast<float>(string::toIntOpt<int64_t>(args[1]).value()) / 10000.0f;
			owf_broadcast_value(ObfusString("fov_override"), fov_override);
		}
		else
		{
			out.add(ObfusString("fov_override"), fov_override);
		}
		break;
	}
	return false;
}

static soup::Pattern hash_to_pattern(uint32_t hash)
{
	char data[23];
	data[0] = soup::string::charset_hex[(hash >> 4) & 0xf];
	data[1] = soup::string::charset_hex[(hash >> 0) & 0xf];
	data[2] = ' ';
	data[3] = soup::string::charset_hex[(hash >> 12) & 0xf];
	data[4] = soup::string::charset_hex[(hash >> 8) & 0xf];
	data[5] = ' ';
	data[6] = soup::string::charset_hex[(hash >> 20) & 0xf];
	data[7] = soup::string::charset_hex[(hash >> 16) & 0xf];
	data[8] = ' ';
	data[9] = soup::string::charset_hex[(hash >> 28) & 0xf];
	data[10] = soup::string::charset_hex[(hash >> 24) & 0xf];
	data[11] = ' ';
	data[12] = '0';
	data[13] = '0';
	data[14] = ' ';
	data[15] = '0';
	data[16] = '0';
	data[17] = ' ';
	data[18] = '0';
	data[19] = '0';
	data[20] = ' ';
	data[21] = '0';
	data[22] = '0';
	return Pattern(data, sizeof(data));
}

static soup::Pattern hash_to_pattern(uint32_t hash1, uint32_t hash2)
{
	char data[63];
	data[0] = soup::string::charset_hex[(hash1 >> 4) & 0xf];
	data[1] = soup::string::charset_hex[(hash1 >> 0) & 0xf];
	data[2] = ' ';
	data[3] = soup::string::charset_hex[(hash1 >> 12) & 0xf];
	data[4] = soup::string::charset_hex[(hash1 >> 8) & 0xf];
	data[5] = ' ';
	data[6] = soup::string::charset_hex[(hash1 >> 20) & 0xf];
	data[7] = soup::string::charset_hex[(hash1 >> 16) & 0xf];
	data[8] = ' ';
	data[9] = soup::string::charset_hex[(hash1 >> 28) & 0xf];
	data[10] = soup::string::charset_hex[(hash1 >> 24) & 0xf];
	data[11] = ' ';
	data[12] = '0';
	data[13] = '0';
	data[14] = ' ';
	data[15] = '0';
	data[16] = '0';
	data[17] = ' ';
	data[18] = '0';
	data[19] = '0';
	data[20] = ' ';
	data[21] = '0';
	data[22] = '0';
	data[23] = ' ';
	data[24] = '?';
	data[25] = ' ';
	data[26] = '?';
	data[27] = ' ';
	data[28] = '?';
	data[29] = ' ';
	data[30] = '?';
	data[31] = ' ';
	data[32] = '?';
	data[33] = ' ';
	data[34] = '?';
	data[35] = ' ';
	data[36] = '?';
	data[37] = ' ';
	data[38] = '?';
	data[39] = ' ';
	data[40] = soup::string::charset_hex[(hash2 >> 4) & 0xf];
	data[41] = soup::string::charset_hex[(hash2 >> 0) & 0xf];
	data[42] = ' ';
	data[43] = soup::string::charset_hex[(hash2 >> 12) & 0xf];
	data[44] = soup::string::charset_hex[(hash2 >> 8) & 0xf];
	data[45] = ' ';
	data[46] = soup::string::charset_hex[(hash2 >> 20) & 0xf];
	data[47] = soup::string::charset_hex[(hash2 >> 16) & 0xf];
	data[48] = ' ';
	data[49] = soup::string::charset_hex[(hash2 >> 28) & 0xf];
	data[50] = soup::string::charset_hex[(hash2 >> 24) & 0xf];
	data[51] = ' ';
	data[52] = '0';
	data[53] = '0';
	data[54] = ' ';
	data[55] = '0';
	data[56] = '0';
	data[57] = ' ';
	data[58] = '0';
	data[59] = '0';
	data[60] = ' ';
	data[61] = '0';
	data[62] = '0';
	return Pattern(data, sizeof(data));
}

static SOUP_FORCEINLINE void create_all_hooks()
{
	// 2018.02.22.14.34 (M:8004325165498360760)
	/*{
		SIG_INST("48 89 5C 24 18 55 56 57 48 8D AC 24 00 FA FF FF 48 81 EC 00 07 00 00 48 8B 05 ? ? ? ? 48 33 C4");
		auto legacy_parse_url = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "legacy_parse_url = " << legacy_parse_url << std::endl;
#endif
		SOUP_IF_LIKELY (legacy_parse_url)
		{
			legacy_parse_url_hook.detour = reinterpret_cast<void*>(&legacy_parse_url_detour);
			legacy_parse_url_hook.target = legacy_parse_url;
			legacy_parse_url_hook.create();
			legacy_parse_url_hook.enable();
		}
	}*/

	// 2018.02.22.14.34 (M:8004325165498360760)
	/*{
		SIG_INST("40 53 48 81 EC 60 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 50 02 00 00 4C 8B 49 08");
		auto internet_connect = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "internet_connect = " << internet_connect << std::endl;
#endif
		SOUP_IF_LIKELY (internet_connect)
		{
			internet_connect_hook.detour = reinterpret_cast<void*>(&internet_connect_detour);
			internet_connect_hook.target = internet_connect;
			internet_connect_hook.code_cave = Module(nullptr).range.scan(CompactDetourHook::getCodeCavePattern()).as<void*>();
			internet_connect_hook.create();
			internet_connect_hook.enable();
		}
	}*/

	// 2018.02.22.14.34 (M:8004325165498360760)
	// This breaks update 36
	/*{
		SIG_INST("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 0F B7 01");
		auto resolve_addr = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "resolve_addr = " << resolve_addr << std::endl;
#endif
		SOUP_IF_LIKELY (resolve_addr)
		{
			resolve_addr_hook.detour = reinterpret_cast<void*>(&resolve_addr_detour);
			resolve_addr_hook.target = resolve_addr;
			resolve_addr_hook.create();
			resolve_addr_hook.enable();
		}
	}*/

	/*{
		SIG_INST("48 89 5C 24 18 55 56 57 48 8D AC 24 30 F6 FF FF 48 81 EC D0 0A 00 00");
		auto parse_url = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "parse_url = " << parse_url << std::endl;
#endif
		parse_url_hook.detour = reinterpret_cast<void*>(&parse_url_detour);
		parse_url_hook.target = parse_url;
		parse_url_hook.create();
		parse_url_hook.enable();
	}*/

	{
		void* winhttp_connect;
		if (game_version >= GV(31, 5, 0))
		{
			SIG_INST("40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 68 0C 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 50 0C 00 00");
			winhttp_connect = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else
		{
			SIG_INST("40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 ? ? 00 00 44 0F B7 AC");
			winhttp_connect = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
#if LOGGING
		conout << "winhttp_connect = " << winhttp_connect << std::endl;
#endif
		SOUP_IF_LIKELY (winhttp_connect)
		{
			winhttp_connect_hook.detour = reinterpret_cast<void*>(&winhttp_connect_detour);
			winhttp_connect_hook.target = winhttp_connect;
			winhttp_connect_hook.create();
			winhttp_connect_hook.enable();
		}
		else
		{
			log_optional_scan_failure(game_version >= GV(33, 6, 0));
		}
	}

#if !MINIMAL_HOOKS
	{
		Pointer game_http_request_caller;
		size_t offset;
		if (game_version >= GV(19, 12, 0))
		{
			SIG_INST("48 8D 53 18 E8 ? ? ? ? 48 8D 8B");
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 5;
		}
		else if (game_version >= GV(18, 18, 0))
		{
			SIG_INST("48 8D 53 18 48 8B CF E8 ? ? ? ? 48 8B 05"); // 2016.12.16.14.33, 2016.09.30.12.04, 2016.08.19.17.12
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 8;
		}
		else if (game_version >= GV(18, 4, 0))
		{
			SIG_INST("48 8D 53 18 48 8B CF E8 ? ? ? ? 48 8B 4F 48"); // 2016.03.31.15.16, 2016.03.04.10.06, 2016.02.22.15.37
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 8;
		}
		else if (game_version >= GV(18, 0, 0))
		{
			SIG_INST("48 8D 53 18 48 8B CF 40 88 6A 30 E8"); // 2015.12.05.18.07
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 12;
		}
		else if (game_version >= GV(13, 4, 0))
		{
			SIG_INST("48 8B CF 44 88 72 30 E8"); // 2015.10.21.12.48, 2014.05.23.12.12, 2014.05.21.15.02
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 8;
		}
		else if (game_version >= GV(12, 0, 0))
		{
			SIG_INST("48 8B CE 44 88 62 30 E8"); // 2014.02.07.16.15
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 8;
		}
		else if (game_version >= GV(9, 0, 0))
		{
			SIG_INST("48 8B CE 44 88 62 58 E8"); // 2013.07.15.20.46
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 8;
		}
		else
		{
			SIG_INST("48 8B CF 40 88 6A 58 E8"); // 2013.05.23.16.06
			game_http_request_caller = Module(nullptr).range.scan(sig_inst);
			offset = 8;
		}
#if LOGGING
		conout << "game_http_request_caller = " << game_http_request_caller.as<void*>() << std::endl;
#endif
		SOUP_IF_UNLIKELY (!game_http_request_caller)
		{
			report_critical_failure(get_core_string(ObfusString("sigfailbad").str()));
		}
		auto game_http_request = game_http_request_caller.add(offset).rip().as<void*>();
		GameHttpRequest_body_offset = g_repo.getVersionedU64(soup::joaat::compileTimeHash("OpenWF/vv/off/GameHttpRequest_body.json"), game_version);
		if (game_version >= GV(35, 5, 0))
		{
			game_http_request_hook.detour = reinterpret_cast<void*>(&game_http_request_detour<GameString>);
		}
		else if (game_version >= GV(19, 0, 0))
		{
			game_http_request_hook.detour = reinterpret_cast<void*>(&game_http_request_detour<LegacyGameString>);
		}
		else
		{
			game_http_request_hook.detour = reinterpret_cast<void*>(&game_http_request_detour<LegacyGameStringU18>);
		}
		game_http_request_hook.target = game_http_request;
		game_http_request_hook.code_cave = Module(nullptr).range.scan(CompactDetourHook::getCodeCavePattern()).as<void*>(); // Needed for 2017.03.06.15.49
#if LOGGING
		conout << "game_http_request_hook.code_cave = " << game_http_request_hook.code_cave << std::endl;
#endif
		game_http_request_hook.create();
		game_http_request_hook.enable();
	}
#endif

	// 38.5.0
	/*{
		SIG_INST("74 11 44 38 2D ? ? ? ? 74 08");
		auto WebGet_EncryptPost_cmp = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "WebGet_EncryptPost_cmp = " << WebGet_EncryptPost_cmp.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (WebGet_EncryptPost_cmp)
		{
			auto WebGet_EncryptPost = WebGet_EncryptPost_cmp.add(5).rip().as<bool*>();
			*WebGet_EncryptPost = false;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}*/

#if SOUP_BITS == 64
	// Disable request encryption for 38.5.0 and above
	if (game_version >= GV(38, 5, 0))
	{
		{
			SIG_INST("40 53 57 41 54 48 83 EC 20 44 8B E2 48 8B F9 48 85 C9"); // 38.5.0, 38.5.2, 38.5.3
			auto encstr_append = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
			conout << "encstr_append = " << encstr_append << std::endl;
#endif
			SOUP_IF_LIKELY (encstr_append)
			{
				encstr_append_hook.detour = reinterpret_cast<void*>(&encstr_append_detour);
				encstr_append_hook.target = encstr_append;
				encstr_append_hook.create();
				encstr_append_hook.enable();
			}
		}

		{
			SIG_INST("48 8B C4 48 89 50 10 53 55 41 56 48 83 EC 50 48 89 70 18"); // 38.5.3
			auto encstr_discharge = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
			conout << "encstr_discharge = " << encstr_discharge << std::endl;
#endif
			SOUP_IF_LIKELY (encstr_discharge)
			{
				encstr_discharge_hook.detour = reinterpret_cast<void*>(&encstr_discharge_detour);
				encstr_discharge_hook.target = encstr_discharge;
				//encstr_discharge_hook.create();
				encstr_discharge_hook.enable();
			}
		}

		if (!encstr_discharge_hook.target)
		{
			SIG_INST("48 89 5C 24 18 55 56 57 41 56 41 57 48 83 EC 30 ? ? ? 4C 8B F2"); // 38.5.0, 38.5.2
			auto encstr_discharge = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
			conout << "encstr_discharge = " << encstr_discharge << std::endl;
#endif
			SOUP_IF_LIKELY (encstr_discharge)
			{
				encstr_discharge_hook.detour = reinterpret_cast<void*>(&encstr_discharge_detour);
				encstr_discharge_hook.target = encstr_discharge;
				//encstr_discharge_hook.create();
				encstr_discharge_hook.enable();
			}
		}

		SOUP_IF_UNLIKELY (!encstr_append_hook.target || !encstr_discharge_hook.target)
		{
			report_critical_failure(get_core_string(ObfusString("sigfailenc").str()));
		}
	}
#endif

	// 38.5.0
	/*{
		SIG_INST("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 30 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 20 01 00 00");
		auto queue_http_request_internal = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "queue_http_request_internal = " << queue_http_request_internal << std::endl;
#endif
		SOUP_IF_LIKELY (queue_http_request_internal)
		{
			queue_http_request_internal_hook.detour = reinterpret_cast<void*>(&queue_http_request_internal_detour);
			queue_http_request_internal_hook.target = queue_http_request_internal;
			queue_http_request_internal_hook.create();
			queue_http_request_internal_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}*/

#if PRIVATE || MINIMAL_HOOKS
	{
		// "Hostname %s was found in DNS cache"
		void* Curl_resolv;
		if (game_version >= GV(41, 0, 0))
		{
			SIG_INST("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 0F 48 8B 45 7F 48 8B F9");
			Curl_resolv = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else if (game_version >= GV(37, 0, 0))
		{
			SIG_INST("40 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 E1 48 81 EC A0 00 00 00 48 8B 05");
			Curl_resolv = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else
		{
			SIG_INST("48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 40 48 8B 39");
			Curl_resolv = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
#if LOGGING
		conout << "Curl_resolv = " << Curl_resolv << std::endl;
#endif
		SOUP_IF_LIKELY (Curl_resolv)
		{
			Curl_resolv_hook.detour = reinterpret_cast<void*>(&Curl_resolv_detour);
			Curl_resolv_hook.target = Curl_resolv;
			Curl_resolv_hook.create();
			Curl_resolv_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

	if (auto sig_inst = g_repo.getVersionedPattern(soup::joaat::compileTimeHash("OpenWF/vv/sig/ssl_verify_internal_caller.json"), game_version); !sig_inst.bytes.empty())
	{
		auto ssl_verify_internal_caller = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "ssl_verify_internal_caller = " << ssl_verify_internal_caller.as<void*>() << std::endl;
#endif
		SOUP_IF_UNLIKELY (!ssl_verify_internal_caller)
		{
			report_critical_failure(get_core_string(ObfusString("sigfailbad").str()));
		}
		auto ssl_verify_internal = ssl_verify_internal_caller.add(7).rip().as<void*>();
		ssl_verify_internal_hook.detour = reinterpret_cast<void*>(&ssl_verify_internal_detour);
		ssl_verify_internal_hook.target = ssl_verify_internal;
		//ssl_verify_internal_hook.create();
		ssl_verify_internal_hook.enable();
	}

	if (!strip_tls)
	{
		if (auto sig_inst = g_repo.getVersionedPattern(soup::joaat::compileTimeHash("OpenWF/vv/sig/Curl_ossl_verifyhost.json"), game_version); !sig_inst.bytes.empty())
		{
			auto Curl_ossl_verifyhost = Module(nullptr).range.scan(sig_inst).as<void*>();
	#if LOGGING
			conout << "Curl_ossl_verifyhost = " << Curl_ossl_verifyhost << std::endl;
	#endif
			SOUP_IF_UNLIKELY (!Curl_ossl_verifyhost)
			{
				report_critical_failure(get_core_string(ObfusString("sigfailbad").str()));
			}
			Curl_ossl_verifyhost_hook.detour = reinterpret_cast<void*>(&Curl_ossl_verifyhost_detour);
			Curl_ossl_verifyhost_hook.target = Curl_ossl_verifyhost;
			//Curl_ossl_verifyhost_hook.create();
			Curl_ossl_verifyhost_hook.enable();
		}
	}

#if DISABLE_WSINTCHK
	// This hook allows WorldSeed to be absent or just any value.
	if (auto sig_inst = g_repo.getVersionedPattern(soup::joaat::compileTimeHash("OpenWF/vv/sig/verify_worldstate_integrity.json"), game_version); !sig_inst.bytes.empty())
	{
		auto verify_worldstate_integrity = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "verify_worldstate_integrity = " << verify_worldstate_integrity << std::endl;
#endif
		SOUP_IF_UNLIKELY (!verify_worldstate_integrity)
		{
			report_critical_failure(get_core_string(ObfusString("sigfailbad").str()));
		}
		verify_worldstate_integrity_hook.detour = reinterpret_cast<void*>(&verify_worldstate_integrity_detour);
		verify_worldstate_integrity_hook.target = verify_worldstate_integrity;
		verify_worldstate_integrity_hook.code_cave = Module(nullptr).range.scan(CompactDetourHook::getCodeCavePattern()).as<void*>();
#if LOGGING
		conout << "verify_worldstate_integrity_hook.code_cave = " << verify_worldstate_integrity_hook.code_cave<< std::endl;
#endif
		verify_worldstate_integrity_hook.create();
		verify_worldstate_integrity_hook.enable();
	}
#endif

	// This hook allows any WorldSeed be considered valid.
	/*{
		SIG_INST("48 89 5C 24 10 48 89 6C 24 18 56 41 54 41 55 41 56 41 57 48 83 EC 40 48 8B AC 24 A8 00 00 00");
		auto int_rsa_verify = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "int_rsa_verify = " << int_rsa_verify << std::endl;
#endif
		int_rsa_verify_hook.detour = reinterpret_cast<void*>(&int_rsa_verify_detour);
		int_rsa_verify_hook.target = int_rsa_verify;
		int_rsa_verify_hook.create();
		int_rsa_verify_hook.enable();
	}*/

#if !MINIMAL_HOOKS
	{
		Pointer parse_arguments_callsite;
		if (game_version >= GV(40, 0, 0))
		{
			SIG_INST("48 8D 0D ? ? ? ? 49 8D 43 E8 49 89 43 E8 49 8D 43 E8 49 89 43 F0 E8");
			parse_arguments_callsite = Module(nullptr).range.scan(sig_inst);
		}
		else if (game_version >= GV(19, 0, 0))
		{
			SIG_INST("48 8D 0D ? ? ? ? 49 8D 43 ? 49 89 43 ? 49 8D 43 ? 49 89 43 ? E8"); // 2019.05.22.23.12 (has 2 matches in U40)
			parse_arguments_callsite = Module(nullptr).range.scan(sig_inst);
		}
		else
		{
			// Also made this 24 bytes just to match the above pattern
			SIG_INST("89 43 ? 49 8D 43 ? 49 89 ? ? 49 89 43 ? 49 8D 43 ? 49 89 43 ? E8"); // 2016.09.30.12.04
			parse_arguments_callsite = Module(nullptr).range.scan(sig_inst);
		}
#if LOGGING
		conout << "parse_arguments_callsite = " << parse_arguments_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (parse_arguments_callsite)
		{
			auto parse_arguments = parse_arguments_callsite.add(24).rip().as<void*>();

			/*if (game_version >= GV(39, 0, 0))
			{
				parse_arguments_hook.detour = reinterpret_cast<void*>(&parse_arguments_detour<GameString, true, true>);
			}
			else*/ if (game_version >= GV(35, 5, 0))
			{
				parse_arguments_hook.detour = reinterpret_cast<void*>(&parse_arguments_detour<GameString/*, false, true*/>);
			}
			/*else if (game_version >= GV(28, 0, 0))
			{
				parse_arguments_hook.detour = reinterpret_cast<void*>(&parse_arguments_detour<LegacyGameString, false, true>);
			}*/
			else if (game_version >= GV(19, 0, 0))
			{
				parse_arguments_hook.detour = reinterpret_cast<void*>(&parse_arguments_detour<LegacyGameString/*, false, false*/>);
			}
			else
			{
				parse_arguments_hook.detour = reinterpret_cast<void*>(&parse_arguments_detour<LegacyGameStringU18/*, false, false*/>);
			}
			parse_arguments_hook.target = parse_arguments;
			parse_arguments_hook.create();
			parse_arguments_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if false
	{
		SIG_INST("48 03 0D ? ? ? ? 48 89 8F");
		auto worldstate_update_interval_insn = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "worldstate_update_interval_insn = " << worldstate_update_interval_insn.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (worldstate_update_interval_insn)
		{
			*worldstate_update_interval_insn.add(3).rip().as<uint64_t*>() = 0; // default: 300
		}
	}
#endif

	// Emulate a non-stripped build so that no H.Cache is needed (breaks dialogue)
	// Needed for versions prior to echoes of duviri. Doesn't seem to cause any issues.
	if (game_version < GV(33, 6, 0))
	{
		SIG_INST("88 44 24 20 E8 ? ? ? ? 83 7B 0C 01"); // 2013.05.23.16.06, 2014.10.24.08.24, 2017.03.06.15.49, 2023.04.25.23.40
		const auto init_cache_fetching_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "init_cache_fetching_callsite = " << init_cache_fetching_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (init_cache_fetching_callsite)
		{
			auto init_cache_fetching = init_cache_fetching_callsite.add(5).rip().as<void*>();

			init_cache_fetching_hook.detour = reinterpret_cast<void*>(&init_cache_fetching_detour);
			init_cache_fetching_hook.target = init_cache_fetching;
			init_cache_fetching_hook.create();
			init_cache_fetching_hook.enable();
		}
		else
		{
			log_optional_scan_failure(true);
		}
	}

	if (game_version < GV(33, 0, 0))
	{
		void* name_lookup;
		/*if (game_version >= GV(35, 5, 0))
		{
			SIG_INST("40 55 57 41 56 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F9"); // U38
			name_lookup = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else*/ if (game_version >= GV(14, 0, 0))
		{
			SIG_INST("40 55 56 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? C6 41 06 01"); // 2016.12.16.14.33, 2014.07.21.18.38
			name_lookup = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else if (game_version >= GV(13, 4, 0))
		{
			SIG_INST("40 55 56 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 83 7A 08 00"); // 2014.05.23.12.12
			name_lookup = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else if (game_version >= GV(11, 0, 0))
		{
			SIG_INST("48 89 5C 24 20 55 56 41 54 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 83 7A 08 00"); // 2013.11.29.16.33
			name_lookup = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else
		{
			SIG_INST("40 55 53 41 54 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 83 7A 08 00"); // 2013.11.12.14.03
			name_lookup = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
#if LOGGING
		conout << "name_lookup = " << name_lookup << std::endl;
#endif
		SOUP_IF_LIKELY (name_lookup)
		{
			/*if (game_version >= GV(35, 5, 0))
			{
				name_lookup_hook.detour = reinterpret_cast<void*>(&name_lookup_detour<GameString>);
			}
			else*/ if (game_version >= GV(19, 0, 0))
			{
				name_lookup_hook.detour = reinterpret_cast<void*>(&name_lookup_detour<LegacyGameString>);
			}
			else
			{
				name_lookup_hook.detour = reinterpret_cast<void*>(&name_lookup_detour<LegacyGameStringU18>);
			}
			name_lookup_hook.target = name_lookup;
			name_lookup_hook.create();
			name_lookup_hook.enable();
		}
		else
		{
			conout << get_core_string(ObfusString("sigfaillegacy").str()) << std::endl;
		}
	}

	if (game_version >= GV(37, 0, 0))
	{
		SIG_INST("48 C1 C8 ? 48 89 05 ? ? ? ? 48 33 C1 48 89 05 ? ? ? ? C3"); // Alternatively: CC 48 B9 ? ? ? ? ? ? ? ? 48 8D 05
		auto device_id_insn = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "device_id_insn = " << device_id_insn.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(device_id_insn.as<void*>()))
		{
			//device_id_mask = device_id_insn.add(3).as<uint64_t&>();
			device_id_ptr = device_id_insn.add(7).rip().as<uint64_t*>();
		}
	}

#if DISABLE_XP_BASED_LEVEL_CAPPING
	if (game_version >= GV(35, 5, 0))
	{
		SIG_INST("73 43 B2 05");
		auto xp_based_level_jnb = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "xp_based_level_jnb = " << xp_based_level_jnb.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(xp_based_level_jnb.as<void*>()))
		{
			memGuard::setAllowedAccess(xp_based_level_jnb.as<void*>(), 1, memGuard::ACC_RWX);
			*xp_based_level_jnb.as<uint8_t*>() = 0xEB; // jnb -> jmp
			disabled_xp_based_level_cap = true;
		}
	}
#endif

	/*{
		//SIG_INST("48 89 5C 24 18 48 89 74 24 20 57 48 83 EC 50 0F 29 74 24 40 0F 57 C0 0F 28 F1");
		SIG_INST("48 89 5C 24 10 57 48 83 EC 50 0F 29 74 24 40 0F 57 C0 0F 28 F1");
		auto SquadSetCountdownTimer = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "SquadSetCountdownTimer = " << SquadSetCountdownTimer << std::endl;
#endif
		SOUP_IF_LIKELY (SquadSetCountdownTimer)
		{
			SquadSetCountdownTimer_hook.detour = reinterpret_cast<void*>(&SquadSetCountdownTimer_detour);
			SquadSetCountdownTimer_hook.target = SquadSetCountdownTimer;
			SquadSetCountdownTimer_hook.create();
			SquadSetCountdownTimer_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}*/

#if !MINIMAL_HOOKS
	{
		ObfusString str("SquadSetCountdownTimer");
		auto lua_SquadSetCountdownTimer_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_SquadSetCountdownTimer_hash = " << lua_SquadSetCountdownTimer_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_SquadSetCountdownTimer_hash)
		{
			auto lua_SquadSetCountdownTimer_fp = lua_SquadSetCountdownTimer_hash.add(8).as<luau_CFunction*>();
			lua_SquadSetCountdownTimer_og = *lua_SquadSetCountdownTimer_fp;
			memGuard::setAllowedAccess(lua_SquadSetCountdownTimer_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_SquadSetCountdownTimer_fp = lua_SquadSetCountdownTimer_detour;
		}
		else
		{
			conout << get_core_string(ObfusString("sigfailsmst").str()) << std::endl;
		}
	}
#endif

#if !MINIMAL_HOOKS
	{
		ObfusString str("SanitizeText");
		auto lua_SanitizeText_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_SanitizeText_hash = " << lua_SanitizeText_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_SanitizeText_hash)
		{
			auto lua_SanitizeText_fp = lua_SanitizeText_hash.add(8).as<luau_CFunction*>();
			lua_SanitizeText_og = *lua_SanitizeText_fp;
			memGuard::setAllowedAccess(lua_SanitizeText_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_SanitizeText_fp = lua_SanitizeText_detour;
		}
		else
		{
			conout << get_core_string(ObfusString("sigfaildpf").str()) << std::endl;
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (game_version >= GV(36, 0, 0) && game_version < GV(41, 1, 0))
	{
		{
			SIG_INST("48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ? FE FF FF 48 81 EC ? 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 98 68 FF FF FF 44 0F 29 A0 58 FF FF FF 44 0F 29 A8 48 FF FF FF 44 0F 29 B0 38 FF FF FF 44 0F 29 B8 28 FF FF FF");
			auto get_total_damage = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "get_total_damage = " << get_total_damage << std::endl;
#endif
			SOUP_IF_LIKELY (get_total_damage)
			{
				get_total_damage_hook.detour = reinterpret_cast<void*>(&get_total_damage_detour);
				get_total_damage_hook.target = get_total_damage;
				get_total_damage_hook.create();
				get_total_damage_hook.enable();
			}
		}

		if (game_version >= GV(41, 1, 0))
		{
		}
		else if (game_version >= GV(40, 0, 0))
		{
			SIG_INST("66 41 0F 6E F6 0F 5B F6 0F 84"); // "66 41 0F 6E F6 0F 5B F6 0F" works in 41.1.0 but the hook doesn't have the desired effect.
			const Pointer dmg_number_patch_addr = get_total_damage_hook.target ? Module(nullptr).range.scan(sig_inst) : nullptr;
#if LOGGING
			conout << "dmg_number_patch_addr = " << dmg_number_patch_addr.as<void*>() << std::endl;
#endif
			SOUP_IF_LIKELY (should_setup_optional_conditional_feature(dmg_number_patch_addr.as<void*>()))
			{
				uint8_t detour_bytes[] = {
					// prepare call
					/*  0 */ 0x44, 0x89, 0xF1, // mov ecx, r14d
					/*  3 */ 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)

					/* 13 */ 0x74, (34 - 15), // if compact numbers are off, jump to the appropriate branch

					// compact numbers on
					/* 15 */ 0x41, 0xFF, 0xD2, // call r10
					/* 18 */ 0x0F, 0x28, 0xF0, // movaps xmm6, xmm0
					/* 21 */ 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)
					/* 31 */ 0x41, 0xFF, 0xE2, // jmp r10

					// compact numbers off
					/* 34 */ 0x41, 0xFF, 0xD2, // call r10
					/* 37 */ 0x0F, 0x28, 0xF0, // movaps xmm6, xmm0
					/* 40 */ 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)
					/* 50 */ 0x41, 0xFF, 0xE2, // jmp r10
				};
				static_assert(sizeof(detour_bytes) == 50 + 3);
				*(void**)(detour_bytes + 3 + 2) = reinterpret_cast<void*>(&get_dmg_to_display);
				*(void**)(detour_bytes + 21 + 2) = dmg_number_patch_addr.add(17).as<void*>(); // no jump at jz = compact numbers on -> go to `call log10f`
				*(void**)(detour_bytes + 40 + 2) = dmg_number_patch_addr.add(10).rip().as<void*>(); // jumped at jz = compact numbers off -> go to branch

				void* detour = memGuard::alloc(sizeof(detour_bytes), memGuard::ACC_RWX);
				memcpy(detour, detour_bytes, sizeof(detour_bytes));

				uint8_t trampoline[] = {
					0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)
					0x41, 0xff, 0xe2, // jmp r10
				};
				*(void**)(trampoline + 2) = detour;
				memGuard::setAllowedAccess(dmg_number_patch_addr.as<void*>(), sizeof(trampoline), memGuard::ACC_RWX);
				memcpy(dmg_number_patch_addr.as<void*>(), trampoline, sizeof(trampoline));
			}
		}
		else if (game_version >= GV(36, 0, 0))
		{
			SIG_INST("66 41 0F 6E F4 0F 5B F6 0F 84");
			const Pointer dmg_number_patch_addr = get_total_damage_hook.target ? Module(nullptr).range.scan(sig_inst) : nullptr;
#if LOGGING
			conout << "dmg_number_patch_addr = " << dmg_number_patch_addr.as<void*>() << std::endl;
#endif
			SOUP_IF_LIKELY (should_setup_optional_conditional_feature(dmg_number_patch_addr.as<void*>()))
			{
				uint8_t detour_bytes[] = {
					// prepare call
					/*  0 */ 0x44, 0x89, 0xE1, // mov ecx, r12d
					/*  3 */ 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)

					/* 13 */ 0x74, (34 - 15), // if compact numbers are off, jump to the appropriate branch

					// compact numbers on
					/* 15 */ 0x41, 0xFF, 0xD2, // call r10
					/* 18 */ 0x0F, 0x28, 0xF0, // movaps xmm6, xmm0
					/* 21 */ 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)
					/* 31 */ 0x41, 0xFF, 0xE2, // jmp r10

					// compact numbers off
					/* 34 */ 0x41, 0xFF, 0xD2, // call r10
					/* 37 */ 0x0F, 0x28, 0xF0, // movaps xmm6, xmm0
					/* 40 */ 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)
					/* 50 */ 0x41, 0xFF, 0xE2, // jmp r10
				};
				static_assert(sizeof(detour_bytes) == 50 + 3);
				*(void**)(detour_bytes + 3 + 2) = reinterpret_cast<void*>(&get_dmg_to_display);
				*(void**)(detour_bytes + 21 + 2) = dmg_number_patch_addr.add(17).as<void*>(); // no jump at jz = compact numbers on -> go to `call log10f`
				*(void**)(detour_bytes + 40 + 2) = dmg_number_patch_addr.add(10).rip().as<void*>(); // jumped at jz = compact numbers off -> go to branch

				void* detour = memGuard::alloc(sizeof(detour_bytes), memGuard::ACC_RWX);
				memcpy(detour, detour_bytes, sizeof(detour_bytes));

				uint8_t trampoline[] = {
					0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)
					0x41, 0xff, 0xe2, // jmp r10
				};
				*(void**)(trampoline + 2) = detour;
				memGuard::setAllowedAccess(dmg_number_patch_addr.as<void*>(), sizeof(trampoline), memGuard::ACC_RWX);
				memcpy(dmg_number_patch_addr.as<void*>(), trampoline, sizeof(trampoline));
			}
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (disable_firewall_prompt)
	{
		SIG_INST("66 69 72 65 77 61 6C 6C 00"); // "firewall"
		auto pStrFirewall = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "pStrFirewall = " << pStrFirewall.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (pStrFirewall)
		{
			memGuard::setAllowedAccess(pStrFirewall.as<void*>(), 8, memGuard::ACC_RWX);
			ObfusString str("nominal");
			strcpy(pStrFirewall.as<char*>(), str.c_str());
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (game_version >= GV(23, 10, 0)) // Seems to match something unexpected in 2018.06.14.23.21 & 2018.02.22.14.34
	{
		// "Sys [Error]: Could not write to "
		SIG_INST("48 8B 0D ? ? ? ? 48 85 C9 74 14 41 B8 20 00 00 00 48 8D 15 ? ? ? ? E8");
		auto write_to_log_file_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "write_to_log_file_callsite = " << write_to_log_file_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (write_to_log_file_callsite)
		{
			write_to_log_file_hook.detour = reinterpret_cast<void*>(&write_to_log_file_detour);
			write_to_log_file_hook.target = write_to_log_file_callsite.add(26).rip().as<void*>();
			write_to_log_file_hook.create();
			write_to_log_file_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (game_version >= GV(33, 0, 0))
	{
		ObfusString str_GetConfigBool("GetConfigBool");
		ObfusString str_SetConfigBool("SetConfigBool");
		auto lua_FlashMgr_GetConfigBool_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str_GetConfigBool.c_str()), wf_hash(str_SetConfigBool.c_str())));
#if LOGGING
		conout << "lua_FlashMgr_GetConfigBool_hash = " << lua_FlashMgr_GetConfigBool_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_FlashMgr_GetConfigBool_hash)
		{
			auto lua_FlashMgr_GetConfigBool_fp = lua_FlashMgr_GetConfigBool_hash.add(8).as<luau_CFunction*>();
			lua_FlashMgr_GetConfigBool_og = *lua_FlashMgr_GetConfigBool_fp;
			memGuard::setAllowedAccess(lua_FlashMgr_GetConfigBool_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_FlashMgr_GetConfigBool_fp = lua_FlashMgr_GetConfigBool_detour;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (game_version >= GV(40, 0, 0))
	{
		SIG_INST("49 8B 4E 20 BA ? ? ? ? E8 ? ? ? ? 49 8B 4E 20 E8");
		auto lua_set_global_by_hash_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "lua_set_global_by_hash_callsite = " << lua_set_global_by_hash_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_set_global_by_hash_callsite)
		{
			auto lua_set_global_by_hash = lua_set_global_by_hash_callsite.add(10).rip().as<void*>();

			lua_set_global_by_hash_hook.detour = reinterpret_cast<void*>(&lua_set_global_by_hash_detour);
			lua_set_global_by_hash_hook.target = lua_set_global_by_hash;
			lua_set_global_by_hash_hook.code_cave = Module(nullptr).range.scan(CompactDetourHook::getCodeCavePattern()).as<void*>();
#if LOGGING
			conout << "lua_set_global_by_hash_hook.code_cave = " << lua_set_global_by_hash_hook.code_cave<< std::endl;
#endif
			lua_set_global_by_hash_hook.create();
			lua_set_global_by_hash_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		void* lua_set_global;
		if (game_version >= GV(35, 5, 0))
		{
			SIG_INST("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 F6 41 01 04 48 8B FA");
			lua_set_global = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else
		{
			SIG_INST("48 89 5C 24 08 57 48 83 EC 20 F6 41 01 04 48 8B FA 48 8B D9"); // U35.1
			lua_set_global = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
#if LOGGING
		conout << "lua_set_global = " << lua_set_global << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(lua_set_global))
		{
			lua_set_global_hook.detour = reinterpret_cast<void*>(&lua_set_global_detour);
			lua_set_global_hook.target = lua_set_global;
			lua_set_global_hook.create();
			lua_set_global_hook.enable();
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (have_scripting)
	{
		ObfusString str("UpdateFlashMarkers");
		auto lua_LotusHudStatus_UpdateFlashMarkers_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_LotusHudStatus_UpdateFlashMarkers_hash = " << lua_LotusHudStatus_UpdateFlashMarkers_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(lua_LotusHudStatus_UpdateFlashMarkers_hash.as<void*>()))
		{
			auto lua_LotusHudStatus_UpdateFlashMarkers_fp = lua_LotusHudStatus_UpdateFlashMarkers_hash.add(8).as<luau_CFunction*>();
			lua_LotusHudStatus_UpdateFlashMarkers_og = *lua_LotusHudStatus_UpdateFlashMarkers_fp;
			memGuard::setAllowedAccess(lua_LotusHudStatus_UpdateFlashMarkers_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_LotusHudStatus_UpdateFlashMarkers_fp = lua_LotusHudStatus_UpdateFlashMarkers_detour;
		}
	}
#endif

	if (game_version >= GV(40, 0, 0))
	{
#if !MINIMAL_HOOKS
		SIG_INST("49 8D 43 D8 49 89 43 F0 E8 ? ? ? ? 48 8D 0D ? ? ? ? 48 83 C4 58 E9");
		auto register_enum_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "register_enum_callsite = " << register_enum_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(register_enum_callsite.as<void*>()))
		{
			auto register_enum = register_enum_callsite.add(9).rip().as<void*>();

			register_enum_hook.detour = reinterpret_cast<void*>(&register_enum_detour);
			register_enum_hook.target = register_enum;
			register_enum_hook.create();
			register_enum_hook.enable();
		}
#endif
	}

#if !MINIMAL_HOOKS
	if ((!forced_profile_dir.empty() || PRIVATE)
		&& game_version >= GV(35, 5, 0)
		)
	{
		// "Using profile dir "
		Pointer get_profile_dir;
		size_t offset_offset;
		if (game_version >= GV(40, 0, 0))
		{
			SIG_INST("40 55 53 56 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8D 99 ? ? ? ? 48 8B F1");
			get_profile_dir = Module(nullptr).range.scan(sig_inst);
			offset_offset = (0x0000000140EB0D24 - 0x0000000140EB0D00) + 3;
		}
		else
		{
			SIG_INST("40 55 53 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 0F B6 81 ? ? ? ? 48 8D 99");
			get_profile_dir = Module(nullptr).range.scan(sig_inst);
			offset_offset = 46;
		}
#if LOGGING
		conout << "get_profile_dir = " << get_profile_dir.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (get_profile_dir)
		{
			if (!forced_profile_dir.empty())
			{
				get_profile_dir_hook.detour = reinterpret_cast<void*>(&get_profile_dir_detour);
				get_profile_dir_hook.target = get_profile_dir.as<void*>();
				//get_profile_dir_hook.create();
				get_profile_dir_hook.enable();
			}
			get_profile_dir_offset = get_profile_dir.add(offset_offset).as<uint32_t&>();
#if LOGGING
			conout << "get_profile_dir_offset = " << get_profile_dir_offset << std::endl;
#endif
		}
		else
		{
			conout << get_core_string(ObfusString("sigfailfpd").str()) << std::endl;
		}
	}
#endif

#if !MINIMAL_HOOKS
	{
		ObfusString str("excludedFromSimulacrum");
		auto excludedFromSimulacrum_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "excludedFromSimulacrum_hash = " << excludedFromSimulacrum_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (excludedFromSimulacrum_hash)
		{
			auto lua_AvatarEntry_excludedFromSimulacrum_get = *excludedFromSimulacrum_hash.add(8).as<void**>();
#if LOGGING
			conout << "lua_AvatarEntry_excludedFromSimulacrum_get = " << lua_AvatarEntry_excludedFromSimulacrum_get << std::endl;
#endif
			lua_AvatarEntry_excludedFromSimulacrum_get_hook.detour = reinterpret_cast<void*>(&lua_AvatarEntry_excludedFromSimulacrum_get_detour);
			lua_AvatarEntry_excludedFromSimulacrum_get_hook.target = lua_AvatarEntry_excludedFromSimulacrum_get;
			lua_AvatarEntry_excludedFromSimulacrum_get_hook.code_cave = Module(nullptr).range.scan(CompactDetourHook::getCodeCavePattern()).as<void*>();
#if LOGGING
			conout << "lua_AvatarEntry_excludedFromSimulacrum_get_hook.code_cave = " << lua_AvatarEntry_excludedFromSimulacrum_get_hook.code_cave << std::endl;
#endif
			lua_AvatarEntry_excludedFromSimulacrum_get_hook.create();
			lua_AvatarEntry_excludedFromSimulacrum_get_hook.enable();
		}
		else
		{
			conout << get_core_string(ObfusString("sigfailswb").str()) << std::endl;
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (game_version >= GV(37, 0, 0))
	{
		SIG_INST("48 89 5C 24 10 48 89 74 24 18 57 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B D9 E8 ? ? ? ? 48 8B C8"); // U37, U38, U41
		auto is_pause_allowed = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "is_pause_allowed = " << is_pause_allowed << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(is_pause_allowed))
		{
			is_pause_allowed_hook.detour = reinterpret_cast<void*>(&is_pause_allowed_detour);
			is_pause_allowed_hook.target = is_pause_allowed;
			is_pause_allowed_hook.create();
			is_pause_allowed_hook.enable();
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (game_version >= GV(33, 0, 0)) // U32 Veilbreaker (2022.09.06.19.24) seems to crash in this detour
	{
		ObfusString str("GetStringVariable");
		auto lua_FlashInstance_GetStringVariable_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_FlashInstance_GetStringVariable_hash = " << lua_FlashInstance_GetStringVariable_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(lua_FlashInstance_GetStringVariable_hash.as<void*>()))
		{
			auto lua_FlashInstance_GetStringVariable_fp = lua_FlashInstance_GetStringVariable_hash.add(8).as<luau_CFunction*>();
			lua_FlashInstance_GetStringVariable_og = *lua_FlashInstance_GetStringVariable_fp;
			memGuard::setAllowedAccess(lua_FlashInstance_GetStringVariable_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_FlashInstance_GetStringVariable_fp = lua_FlashInstance_GetStringVariable_detour;
		}
	}
#endif

#if !MINIMAL_HOOKS
	{
		ObfusString str("OpenWebBrowser");
		auto lua_OpenWebBrowser_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_OpenWebBrowser_hash = " << lua_OpenWebBrowser_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_OpenWebBrowser_hash)
		{
			auto lua_OpenWebBrowser_fp = lua_OpenWebBrowser_hash.add(8).as<luau_CFunction*>();
			lua_OpenWebBrowser_og = *lua_OpenWebBrowser_fp;
			memGuard::setAllowedAccess(lua_OpenWebBrowser_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_OpenWebBrowser_fp = lua_OpenWebBrowser_detour;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if VERBOSE_RNG
	{
		SIG_INST("48 89 1D ? ? ? ? 85 C0 74 27 48 B9 2D 7F 95 4C 2D F4 51 58");
		auto lua_seed_mov = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "lua_seed_mov = " << lua_seed_mov.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_seed_mov)
		{
			lua_seed = lua_seed_mov.add(3).rip().as<int64_t*>();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		ObfusString str("SetSeed");
		auto lua_SetSeed_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_SetSeed_hash = " << lua_SetSeed_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_SetSeed_hash)
		{
			auto lua_SetSeed_fp = lua_SetSeed_hash.add(8).as<luau_CFunction*>();
			lua_SetSeed_og = *lua_SetSeed_fp;
			memGuard::setAllowedAccess(lua_SetSeed_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_SetSeed_fp = lua_SetSeed_detour;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		ObfusString str("ChurnSeed");
		auto lua_ChurnSeed_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_ChurnSeed_hash = " << lua_ChurnSeed_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_ChurnSeed_hash)
		{
			auto lua_ChurnSeed_fp = lua_ChurnSeed_hash.add(8).as<luau_CFunction*>();
			lua_ChurnSeed_og = *lua_ChurnSeed_fp;
			memGuard::setAllowedAccess(lua_ChurnSeed_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_ChurnSeed_fp = lua_ChurnSeed_detour;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		ObfusString str("SRandom");
		auto lua_SRandom_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_SRandom_hash = " << lua_SRandom_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_SRandom_hash)
		{
			auto lua_SRandom_fp = lua_SRandom_hash.add(8).as<luau_CFunction*>();
			lua_SRandom_og = *lua_SRandom_fp;
			memGuard::setAllowedAccess(lua_SRandom_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_SRandom_fp = lua_SRandom_detour;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		ObfusString str("SRandomInt");
		auto lua_SRandomInt_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_SRandomInt_hash = " << lua_SRandomInt_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_SRandomInt_hash)
		{
			auto lua_SRandomInt_fp = lua_SRandomInt_hash.add(8).as<luau_CFunction*>();
			lua_SRandomInt_og = *lua_SRandomInt_fp;
			memGuard::setAllowedAccess(lua_SRandomInt_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_SRandomInt_fp = lua_SRandomInt_detour;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		ObfusString str("HashCrc32");
		auto lua_HashCrc32_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_HashCrc32_hash = " << lua_HashCrc32_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_HashCrc32_hash)
		{
			auto lua_HashCrc32_fp = lua_HashCrc32_hash.add(8).as<luau_CFunction*>();
			lua_HashCrc32_og = *lua_HashCrc32_fp;
			memGuard::setAllowedAccess(lua_HashCrc32_fp, sizeof(void*), memGuard::ACC_READ | memGuard::ACC_WRITE);
			*lua_HashCrc32_fp = lua_HashCrc32_detour;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if VERBOSE_CRC32
	{
		SIG_INST("48 89 5C 24 10 48 89 6C 24 18 57 48 8D 2D ? ? ? ? 49 8B F8"); // 2022.04.29.12.53
		//SIG_INST("40 57 48 8D 3D ? ? ? ? 4D 8B D8 4C 8B D2 F7 D1"); // 2019.10.31.22.42
		//SIG_INST("40 57 4D 8B D8 4C 8B D2 F7 D1 48 8D 3D"); // 2015.12.09.17.09
		auto crc32_impl = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "crc32_impl = " << crc32_impl << std::endl;
#endif
		SOUP_IF_LIKELY (crc32_impl)
		{
			crc32_impl_hook.detour = reinterpret_cast<void*>(&crc32_impl_detour);
			crc32_impl_hook.target = crc32_impl;
			//crc32_impl_hook.create();
			crc32_impl_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if VERBOSE_CRC32C
	{
		SIG_INST("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 83 3D");
		auto crc32c_impl = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "crc32c_impl = " << crc32c_impl << std::endl;
#endif
		SOUP_IF_LIKELY (crc32c_impl)
		{
			crc32c_impl_hook.detour = reinterpret_cast<void*>(&crc32c_impl_detour);
			crc32c_impl_hook.target = crc32c_impl;
			crc32c_impl_hook.create();
			crc32c_impl_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if VERBOSE_MD5
	{
		SIG_INST("4D 85 C0 0F 84 ? 00 00 00 48 89 6C 24 10");
		auto MD5_append = Module(nullptr).range.scan(sig_inst).add(9).as<void*>();
#if LOGGING
		conout << "MD5_append = " << MD5_append << std::endl;
#endif
		SOUP_IF_LIKELY (MD5_append != (void*)9)
		{
			MD5_append_hook.detour = reinterpret_cast<void*>(&MD5_append_detour);
			MD5_append_hook.target = MD5_append;
			MD5_append_hook.create();
			MD5_append_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if LABEL_REPLACEMENTS
	if (auto sig_inst = g_repo.getVersionedPattern(soup::joaat::compileTimeHash("OpenWF/vv/sig/check_string_substitutions.json"), game_version); !sig_inst.bytes.empty())
	{
		auto check_string_substitutions = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "check_string_substitutions = " << check_string_substitutions << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(check_string_substitutions))
		{
			if (game_version >= GV(36, 0, 0))
			{
				check_string_substitutions_hook.detour = reinterpret_cast<void*>(&static_check_string_substitutions_detour);
			}
			else if (game_version >= GV(35, 5, 0))
			{
				check_string_substitutions_hook.detour = reinterpret_cast<void*>(&check_string_substitutions_detour<GameString>);
			}
			else
			{
				check_string_substitutions_hook.detour = reinterpret_cast<void*>(&check_string_substitutions_detour<LegacyGameString>);
			}
			check_string_substitutions_hook.target = check_string_substitutions;
			check_string_substitutions_hook.code_cave = Module(nullptr).range.scan(CompactDetourHook::getCodeCavePattern()).as<void*>();
#if LOGGING
			conout << "check_string_substitutions_hook.code_cave = " << check_string_substitutions_hook.code_cave << std::endl;
#endif
			check_string_substitutions_hook.create();
			check_string_substitutions_hook.enable();
		}
	}
#endif

	{
		Pointer string_pool_insn;
		if (game_version >= GV(40, 0, 0))
		{
			SIG_INST("48 8B 05 ? ? ? ? 48 8B FA 45 0F B7 C1 4D 03 C0 49 C1 E9 10");
			string_pool_insn = Module(nullptr).range.scan(sig_inst);
		}
		else
		{
			SIG_INST("48 8B 05 ? ? ? ? 0F B7 CA 48 03 C9 48 C1 EA 10 48 03 14 C8");
			string_pool_insn = Module(nullptr).range.scan(sig_inst);
		}
#if LOGGING
		conout << "string_pool_insn = " << string_pool_insn.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (string_pool_insn)
		{
			string_pool = string_pool_insn.add(3).rip().as<StringPoolBucket**>();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

#if METADATA_PATCHES && SOUP_BITS == 64
	if (game_version >= GV(35, 5, 0))
	{
		SIG_INST("41 B1 03 48 8D 55 ? 45 33 C0 48 8D 8D ? ? ? ? E8");
		const Pointer object_type_serialise_propery_text_call = string_pool ? Module(nullptr).range.scan(sig_inst) : nullptr;
#if LOGGING
		conout << "object_type_serialise_propery_text_call = " << object_type_serialise_propery_text_call.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(object_type_serialise_propery_text_call.as<void*>()))
		{
			uint8_t detour_bytes[] = {
				0x49, 0x89, 0xF3, // mov r11, rsi
				/* 3 */ 0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs r10, (8 bytes)
				0x41, 0xFF, 0xE2, // jmp r10
			};
			*(void**)(detour_bytes + 5) = reinterpret_cast<void*>(&object_type_serialise_propery_text_detour);

			void* detour = memGuard::alloc(sizeof(detour_bytes), memGuard::ACC_RWX);
			memcpy(detour, detour_bytes, sizeof(detour_bytes));

			object_type_serialise_propery_text_hook.detour = detour;
			object_type_serialise_propery_text_hook.target = object_type_serialise_propery_text_call.add(17).as<void*>();
			object_type_serialise_propery_text_hook.code_cave = Module(nullptr).range.scan(CallsiteHook::getCodeCavePattern()).as<void*>();
#if LOGGING
			conout << "object_type_serialise_propery_text_hook.code_cave = " << object_type_serialise_propery_text_hook.code_cave << std::endl;
#endif
			object_type_serialise_propery_text_hook.create();
			object_type_serialise_propery_text_hook.enable();
		}
	}
#endif

#if VERBOSE_SERPROPTXT
	{
		SIG_INST("48 8B C4 48 89 58 08 48 89 68 10 56 57 41 56 48 81 EC A0 00 00 00 0F 29 70 D8");
		auto serialise_propery_text = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "serialise_propery_text = " << serialise_propery_text << std::endl;
#endif
		SOUP_IF_LIKELY (serialise_propery_text)
		{
			serialise_propery_text_hook.detour = reinterpret_cast<void*>(&serialise_propery_text_detour);
			serialise_propery_text_hook.target = serialise_propery_text;
			serialise_propery_text_hook.create();
			serialise_propery_text_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if false // This one is definitely problematic for 40.0.0
	{
		//SIG_INST("48 89 6C 24 18 57 41 56 41 57 48 83 EC 30 4C 8B F1 4D 8B F9 48 8B CA 49 8B F8 48 8B EA E8"); // startInstance (4 arguments, void return)
		SIG_INST("48 89 5C 24 20 55 56 57 48 83 EC 30 48 8B E9 48 8B FA 48 8D 0D"); // startInstanceInternal (2 arguments, bool return)
		auto ScriptMgr_startInstance = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "ScriptMgr_startInstance = " << ScriptMgr_startInstance << std::endl;
#endif
		SOUP_IF_LIKELY (ScriptMgr_startInstance)
		{
			ScriptMgr_startInstance_hook.detour = reinterpret_cast<void*>(&ScriptMgr_startInstance_detour);
			ScriptMgr_startInstance_hook.target = ScriptMgr_startInstance;
			ScriptMgr_startInstance_hook.create();
			ScriptMgr_startInstance_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (auto sig_inst = g_repo.getVersionedPattern(soup::joaat::compileTimeHash("OpenWF/vv/sig/irc_send_raw.json"), game_version); !sig_inst.bytes.empty())
	{
		auto irc_send_raw = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "irc_send_raw = " << irc_send_raw << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(irc_send_raw))
		{
			if (game_version >= GV(35, 5, 0))
			{
				irc_send_raw_hook.detour = reinterpret_cast<void*>(&irc_send_raw_detour<GameString>);
			}
			else if (game_version >= GV(19, 0, 0))
			{
				irc_send_raw_hook.detour = reinterpret_cast<void*>(&irc_send_raw_detour<LegacyGameString>);
			}
			else
			{
				irc_send_raw_hook.detour = reinterpret_cast<void*>(&irc_send_raw_detour<LegacyGameStringU18>);
			}
			irc_send_raw_hook.target = irc_send_raw;
			irc_send_raw_hook.create();
			irc_send_raw_hook.enable();
		}
	}
#endif

#if VERBOSE_IRC
	{
		SIG_INST("80 3D ? ? ? ? 00 74 ? 40 84 FF 74 ? B2 05");
		auto irc_log_in_cond = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "irc_log_in_cond = " << irc_log_in_cond.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (irc_log_in_cond)
		{
			memGuard::setAllowedAccess(irc_log_in_cond.add(12).as<void*>(), 2, memGuard::ACC_RWX);
			*irc_log_in_cond.add(12).as<uint8_t*>() = 0x90;
			*irc_log_in_cond.add(13).as<uint8_t*>() = 0x90;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if METADATA_PATCHES && !MINIMAL_HOOKS
	// Allow GetOnVehicle with an operator avatar
	// This is honestly such a stupid restriction for them to even have in code, I don't think it even needs a config to disable
	if (object_type_serialise_propery_text_hook.target)
	{
		// "an operator is trying to ride "
		Pointer operator_mount_fail;
		if (game_version >= GV(38, 0, 0))
		{
			SIG_INST("32 C0 48 8B 5C 24 40 48 8B 74 24 48 48 83 C4 30 5F C3 B2 05");
			operator_mount_fail = Module(nullptr).range.scan(sig_inst);
		}
		else
		{
			SIG_INST("E8 ? ? ? ? 32 C0 48 8B 5C 24 40 48 8B 6C 24 48 48 8B 74 24 50 48 83 C4 30 5F C3"); // U37
			operator_mount_fail = Module(nullptr).range.scan(sig_inst);
			if (operator_mount_fail)
			{
				operator_mount_fail = operator_mount_fail.add(5);
			}
		}
#if LOGGING
		conout << "operator_mount_fail = " << operator_mount_fail.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (operator_mount_fail)
		{
			memGuard::setAllowedAccess(operator_mount_fail.as<void*>(), 2, memGuard::ACC_RWX);
			operator_mount_fail.as<uint8_t*>()[0] = 0xb0;
			operator_mount_fail.as<uint8_t*>()[1] = 0x01;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if !MINIMAL_HOOKS
	// Needed to make RequestSlomo work outside of Captura
	{
		SIG_INST("FF 90 ? ? 00 00 84 C0 74 ? F3 0F 11 73");
		auto RequestSlomo_cond = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "RequestSlomo_cond = " << RequestSlomo_cond.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (RequestSlomo_cond)
		{
			memGuard::setAllowedAccess(RequestSlomo_cond.add(8).as<void*>(), 2, memGuard::ACC_RWX);
			RequestSlomo_cond.as<uint8_t*>()[8] = 0x90;
			RequestSlomo_cond.as<uint8_t*>()[9] = 0x90;
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if !MINIMAL_HOOKS
	if (!logout_on_request_failure || PRIVATE)
	{
		ObfusString str("WebSubscribeToFailure");
		auto lua_WebSubscribeToFailure_hash = Module(nullptr).range.scan(hash_to_pattern(wf_hash(str.c_str())));
#if LOGGING
		conout << "lua_WebSubscribeToFailure_hash = " << lua_WebSubscribeToFailure_hash.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (lua_WebSubscribeToFailure_hash)
		{
			if (!logout_on_request_failure)
			{
				auto lua_WebSubscribeToFailure_code = *lua_WebSubscribeToFailure_hash.add(8).as<uint8_t**>();
				memGuard::setAllowedAccess(lua_WebSubscribeToFailure_code, 3, memGuard::ACC_RWX);
				lua_WebSubscribeToFailure_code[0] = 0x31;
				lua_WebSubscribeToFailure_code[1] = 0xC0;
				lua_WebSubscribeToFailure_code[2] = 0xC3;
			}
		}
		else
		{
			conout << get_core_string(ObfusString("sigfaillorf").str()) << std::endl;
		}
	}
#endif

#if VERBOSE_OODLE
	{
		SIG_INST("48 89 5C 24 08 57 48 83 EC 20 48 8B D9 E8 ? ? ? ? 44 8B 43 6C 49 3B C0");
		auto init_oodle_network_state = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "init_oodle_network_state = " << init_oodle_network_state << std::endl;
#endif
		SOUP_IF_LIKELY (init_oodle_network_state)
		{
			init_oodle_network_state_hook.detour = reinterpret_cast<void*>(&init_oodle_network_state_detour);
			init_oodle_network_state_hook.target = init_oodle_network_state;
			init_oodle_network_state_hook.create();
			init_oodle_network_state_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		SIG_INST("48 89 5C 24 20 55 56 57 41 56 41 57 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 40 44 8B 71 08 48 8B D9");
		auto compress_packet_oodle_net = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "compress_packet_oodle_net = " << compress_packet_oodle_net << std::endl;
#endif
		SOUP_IF_LIKELY (compress_packet_oodle_net)
		{
			compress_packet_oodle_net_hook.detour = reinterpret_cast<void*>(&compress_packet_oodle_net_detour);
			compress_packet_oodle_net_hook.target = compress_packet_oodle_net;
			compress_packet_oodle_net_hook.create();
			compress_packet_oodle_net_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		SIG_INST("40 53 55 56 57 41 54 41 56 41 57 48 83 EC 50 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 48 44 8B 79 08 48 8B F9");
		auto compress_packet_oodle_lz = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "compress_packet_oodle_lz = " << compress_packet_oodle_lz << std::endl;
#endif
		SOUP_IF_LIKELY (compress_packet_oodle_lz)
		{
			compress_packet_oodle_lz_hook.detour = reinterpret_cast<void*>(&compress_packet_oodle_lz_detour);
			compress_packet_oodle_lz_hook.target = compress_packet_oodle_lz;
			compress_packet_oodle_lz_hook.create();
			compress_packet_oodle_lz_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		SIG_INST("40 53 55 57 41 56 41 57 48 81 EC E0 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 D0 00 00 00 48 63 9C 24 30 01 00 00 49 8B E9");
		auto oodle_compress = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "oodle_compress = " << oodle_compress << std::endl;
#endif
		SOUP_IF_LIKELY (oodle_compress)
		{
			oodle_compress_hook.detour = reinterpret_cast<void*>(&oodle_compress_detour);
			oodle_compress_hook.target = oodle_compress;
			oodle_compress_hook.create();
			oodle_compress_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if VERBOSE_SENDCNXLESS
	{
		SIG_INST("48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 83 B9");
		auto SendConnectionlessData = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "SendConnectionlessData = " << SendConnectionlessData << std::endl;
#endif
		SOUP_IF_LIKELY (SendConnectionlessData)
		{
			SendConnectionlessData_hook.detour = reinterpret_cast<void*>(&SendConnectionlessData_detour);
			SendConnectionlessData_hook.target = SendConnectionlessData;
			SendConnectionlessData_hook.create();
			SendConnectionlessData_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

	if (game_version >= GV(41, 0, 0))
	{
		SIG_INST("C7 44 24 68 03 00 00 00 48 89 44 24 60 48 89 44 24 58 48 89 44 24 50 48 89 44 24 48 48 89 44 24 40 48 89 44 24 38 89 44 24 30 89 44 24 28 89 44 24 20 E8");
		auto OodleLZ_Decompress_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "OodleLZ_Decompress_callsite = " << OodleLZ_Decompress_callsite.as<void*>() << std::endl;
#endif
		if (OodleLZ_Decompress_callsite)
		{
			OodleLZ_Decompress = OodleLZ_Decompress_callsite.add(51).rip().as<OodleLZ_Decompress_t>();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

#if VERBOSE_LZF
	{
		SIG_INST("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 4C 89 44 24 18 57 41 54 41 55 41 56 41 57 B8 00 00 04 00 E8 ? ? ? ? 48 2B E0");
		auto lzf_compress = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "lzf_compress = " << lzf_compress << std::endl;
#endif
		SOUP_IF_LIKELY (lzf_compress)
		{
			lzf_compress_hook.detour = reinterpret_cast<void*>(&lzf_compress_detour);
			lzf_compress_hook.target = lzf_compress;
			lzf_compress_hook.create();
			lzf_compress_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}

	{
		SIG_INST("48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 44 8B DA 49 8B F8 4C 03 D9");
		auto lzf_decompress = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "lzf_decompress = " << lzf_decompress << std::endl;
#endif
		SOUP_IF_LIKELY (lzf_decompress)
		{
			lzf_decompress_hook.detour = reinterpret_cast<void*>(&lzf_decompress_detour);
			lzf_decompress_hook.target = lzf_decompress;
			lzf_decompress_hook.create();
			lzf_decompress_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if VERBOSE_UNCOMPRESSPKT
	{
		SIG_INST("40 55 56 57 41 54 41 56 48 8D 6C 24 C9 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 1F 4C 8B 0A");
		auto UncompressPacket = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "UncompressPacket = " << UncompressPacket << std::endl;
#endif
		SOUP_IF_LIKELY (UncompressPacket)
		{
			UncompressPacket_hook.detour = reinterpret_cast<void*>(&UncompressPacket_detour);
			UncompressPacket_hook.target = UncompressPacket;
			UncompressPacket_hook.create();
			UncompressPacket_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if VERBOSE_PKTCHKSUM
	{
		SIG_INST("40 53 55 56 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 88 00 00 00 48 8B 32");
		auto verify_packet_sig = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "verify_packet_sig = " << verify_packet_sig << std::endl;
#endif
		SOUP_IF_LIKELY (verify_packet_sig)
		{
			verify_packet_sig_hook.detour = reinterpret_cast<void*>(&verify_packet_sig_detour);
			verify_packet_sig_hook.target = verify_packet_sig;
			verify_packet_sig_hook.code_cave = Module(nullptr).range.scan(CompactDetourHook::getCodeCavePattern()).as<void*>();
#if LOGGING
			conout << "verify_packet_sig_hook.code_cave = " << verify_packet_sig_hook.code_cave << std::endl;
#endif
			verify_packet_sig_hook.create();
			verify_packet_sig_hook.enable();
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

#if !MINIMAL_HOOKS
	// Disable the back-off logic for content requests, which might lead to a cascade of failures from only a single piece of content being 404.
	// This is especially important to keep update patches and stripped assets independent of each other.
	if (game_version >= GV(40, 0, 0))
	{
		// "increasing delay to"
		Pointer content_retry_insn;
		if (game_version >= GV(41, 0, 1))
		{
			SIG_INST("4A 8D 0C 30 48 8B 46 08 48 89 08");
			content_retry_insn = Module(nullptr).range.scan(sig_inst);
		}
		else
		{
			SIG_INST("4A 8D 0C 38 48 8B 47 08 48 89 08");
			content_retry_insn = Module(nullptr).range.scan(sig_inst);
		}
#if LOGGING
		conout << "content_retry_insn = " << content_retry_insn.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(content_retry_insn.as<void*>()))
		{
			memGuard::setAllowedAccess(content_retry_insn.as<void*>(), 4, memGuard::ACC_RWX);
			// < 41.0.1: lea rcx, [rax+r15] -> xor rcx, rcx; nop; nop
			// >=41.0.1: lea rcx, [rax+r14] -> xor rcx, rcx; nop; nop
			content_retry_insn.as<uint8_t*>()[0] = 0x31;
			content_retry_insn.as<uint8_t*>()[1] = 0xC9;
			content_retry_insn.as<uint8_t*>()[2] = 0x90;
			content_retry_insn.as<uint8_t*>()[3] = 0x90;
		}
	}
	else
	{
		SIG_INST("48 03 D0 48 8B 47 08 48 89 10");
		auto content_retry_insn = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
		conout << "content_retry_insn = " << content_retry_insn << std::endl;
#endif
		SOUP_IF_LIKELY (content_retry_insn)
		{
			memGuard::setAllowedAccess(content_retry_insn, 3, memGuard::ACC_RWX);
			memset(content_retry_insn, 0x90, 3);
		}
		else
		{
			log_optional_scan_failure(false);
		}
	}
#endif

	if (game_version >= GV(40, 0, 0))
	{
		void* anticheat_sideloading_check;
		if (game_version >= GV(41, 0, 0))
		{
			SIG_INST("40 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 80 01 00 00 48 8B 81");
			anticheat_sideloading_check = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
		else
		{
			SIG_INST("40 55 56 41 54 41 55 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 70 01 00 00");
			anticheat_sideloading_check = Module(nullptr).range.scan(sig_inst).as<void*>();
		}
#if LOGGING
		conout << "anticheat_sideloading_check = " << anticheat_sideloading_check << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(anticheat_sideloading_check))
		{
			anticheat_sideloading_check_hook.detour = reinterpret_cast<void*>(&do_nothing);
			anticheat_sideloading_check_hook.target = anticheat_sideloading_check;
			anticheat_sideloading_check_hook.enable();
		}
	}

	// In case the timer was initialised, we'll also want to disable the timer check. U41 initialises it in some setup function. ("EnableNonClientDpiScaling")
	if (game_version >= GV(40, 0, 0))
	{
		SIG_INST("48 8B CE 0F B6 D8 E8 ? ? ? ? 48 8B 7C 24 40");
		auto anticheat_timer_check_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "anticheat_timer_check_callsite = " << anticheat_timer_check_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(anticheat_timer_check_callsite.as<void*>()))
		{
			auto anticheat_timer_check = anticheat_timer_check_callsite.add(7).rip().as<void*>();

			anticheat_timer_check_hook.detour = reinterpret_cast<void*>(&do_nothing);
			anticheat_timer_check_hook.target = anticheat_timer_check;
			anticheat_timer_check_hook.enable();
		}
	}
}

static SOUP_FORCEINLINE void do_pointer_scans()
{
	if (game_version >= GV(38, 5, 0))
	{
		SIG_INST("48 8D 4B 18 33 D2 E8 ? ? ? ? 33 D2 48 8D 4B 38 E8 ? ? ? ? 48 8B ? 24");
		auto string_resize_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "string_resize_callsite = " << string_resize_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(string_resize_callsite.as<void*>()))
		{
			string_resize = string_resize_callsite.add(7).rip().as<string_resize_t>();
		}
	}

	if (have_scripting)
	{
		SIG_INST("48 8B 05 ? ? ? ? FF D0 85 C0 74 02 CD 2C");
		auto raise_script_error_fp_mov = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "raise_script_error_fp_mov = " << raise_script_error_fp_mov.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(raise_script_error_fp_mov.as<void*>()))
		{
			raise_script_error_fp = raise_script_error_fp_mov.add(3).rip().as<raise_script_error_t*>();
		}
	}

	/*{
		SIG_INST("48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 81 EC ? ? ? ? 48 8B FA 41 8B E8 48 8B F1 41 B9");
		luau_newstate = Module(nullptr).range.scan(sig_inst).as<luau_newstate_t>();
#if LOGGING
		conout << "luau_newstate = " << (void*)luau_newstate << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luau_newstate)
		{
			log_optional_scan_failure(false);
		}
	}*/

	if (have_scripting)
	{
		if (game_version >= GV(40, 0, 0))
		{
			SIG_INST("48 89 74 24 18 57 48 83 EC 20 48 8B F2 48 8B F9 48 85 D2 75 17 48 8B 41 08 89 50 0C");
			luau_pushstring = Module(nullptr).range.scan(sig_inst).as<luau_pushstring_t>();
		}
		else
		{
			SIG_INST("48 89 6C 24 18 56 48 83 EC 20 48 8B EA 48 8B F1 48 85 D2");
			luau_pushstring = Module(nullptr).range.scan(sig_inst).as<luau_pushstring_t>();
		}
#if LOGGING
		conout << "luau_pushstring = " << (void*)luau_pushstring << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luau_pushstring)
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		SIG_INST("48 89 5C 24 08 57 48 83 EC 20 48 8B DA 48 8B F9 48 85 D2 75 0F");
		luau_pushpointer = Module(nullptr).range.scan(sig_inst).as<luau_pushpointer_t>();
#if LOGGING
		conout << "luau_pushpointer = " << (void*)luau_pushpointer << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luau_pushpointer)
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		SIG_INST("48 89 74 24 18 57 48 83 EC 20 48 8B F2 48 8B F9 48 85 D2 75 0F 48 8B 74");
		luau_pushobject = Module(nullptr).range.scan(sig_inst).as<luau_pushobject_t>();
#if LOGGING
		conout << "luau_pushobject = " << (void*)luau_pushobject << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luau_pushobject)
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		SIG_INST("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B D9 49 63 F9 48 8B 49 18 49 8B F0");
		luau_pushcclosurek = Module(nullptr).range.scan(sig_inst).as<luau_pushcclosurek_t>();
#if LOGGING
		conout << "luau_pushcclosurek = " << (void*)luau_pushcclosurek << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luau_pushcclosurek)
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		SIG_INST("BA 01 00 00 00 48 8B CB E8 ? ? ? ? 85 C0 74 0B B8 02 00 00 00"); // U37, U38, U40, U41
		auto lua_next_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "lua_next_callsite = " << lua_next_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(lua_next_callsite.as<void*>()))
		{
			luau_next = lua_next_callsite.add(9).rip().as<luau_next_t>();
		}
	}

	if (have_scripting)
	{
		SIG_INST("BA 03 00 00 00 48 8B CF E8 ? ? ? ? BA FF FF FF FF");
		auto luau_gettable_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
		conout << "luau_gettable_callsite = " << luau_gettable_callsite.as<void*>() << std::endl;
#endif
		SOUP_IF_LIKELY (should_setup_optional_conditional_feature(luau_gettable_callsite.as<void*>()))
		{
			luau_gettable = luau_gettable_callsite.add(9).rip().as<luau_gettable_t>();
		}
	}

	if (have_scripting)
	{
		SIG_INST("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 4C 8B 49 18 41 8B F0");
		luau_createtable = Module(nullptr).range.scan(sig_inst).as<luau_createtable_t>();
#if LOGGING
		conout << "luau_createtable = " << (void*)luau_createtable << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luau_createtable)
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		if (game_version >= GV(35, 5, 0))
		{
			SIG_INST("40 53 48 83 EC 20 4C 8B D1 85 D2 7E");
			luau_settable = Module(nullptr).range.scan(sig_inst).as<luau_settable_t>();
		}
		else
		{
			SIG_INST("40 53 48 83 EC 20 48 8B D9 85 D2 7E ? 48 8B 41 10"); // U35.1
			luau_settable = Module(nullptr).range.scan(sig_inst).as<luau_settable_t>();
		}
#if LOGGING
		conout << "luau_settable = " << (void*)luau_settable << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luau_settable)
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		if (game_version >= GV(43, 0, 0))
		{
			SIG_INST("48 89 5C 24 18 57 48 83 EC 20 0F B7 41 50 48 8B D9 66 FF C0 49 63 F8"); // U43
			luauD_call = Module(nullptr).range.scan(sig_inst).as<luauD_call_t>();
		}
		else if (game_version >= GV(39, 0, 0))
		{
			SIG_INST("40 53 57 48 83 EC 28 0F B7 41 50 48 8B D9 66 FF C0 49 63 F8");
			luauD_call = Module(nullptr).range.scan(sig_inst).as<luauD_call_t>();
		}
		else if (game_version >= GV(35, 0, 0))
		{
			SIG_INST("48 89 5C 24 18 57 48 83 EC 20 0F B7 41 50 48 8B D9 66 FF C0"); // U35.1
			luauD_call = Module(nullptr).range.scan(sig_inst).as<luauD_call_t>();
		}
		else
		{
			SIG_INST("40 53 48 83 EC 20 0F B7 41 50 48 8B D9 66 FF C0"); // U33.6
			luauD_call = Module(nullptr).range.scan(sig_inst).as<luauD_call_t>();
		}
#if LOGGING
		conout << "luauD_call = " << (void*)luauD_call << std::endl;
#endif
		SOUP_IF_UNLIKELY (!luauD_call)
		{
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting)
	{
		Pointer res[20]; // 11 results in U35.1 & U38
		int type_arr_end_offset;
		int nres;
		if (game_version >= GV(40, 0, 0))
		{
			SIG_INST("48 8D 05 ? ? ? ? 4C 89 3D ? ? ? ? 48 89 05 ? ? ? ? BF 01 00 00 00 48 8D 05 ? ? ? ? 48 89 05 ? ? ? ? EB");
			type_arr_end_offset = 29;
			nres = Module(nullptr).range.scanWithMultipleResults(sig_inst, res);
		}
		else if (game_version >= GV(38, 0, 0))
		{
			SIG_INST("48 8D 05 ? ? ? ? 48 89 35 ? ? ? ? 48 89 05 ? ? ? ? BF 01 00 00 00 48 8D 05 ? ? ? ? 48 89 05 ? ? ? ? EB");
			type_arr_end_offset = 29;
			nres = Module(nullptr).range.scanWithMultipleResults(sig_inst, res);
		}
		else if (game_version >= GV(35, 5, 0))
		{
			SIG_INST("48 8D 05 ? ? ? ? 48 89 2D ? ? ? ? 48 89 05 ? ? ? ? BF 01 00 00 00 48 8D 05 ? ? ? ? 48 89 05 ? ? ? ? EB");
			type_arr_end_offset = 29;
			nres = Module(nullptr).range.scanWithMultipleResults(sig_inst, res);
		}
		else
		{
			SIG_INST("48 8D 05 ? ? ? ? 48 89 35 ? ? ? ? 48 89 05 ? ? ? ? 41 8D 7D 01 48 8D 05 ? ? ? ? 48 89 05 ? ? ? ? EB"); // U35.1
			type_arr_end_offset = 28;
			nres = Module(nullptr).range.scanWithMultipleResults(sig_inst, res);
		}
		for (int i = 0; i != nres; ++i)
		{
			auto type_arr = res[i].add(3).rip().as<SwigTypeField**>();
			auto type_arr_end = res[i].add(type_arr_end_offset).rip().as<SwigTypeField**>();
#if LOGGING
			//conout << i << std::endl;
			//conout << "type_arr = " << (void*)type_arr << std::endl;
			//conout << "type_arr_end = " << (void*)type_arr_end << std::endl;
			//conout << "type_arr_size = " << (type_arr_end - type_arr) << std::endl;
#endif
			for (auto entry = type_arr; entry != type_arr_end && *entry; ++entry)
			{
				if ((*entry)->type_desc)
				{
#if LOGGING
					//conout << "\t- " << (*entry)->type_name << " " << (*entry)->field_name << std::endl;
#endif
					swig_types.emplace(soup::joaat::hashRange((*entry)->type_name, strlen((*entry)->type_name) - 2), (*entry)->type_desc);
#if PRIVATE
					swig_type_names.emplace_back(std::string((*entry)->type_name, strlen((*entry)->type_name) - 2));
#endif
				}
			}
		}
		if (nres == 0)
		{
#if LOGGING
			conout << "No results for swig types" << std::endl;
#endif
			log_optional_scan_failure(false);
		}
	}

	if (have_scripting && game_version < GV(40, 0, 0))
	{
		Pointer res[7]; // In U37 there's an 8th match that's not an enum so we need to ignore that one.
		int nres;
		if (game_version >= GV(38, 0, 0))
		{
			SIG_INST("48 8B 05 ? ? ? ? 4C 8D ? ? ? ? ? 4D 8B");
			nres = Module(nullptr).range.scanWithMultipleResults(sig_inst, res);
		}
		else
		{
			SIG_INST("? 8B 05 ? ? ? ? 4C 8D ? ? ? ? ? 4D 8B");
			nres = Module(nullptr).range.scanWithMultipleResults(sig_inst, res);
		}
		swig_enums1.reserve(nres);
		for (int i = 0; i != nres; ++i)
		{
			swig_enums1.emplace_back(res[i].add(3).rip().as<SwigEnum*>());
		}
		if (nres == 0)
		{
#if LOGGING
			conout << "No results for swig enums" << std::endl;
#endif
			log_optional_scan_failure(false);
		}
	}
}

#if SOUP_BITS == 32
#define EXE_NAME "Warframe.exe"
#else
#define EXE_NAME "Warframe.x64.exe"
#endif

BOOL APIENTRY DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		if (auto proc = soup::Process::current(); proc->name != EXE_NAME)
		{
			if (proc->name == "Launcher.exe")
			{
				std::filesystem::current_path("..");
			}
			else
			{
				MessageBoxA(0, "Please don't keep the Bootstrapper DLL (wtsapi32.dll, dwmapi.dll, or version.dll) in the same folder as any executable other than " EXE_NAME ".", BOOTSTRAPPER_TITLE, MB_OK | MB_ICONERROR);
				return exit(1), FALSE;
			}
		}

		if (!std::filesystem::exists(EXE_NAME))
		{
			MessageBoxA(0, "Launched with incorrect working directory; it must be the folder where " EXE_NAME " is.", BOOTSTRAPPER_TITLE, MB_OK | MB_ICONERROR);
			return exit(1), FALSE;
		}

		owfConsole::setTitle(BOOTSTRAPPER_TITLE);
		owfConsole::activate();

#if LOGGING
		conout << "base address = " << soup::Process::current()->open()->range.base.as<void*>() << std::endl;
#endif

		{
			wchar_t lpFilename[MAX_PATH] = { 0 };
			GetModuleFileNameW(hmod, lpFilename, MAX_PATH);
			dll_path_utf8 = unicode::utf16_to_utf8<std::wstring>(lpFilename);
#if LOGGING
			conout << "dll_path_utf8 = " << dll_path_utf8 << std::endl;
#endif
		}

		{
			std::wstring path(_wgetenv(L"windir"));
			path.append(LR"(\System32\dwmapi.dll)");
			og_dwmapi = LoadLibraryW(path.c_str());
#if LOGGING
			conout << "og_dwmapi = " << (void*)og_dwmapi << std::endl;
#endif
			og_DwmGetCompositionTimingInfo = GetProcAddress(og_dwmapi, "DwmGetCompositionTimingInfo");
		}

		{
			std::wstring path(_wgetenv(L"windir"));
			path.append(LR"(\System32\wtsapi32.dll)");
			og_wtsapi32 = LoadLibraryW(path.c_str());
#if LOGGING
			conout << "og_wtsapi32 = " << (void*)og_wtsapi32 << std::endl;
#endif
			og_WTSRegisterSessionNotification = GetProcAddress(og_wtsapi32, "WTSRegisterSessionNotification");
			og_WTSUnRegisterSessionNotification = GetProcAddress(og_wtsapi32, "WTSUnRegisterSessionNotification");
			og_WTSFreeMemory = GetProcAddress(og_wtsapi32, "WTSFreeMemory");
			og_WTSQuerySessionInformationA = GetProcAddress(og_wtsapi32, "WTSQuerySessionInformationA");
			og_WTSQuerySessionInformationW = GetProcAddress(og_wtsapi32, "WTSQuerySessionInformationW");
		}

		{
			std::wstring path(_wgetenv(L"windir"));
			path.append(LR"(\System32\version.dll)");
			og_version = LoadLibraryW(path.c_str());
#if LOGGING
			conout << "og_version = " << (void*)og_version << std::endl;
#endif
			og_GetFileVersionInfoA = (GetFileVersionInfoA_t)GetProcAddress(og_version, "GetFileVersionInfoA");
			//og_GetFileInformationByHandle = (GetFileInformationByHandle_t)GetProcAddress(og_version, "GetFileInformationByHandle");
			og_GetFileVersionInfoExA = (GetFileVersionInfoExA_t)GetProcAddress(og_version, "GetFileVersionInfoExA");
			og_GetFileVersionInfoExW = (GetFileVersionInfoExW_t)GetProcAddress(og_version, "GetFileVersionInfoExW");
			og_GetFileVersionInfoSizeA = (GetFileVersionInfoSizeA_t)GetProcAddress(og_version, "GetFileVersionInfoSizeA");
			og_GetFileVersionInfoSizeExA = (GetFileVersionInfoSizeExA_t)GetProcAddress(og_version, "GetFileVersionInfoSizeExA");
			og_GetFileVersionInfoSizeExW = (GetFileVersionInfoSizeExW_t)GetProcAddress(og_version, "GetFileVersionInfoSizeExW");
			og_GetFileVersionInfoSizeW = (GetFileVersionInfoSizeW_t)GetProcAddress(og_version, "GetFileVersionInfoSizeW");
			og_GetFileVersionInfoW = (GetFileVersionInfoW_t)GetProcAddress(og_version, "GetFileVersionInfoW");
			og_VerFindFileA = (VerFindFileA_t)GetProcAddress(og_version, "VerFindFileA");
			og_VerFindFileW = (VerFindFileW_t)GetProcAddress(og_version, "VerFindFileW");
			og_VerInstallFileA = (VerInstallFileA_t)GetProcAddress(og_version, "VerInstallFileA");
			og_VerInstallFileW = (VerInstallFileW_t)GetProcAddress(og_version, "VerInstallFileW");
			og_VerLanguageNameA = (VerLanguageNameA_t)GetProcAddress(og_version, "VerLanguageNameA");
			og_VerLanguageNameW = (VerLanguageNameW_t)GetProcAddress(og_version, "VerLanguageNameW");
			og_VerQueryValueA = (VerQueryValueA_t)GetProcAddress(og_version, "VerQueryValueA");
			og_VerQueryValueW = (VerQueryValueW_t)GetProcAddress(og_version, "VerQueryValueW");
		}

		{
			DWORD dwHandle;
			DWORD version_info_size = og_GetFileVersionInfoSizeA(EXE_NAME, &dwHandle);

			void* data = soup::malloc(version_info_size);
			og_GetFileVersionInfoA(EXE_NAME, 0, version_info_size, data);

			/*struct LANGANDCODEPAGE {
				WORD wLanguage;
				WORD wCodePage;
			} *lpTranslate;
			UINT cbTranslate;
			VerQueryValueA(data, "\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate);
			for (INT i=0; i < (cbTranslate/4); i++)
			{
				conout << "Lang " << lpTranslate[i].wLanguage << ", c.p. " << lpTranslate[i].wCodePage << std::endl;
			}*/

			LPVOID value_data;
			UINT value_size;
			og_VerQueryValueA(data, "\\StringFileInfo\\040904B0\\ProductVersion", &value_data, &value_size);
			memcpy(build_version, value_data, 16);

			soup::free(data);
		}

#if LOGGING
		conout << "build_version = " << std::string(build_version, 16) << std::endl;
#endif

		std::error_code ec{};
		std::filesystem::create_directory(ObfusString("OpenWF").str(), ec);
		SOUP_RETHROW_FALSE(check_ec(ec));

		g_repo.loadBuiltinArchive();
		if (auto hotfix = string::fromFile(ObfusString("OpenWF/Hotfix.owf").str()); !hotfix.empty())
		{
			if (g_repo.loadHotfix(hotfix.data(), hotfix.size()))
			{
#if PRIVATE
				conout << ObfusString("Hotfix applied").str() << std::endl;
#endif
			}
			else
			{
				conout << ObfusString("Ignoring hotfix because it was made for a different DLL version").str() << std::endl;
			}
		}

		{
			auto build_version_int = static_cast<uint64_t>(build_version[ 0] - '0') * 100000000000ull +
				static_cast<uint64_t>(build_version[ 1] - '0') * 10000000000ull +
				static_cast<uint64_t>(build_version[ 2] - '0') * 1000000000ull +
				static_cast<uint64_t>(build_version[ 3] - '0') * 100000000ull +
				static_cast<uint64_t>(build_version[ 5] - '0') * 10000000ull +
				static_cast<uint64_t>(build_version[ 6] - '0') * 1000000ull +
				static_cast<uint64_t>(build_version[ 8] - '0') * 100000ull +
				static_cast<uint64_t>(build_version[ 9] - '0') * 10000ull +
				static_cast<uint64_t>(build_version[11] - '0') * 1000ull +
				static_cast<uint64_t>(build_version[12] - '0') * 100ull +
				static_cast<uint64_t>(build_version[14] - '0') * 10ull +
				static_cast<uint64_t>(build_version[15] - '0');

			game_version = static_cast<uint32_t>(g_repo.getVersionedU64(soup::joaat::compileTimeHash("OpenWF/vv/game_versions.json"), build_version_int));

#if LOGGING
			conout << "build_version_int = " << build_version_int << std::endl;
			conout << "game_version = " << game_version << std::endl;
#endif
		}

		// Load client tunables from repo
		{
			size_t size;
			auto data = g_repo.find(joaat::compileTimeHash("OpenWF/tunables.json"), size);
			g_client_tunables.loadMsgpack(data, size);
		}

		// Load version config (depends on tunables)
		strip_tls = game_version < g_client_tunables.getInt(joaat::compileTimeHash("min_gv_for_tls"));
		force_disable_overlay = game_version < g_client_tunables.getInt(joaat::compileTimeHash("min_gv_for_overlay"));
		have_scripting = game_version >= g_client_tunables.getInt(joaat::compileTimeHash("min_gv_for_scripting"));

		// Load config (depends on repo & version config)
		if (!std::filesystem::exists(ObfusString("OpenWF/Client Config.json").str()))
		{
			if (std::filesystem::exists(ObfusString("OpenWF/client_config.json").str()))
			{
				std::filesystem::rename(ObfusString("OpenWF/client_config.json").str(), ObfusString("OpenWF/Client Config.json").str(), ec);
				SOUP_RETHROW_FALSE(check_ec(ec));
			}
			else if (std::filesystem::exists(ObfusString("client_config.json").str()))
			{
				std::filesystem::rename(ObfusString("client_config.json").str(), ObfusString("OpenWF/Client Config.json").str(), ec);
				SOUP_RETHROW_FALSE(check_ec(ec));
			}
		}
		owfConfig::load();
		owfConfig::save();
		{
			std::vector<std::string> args{};
			{
				int argc;
				wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
				for (int i = 0; i != argc; ++i)
				{
					args.emplace_back(unicode::utf16_to_utf8<std::wstring>(argv[i]));
				}
			}
			for (const auto& arg : args)
			{
				if (arg.size() > 15 && arg.substr(0, 15) == ObfusString("-owfServerHost:").str())
				{
					server_host = arg.substr(15);
				}
				else if (arg.size() > 13 && arg.substr(0, 13) == ObfusString("-owfHttpPort:").str())
				{
					string::toIntOpt<uint16_t>(arg.substr(13)).consume(http_port);
				}
				else if (arg.size() > 14 && arg.substr(0, 14) == ObfusString("-owfHttpsPort:").str())
				{
					string::toIntOpt<uint16_t>(arg.substr(14)).consume(https_port);
				}
				else if (arg.size() > 19 && arg.substr(0, 19) == ObfusString("-owfClientHttpPort:").str())
				{
					string::toIntOpt<uint16_t>(arg.substr(19)).consume(client_http_port);
				}
				else if (arg.size() > 14 && arg.substr(0, 14) == ObfusString("-owfAutologin:").str())
				{
					autologin = (arg.c_str()[15] == '1');
				}
				else if (arg.size() > 10 && arg.substr(0, 10) == ObfusString("-owfEmail:").str())
				{
					autologin_email = arg.substr(10);
				}
				else if (arg.size() > 13 && arg.substr(0, 13) == ObfusString("-owfPassword:").str())
				{
					owfConfig::setAutologinPassword(arg.substr(13));
				}
			}
		}

		// Initialise core dict (depends on repo + config)
		g_core_dict = g_repo.getDict(ObfusString("core").str(), language);
		g_overlay_dict = g_repo.getDict(ObfusString("overlay").str(), language);

		// Reject too new versions (depends on core dict)
		if (game_version >= g_client_tunables.getInt(joaat::compileTimeHash("toonew")))
		{
			auto msg = soup::unicode::utf8_to_utf16(get_core_string(ObfusString("toonew").str()));
			auto title = soup::unicode::utf8_to_utf16(get_bootstrapper_title());
			MessageBoxW(0, msg.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
			return exit(1), FALSE;
		}

		if (!ee_log_in_console || game_version >= GV(23, 10, 0))
		{
			owfConsole::setExclusiveOutput();
		}

		conout << get_core_string(ObfusString("freenote").str()) << std::endl;

		owfScript::init();

		{
#if PRIVATE
			auto t = time::millis();
#endif
			create_all_hooks();
#if PRIVATE
			conout << "Scans & hooks done in " << (time::millis() - t) << " ms" << std::endl;
#endif
		}

		{
			Thread thrd([](Capture&&)
			{
#if PRIVATE
				auto t = time::millis();
#endif
				do_pointer_scans();
#if PRIVATE
				conout << "Pointer scans done in " << (time::millis() - t) << " ms" << std::endl;
#endif

#if VERIFY_EXE_SIG && SOUP_BITS == 64
				// Make sure the EXE version we read earlier is actually to be trusted.
				// Can't do this in DllMain, so doing it here/now.
				if (game_version >= GV(39, 0, 0) && game_version < GV(41, 0, 0)
					&& !os::isWine() // Crashes :(
					)
				{
					ObfusString Warframe_x64_exe("Warframe.x64.exe");
					auto wstr_Warframe_x64_exe = unicode::utf8_to_utf16(Warframe_x64_exe.str());

					WINTRUST_FILE_INFO fileInfo = {};
					fileInfo.cbStruct = sizeof(fileInfo);
					fileInfo.pcwszFilePath = wstr_Warframe_x64_exe.c_str();

					WINTRUST_DATA trustData = {};
					trustData.cbStruct = sizeof(trustData);
					trustData.dwUIChoice = WTD_UI_NONE;
					trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
					trustData.dwUnionChoice = WTD_CHOICE_FILE;
					trustData.pFile = &fileInfo;
					trustData.dwStateAction = 0;
					trustData.dwProvFlags = WTD_SAFER_FLAG;

					GUID policyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;

					ObfusString str_wintrust("wintrust");
					ObfusString str_WinVerifyTrust("WinVerifyTrust");
					auto hWintrust = LoadLibraryA(str_wintrust.c_str());
					auto WinVerifyTrust_fp = reinterpret_cast<decltype(&WinVerifyTrust)>(GetProcAddress(hWintrust, str_WinVerifyTrust.c_str()));
					exe_signed = (WinVerifyTrust_fp(NULL, &policyGUID, &trustData) == ERROR_SUCCESS);
					FreeLibrary(hWintrust);
				}
#endif

				if (!lua_set_global_by_hash_hook.isCreated() && !lua_set_global_hook.isCreated())
				{
					Sleep(10000);
					do_console_to_overlay_transition();
				}
			});
			thrd.detach();
		}

		on_got_server_host();

		start_builtin_http_server();

		{
#if PRIVATE
			auto t = time::millis();
#endif
			start_bgscript();
#if PRIVATE
			conout << "Started background script in " << (time::millis() - t) << " ms" << std::endl;
#endif
		}

		{
#if PRIVATE
			auto t = time::millis();
#endif
			load_hotkeys();
#if PRIVATE
			conout << "Loaded hotkeys in " << (time::millis() - t) << " ms" << std::endl;
#endif
		}

#if LABEL_REPLACEMENTS
		if (check_string_substitutions_hook.isCreated())
		{
	#if PRIVATE
			auto t = time::millis();
	#endif
			load_label_replacements();
	#if PRIVATE
			conout << "Loaded label replacements in " << (time::millis() - t) << " ms" << std::endl;
	#endif
		}
#endif

#if METADATA_PATCHES && SOUP_BITS == 64
		if (object_type_serialise_propery_text_hook.target)
		{
	#if PRIVATE
			auto t = time::millis();
	#endif
			load_metadata_patches();
	#if PRIVATE
			conout << "Loaded metadata patches in " << (time::millis() - t) << " ms" << std::endl;
	#endif
		}
#endif

		if (!auto_start_scripts.empty())
		{
			ObfusString base_path("OpenWF/Scripts/");
			for (const auto& path : auto_start_scripts)
			{
				start_script_from_file(base_path.str() + path);
			}
		}
	}
	return TRUE;
}
