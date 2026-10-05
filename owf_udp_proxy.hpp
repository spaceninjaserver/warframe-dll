#pragma once

#include "owf_console.hpp"
#include "owf_web.hpp"

struct owfUdpProxy
{
	inline static soup::SharedPtr<soup::Worker> downstream;
	inline static soup::SocketAddr downstream_addr;
	inline static soup::SharedPtr<soup::Socket> upstream;
	inline static soup::SocketAddr upstream_addr;

	static void setUpstreamAddr(const soup::SocketAddr& newAddr)
	{
		if (newAddr != owfUdpProxy::upstream_addr)
		{
#if LOGGING
			conout << "owfUdpProxy: New upstream: " << newAddr.toString() << std::endl;
#endif
			const bool bind = owfUdpProxy::upstream_addr.ip.isZero();
			owfUdpProxy::upstream.reset();
			owfUdpProxy::upstream_addr = newAddr;
			if (bind)
			{
				SOUP_IF_UNLIKELY (!owfUdpProxy::bind())
				{
					conout << "Failed to bind UDP/6951.";
				}
			}
		}
	}

	static bool bind()
	{
		return g_serv.bindUdp(6951, [](soup::Socket& s, soup::SocketAddr&& addr, std::string&& data) SOUP_EXCAL
		{
			downstream = g_serv.getShared(s);
			downstream_addr = addr;

			const bool init = !upstream;
			if (init)
			{
				upstream = g_serv.addSocket();
			}

#if LOGGING
			conout << "owfUdpProxy: " << downstream_addr.toString() << " -> " << upstream_addr.toString() << ": " << soup::string::bin2hex(data) << std::endl;
#endif
			if (upstream->udpClientSend(upstream_addr, data))
			{
				upstreamRecv();
			}
		});
	}

	static void upstreamRecv()
	{
		upstream->udpRecv([](soup::Socket&, soup::SocketAddr&& addr, std::string&& data, soup::Capture&&)
		{
			if (upstream && upstream_addr == addr)
			{
#if LOGGING
				conout << "owfUdpProxy: " << upstream_addr.toString() << " -> " << downstream_addr.toString() << ": " << soup::string::bin2hex(data) << std::endl;
#endif
				static_cast<soup::Socket*>(downstream.get())->udpServerSend(downstream_addr, data);
				upstreamRecv();
			}
			else
			{
#if LOGGING
				conout << "owfUdpProxy: Discarding packet from " << addr.toString() << ": " << soup::string::bin2hex(data) << std::endl;
#endif
			}
		});
	}
};
