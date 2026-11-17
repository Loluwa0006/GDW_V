// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "HttpSession.h"
#include "BoostHeaders.h"


namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp_stream = beast::tcp_stream;
using ssl_stream = beast::ssl_stream<tcp_stream>;

class FHttpTcpSession final : public FHttpSession<tcp_stream>
{
public:
	explicit FHttpTcpSession(net::io_context& IoContext,
		const std::string& Host,
		const std::string Port,
		const std::chrono::seconds& ConnectionTimeout,
		const std::chrono::seconds& RequestTimeout)
		: FHttpSession<tcp_stream>(IoContext, tcp_stream(net::make_strand(IoContext)), Host, Port, ConnectionTimeout, RequestTimeout)
	{}

	// Helper to get properly-typed shared_ptr for this derived class
	std::shared_ptr<FHttpTcpSession> SharedThis()
	{
		return std::static_pointer_cast<FHttpTcpSession>(shared_from_this());
	}

	std::future<bool> ConnectAsync() override;
	DiversionHttp::HTTPCallResponse ExecuteRequest(const http::request<http::string_body>& InRequest, FString InOutputFilePath = "") override;

	// Socket access for resource management
	beast::tcp_stream& TcpStream() override
	{
		return Stream;
	}

	const beast::tcp_stream& TcpStream() const override
	{
		return Stream;
	}

	void Shutdown() override
	{
		// Gracefully close the socket
		if (Stream.socket().is_open())
		{
			beast::error_code ec;
			Stream.socket().shutdown(net::ip::tcp::socket::shutdown_both, ec);
			// not_connected happens sometimes so don't bother reporting it.
			if (ec && ec != beast::errc::not_connected)
			{
				UE_LOG(LogDiversionHttp, Warning, TEXT("TCP socket shutdown warning: %hs"), ec.message().c_str());
			}
		}
	}

};
