// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "HttpSession.h"
#include "BoostHeaders.h"


namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp_stream = beast::tcp_stream;
using ssl_stream = beast::ssl_stream<tcp_stream>;


class FHttpSSLSession final : public FHttpSession<ssl_stream>
{
public:
	explicit FHttpSSLSession(net::io_context& IoContext,
		net::ssl::context& SslContext,
		const std::string& Host,
		const std::string Port,
		const std::chrono::seconds& ConnectionTimeout,
		const std::chrono::seconds& RequestTimeout)
		: FHttpSession<ssl_stream>(IoContext, ssl_stream(net::make_strand(IoContext), SslContext), Host, Port, ConnectionTimeout, RequestTimeout)
	{}

	// Helper to get properly-typed shared_ptr for this derived class
	std::shared_ptr<FHttpSSLSession> SharedThis()
	{
		return std::static_pointer_cast<FHttpSSLSession>(shared_from_this());
	}

	std::future<bool> ConnectAsync() override;
	DiversionHttp::HTTPCallResponse ExecuteRequest(const http::request<http::string_body>& InRequest, FString InOutputFilePath = "") override;


	// SSL validation support
	SSL* GetSSLHandle() const { return const_cast<ssl_stream&>(Stream).native_handle(); }

	// Socket access for resource management
	tcp_stream& TcpStream() override;
	const tcp_stream& TcpStream() const override;
	void Shutdown() override;


private:
	std::atomic<bool> bSSLHandshakeCompleted{false};

	void PerformRequest() override;

	// SSL-specific connection callbacks
	void OnSSLResolveForConnection(beast::error_code ec, net::ip::tcp::resolver::results_type results);
	void OnSSLTcpConnected(beast::error_code ec, net::ip::tcp::resolver::results_type::endpoint_type);
	void OnSSLHandshakeComplete(beast::error_code ec);

	// Configure OpenSSL to require the presented certificate's name to match Host before the
	// handshake. Returns false (after logging) if it can't be set up, so the caller fails closed.
	bool ConfigureHostnameVerification(SSL* Ssl) const;

};
