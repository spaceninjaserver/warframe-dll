#include "owf_overlay.hpp"

#include <windows.h>

#include <joaat.hpp>
#include <RenderTarget.hpp>
#include <Rgb.hpp>
#include <string.hpp>
#include <Thread.hpp>
#include <Window.hpp>

using namespace soup;

#include "main.hpp" // cached prohibition tunables
#include "owf_config.hpp"
#if !LOGGING
#include "owf_console.hpp"
#endif
#include "owf_repo.hpp" // get_overlay_string
#include "owf_tunables.hpp"

static HWND s_game_hwnd = 0;
static Window w;
static bool s_prelogin = true;
static bool s_unreachable = false;
static bool s_topmost = false;
static int s_x = -1;
static int s_y = -1;
static unsigned int s_w = -1;
static unsigned int s_h = -1;

bool owfOverlay::isInited()
{
	return s_game_hwnd != 0;
}

void owfOverlay::init()
{
	const auto game_pid = GetCurrentProcessId();
	EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL
	{
		if (IsWindow(hwnd)
			&& Window(hwnd).getOwnerPid() == static_cast<DWORD>(lparam)
			&& hwnd != GetConsoleWindow()
			)
		{
			// Avoid picking up on a message box
			const auto [width, height] = Window(hwnd).getSize();
			if (height > 400)
			{
				s_game_hwnd = hwnd;
				/*char buf[100];
				GetWindowText(hwnd, buf, 100);
				conout << buf << std::endl;*/
				return FALSE;
			}
		}
		return TRUE;
	}, static_cast<LPARAM>(game_pid));

	if (s_game_hwnd != 0)
	{
		Thread t([](Capture&&)
		{
			while (!IsWindowVisible(s_game_hwnd))
			{
				Sleep(100);
			}
			Sleep(500);

#if !LOGGING
			if (!owfConfig::isConsoleEnabled()
				&& owfConsole::active
				)
			{
				owfConsole::deactivate();
			}
#endif

			//conout << "Creating our window..." << std::endl;
			const auto [width, height] = Window(s_game_hwnd).getSize();
			w = Window::create("OpenWF Overlay", width, height);
			if (!overlay_compatibility_mode)
			{
				SetParent(w.h, s_game_hwnd);
				w.setPos(0, 0);
			}
			w.setDrawFunc([](Window w, RenderTarget& rt)
			{
				rt.fill(Rgb::MAGENTA);
				try
				{
					if (s_prelogin)
					{
						rt.drawText(10 + 2, 10 + 2, "OpenWF", RasterFont::simple8(), Rgb::BLACK, 2);
						rt.drawText(10, 10, "OpenWF", RasterFont::simple8(), Rgb{ 90, 253, 123 }, 2);

						std::string at;
						at.push_back('@');
						at.push_back(' ');
						at.append(server_host);
						rt.drawText(88 + 1, 18 + 1, at, RasterFont::simple8(), Rgb::BLACK, 1);
						rt.drawText(88, 18, at, RasterFont::simple8(), Rgb{ 90, 253, 123 }, 1);

						std::string subtext;
						if (s_unreachable)
						{
							subtext = get_overlay_string("unreachable");
						}
						else
						{
							if (prohibit_skip_mission_start_timer) { soup::string::listAppend(subtext, get_overlay_string("prohibit_skip_mission_start_timer")); }
							if (prohibit_disable_profanity_filter) { soup::string::listAppend(subtext, get_overlay_string("prohibit_disable_profanity_filter")); }
							if (prohibit_fov_override) { soup::string::listAppend(subtext, get_overlay_string("prohibit_fov_override")); }
							if (prohibit_freecam) { soup::string::listAppend(subtext, get_overlay_string("prohibit_freecam")); }
							if (prohibit_teleport) { soup::string::listAppend(subtext, get_overlay_string("prohibit_teleport")); }
							if (prohibit_scripts) { soup::string::listAppend(subtext, get_overlay_string("prohibit_scripts")); }
							if (!subtext.empty())
							{
								subtext.insert(0, get_overlay_string("prohibit"));
							}
							else if (auto e = g_server_tunables.strings.find(soup::joaat::compileTimeHash("motd")); e != g_server_tunables.strings.end())
							{
								subtext = e->second;
							}
						}
						if (!subtext.empty())
						{
							rt.drawText(10 + 1, 33 + 1, subtext, RasterFont::simple8(), Rgb::BLACK, 1);
							rt.drawText(10, 33, subtext, RasterFont::simple8(), Rgb{ 90, 253, 123 }, 1);
						}
					}

					{
						std::lock_guard lock(owfOverlay::mtx);
						if (!owfOverlay::data.empty())
						{
							for (const auto& _item : owfOverlay::data)
							{
								switch (_item->type)
								{
								case DrawItem::RECT:
									{
										auto& item = static_cast<const owfOverlay::Rect&>(*_item);
										rt.drawRect(item.x, item.y, item.width, item.height, Rgb{ item.r, item.g, item.b });
									}
									break;

								case DrawItem::TEXT:
									{
										auto& item = static_cast<const owfOverlay::Text&>(*_item);
										rt.drawText(item.x, item.y, item.text, *item.font, Rgb{ item.r, item.g, item.b }, item.scale);
									}
									break;
								}
							}
						}
					}
				}
				catch (const std::exception& e)
				{
					rt.fill(Rgb::MAGENTA);
					rt.drawText(10, 10, e.what(), RasterFont::simple8(), Rgb::RED, 2);
				}
			});
			w.setInvisibleColour(Rgb::MAGENTA);
			w.setClickThrough(true);
			w.hideFromTaskbar();
			{
				Thread t([](Capture&&)
				{
					for (; IsWindow(s_game_hwnd); Sleep(100))
					{
						const auto [x, y] = Window(s_game_hwnd).getPos();
						const auto [width, height] = Window(s_game_hwnd).getSize();
						const auto topmost = GetForegroundWindow() == s_game_hwnd;

						if (overlay_compatibility_mode)
						{
							if (s_topmost != topmost)
							{
								w.setTopmost(topmost);
								s_topmost = topmost;
							}
							if (s_x != x || s_y != y)
							{
								w.setPos(x, y);
								s_x = x;
								s_y = y;
							}
						}
						if (s_w != width || s_h != height)
						{
							w.setSize(width, height);
							s_w = width;
							s_h = height;
						}
					}
				});
				t.detach();
			}
			w.runMessageLoop();
		});
		t.detach();
	}
}

void owfOverlay::onTunablesRequestFinished(bool ok)
{
	s_unreachable = !ok;
	w.redraw();
}

void owfOverlay::onLoggedIn()
{
	s_prelogin = false;
	w.redraw();
}

void owfOverlay::onLoggedOut()
{
	s_prelogin = true;
	s_unreachable = false;
	w.redraw();
}

void owfOverlay::redraw()
{
	w.redraw();
}

unsigned int owfOverlay::getWidth()
{
	return s_w;
}

unsigned int owfOverlay::getHeight()
{
	return s_h;
}
