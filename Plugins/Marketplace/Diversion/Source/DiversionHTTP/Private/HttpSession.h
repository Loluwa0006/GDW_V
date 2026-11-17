// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "BoostHeaders.h"
#include "Types.h"
#include "DiversionHttpModule.h"
#include "DiversionCommonUtils.h"
#include "Misc/Paths.h"

#include <iostream>
#include <fstream>
#include <cstdio>
#include <string>
#include <future>
#include <mutex>

#if PLATFORM_WINDOWS
#include <mstcpip.h>
#elif PLATFORM_LINUX || PLATFORM_MAC
#include <netinet/tcp.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp_stream = beast::tcp_stream;
using ssl_stream = beast::ssl_stream<tcp_stream>;

// Helper functions
template <typename ResponseType>
TMap<FString, FString> ExtractResponseHeaders(const http::response<ResponseType>& InResponse)
{
	TMap<FString, FString> Headers;
	for (auto const& header : InResponse)
	{
		auto HeaderNameStringView = boost::beast::http::to_string(header.name());
		FString HeaderName = DiversionHttp::ConvertToFstring(HeaderNameStringView.data());
		FString HeaderValue = DiversionHttp::ConvertToFstring(header.value().data(), header.value().size());
		HeaderName = HeaderName.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));
		HeaderValue = HeaderValue.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));
		Headers.Add(HeaderName, HeaderValue);
	}

	return Headers;
}

bool DecompressGzipWithZlib(const std::string& CompressedFilePath, const std::string& DecompressedFilePath, std::string& OutError);

void ConvertHttpResponseToTArray(const http::response<http::string_body>& HttpResponse, TArray<uint8>& OutArray);

TArray<uint8> DecompressGzipFromArray(const TArray<uint8>& CompressedData, int32 ExpectedDecompressedSize);

uint32 GetUncompressedSizeFromGzip(const TArray<uint8>& GzipData);


template <typename StreamType>
class FHttpSession : public TSharedFromThis<FHttpSession<StreamType>>,
					  public std::enable_shared_from_this<FHttpSession<StreamType>>
{
public:
	explicit FHttpSession(net::io_context& IoContext,
		StreamType Stream,
		const std::string& Host,
		const std::string Port,
		const std::chrono::seconds& ConnectionTimeout,
		const std::chrono::seconds& RequestTimeout)
		: ConnectionTimeout(ConnectionTimeout), RequestTimeout(RequestTimeout), Compression(),
		  Stream(std::move(Stream)), Host(Host),
		  Port(Port), IoContext(IoContext), Resolver(net::make_strand(IoContext))
	{
	}

	virtual ~FHttpSession() = default;

	// Connection lifecycle methods
	virtual std::future<bool> ConnectAsync() = 0;
	virtual DiversionHttp::HTTPCallResponse ExecuteRequest(const http::request<http::string_body>& InRequest, FString InOutputFilePath = "") = 0;

	void OnWrite(beast::error_code ec, std::size_t bytes_transferred);

	// Connection pooling support
	virtual void SetConnectionTimeout(std::chrono::seconds Timeout) { ConnectionTimeout = Timeout; }
	virtual void SetRequestTimeout(std::chrono::seconds Timeout) { RequestTimeout = Timeout; }
	virtual std::chrono::seconds GetRequestTimeout() const { return RequestTimeout; }
	virtual bool ResetState()
	{
		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("HttpSession %p: ResetState called"), this);

		// Use a mutex to protect HTTP object access during reset
		std::lock_guard<std::mutex> ResetLock(ResetMutex);

		try
		{
			// Reset promise/future for reuse
			response_promise = std::promise<DiversionHttp::HTTPCallResponse>{};

			// Reset response value members individually to avoid container iteration issues
			ResponseValue.Contents.Empty();
			if (ResponseValue.Error.IsSet())
			{
				ResponseValue.Error.Reset();
			}

			// Safely clear headers - this is where crashes often happen
			if (ResponseValue.Headers.Num() > 0)
			{
				UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("HttpSession %p: Clearing %d headers"), this, ResponseValue.Headers.Num());
				ResponseValue.Headers.Empty();
			}
			ResponseValue.ResponseCode = 0;

			// Clear all buffers and HTTP state - protected by mutex
			Buffer.clear();

			// Create fresh HTTP objects instead of clearing existing ones to avoid iterator corruption
			Request = http::request<http::string_body>{};
			Response = http::response<http::string_body>{};

			// Reset file handling state
			Compression = CompressionType::None;
			CompressedFilePath.Empty();
			OutputFilePath.Empty();

			UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("HttpSession %p: ResetState completed successfully"), this);
			return true;
		}
		catch (const std::exception& Ex)
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("HttpSession %p: ResetState failed: %s"), this, UTF8_TO_TCHAR(Ex.what()));
			return false;
		}
		catch (...)
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("HttpSession %p: ResetState failed with unknown exception"), this);
			return false;
		}
	}
	virtual bool IsSocketOpen() const { return TcpStream().socket().is_open(); }

protected:

	virtual tcp_stream& TcpStream() = 0;
	virtual const tcp_stream& TcpStream() const = 0;
	virtual void Shutdown() = 0;
	virtual void PerformRequest();
	void EnableSocketKeepAlive();

	void LogTimeoutErrorIfExists(const beast::error_code& ec) const;

private:
	// Connection establishment callbacks
	void OnResolveForConnection(beast::error_code ec, net::ip::tcp::resolver::results_type results);
	void OnConnectComplete(beast::error_code ec, net::ip::tcp::resolver::results_type::endpoint_type);

	// Request execution callbacks
	void OnResolveForRequest(beast::error_code ec, net::ip::tcp::resolver::results_type results);
	void OnRequestConnect(beast::error_code ec, net::ip::tcp::resolver::results_type::endpoint_type);
	void OnReadStringResponse(beast::error_code ec, std::size_t bytes_transferred);
	void OnReadFileResponseHeaders(beast::error_code ec, std::size_t bytes_transferred);
	void OnReadFileResponseBody(beast::error_code ec, std::size_t bytes_transferred);


protected:
	std::chrono::seconds ConnectionTimeout;
	std::chrono::seconds RequestTimeout;

	beast::flat_buffer Buffer;
	http::request<http::string_body> Request;
	http::response<http::string_body> Response;
	enum class CompressionType
	{
		None,
		Gzip
	} Compression;
	FString CompressedFilePath;
	FString OutputFilePath;
	http::response_parser<http::file_body> FileResponse;

	// Connection state
	enum class ConnectionState { Disconnected, Connecting, Connected, Failed };
	std::atomic<ConnectionState> State{ConnectionState::Disconnected};

	// Connection establishment mechanism
	std::promise<bool> connection_promise;

	// Request/response mechanism
	DiversionHttp::HTTPCallResponse ResponseValue;
	std::promise<DiversionHttp::HTTPCallResponse> response_promise;

	StreamType Stream;
	const std::string Host;
	const std::string Port;

protected:
	net::io_context& IoContext;
	net::ip::tcp::resolver  Resolver;
	std::chrono::time_point<std::chrono::system_clock> StartTime;

	// Thread safety for connection reset operations
	mutable std::mutex ResetMutex;
};

// Connection establishment - async, returns future<bool>
template <typename StreamType>
std::future<bool> FHttpSession<StreamType>::ConnectAsync()
{
	if (State.load() != ConnectionState::Disconnected)
	{
		// Already connected or in process
		std::promise<bool> immediate_promise;
		immediate_promise.set_value(State.load() == ConnectionState::Connected);
		return immediate_promise.get_future();
	}

	State.store(ConnectionState::Connecting);
	connection_promise = std::promise<bool>{};
	auto connection_future = connection_promise.get_future();

	Resolver.async_resolve(
		Host,
		Port,
		beast::bind_front_handler(&FHttpSession<StreamType>::OnResolveForConnection, this->shared_from_this()));

	return connection_future;
}

// Request execution - synchronous, assumes connection is established
template <typename StreamType>
DiversionHttp::HTTPCallResponse FHttpSession<StreamType>::ExecuteRequest(const http::request<http::string_body>& InRequest, FString InOutputFilePath)
{
	UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("HttpSession %p: ExecuteRequest called, State=%d"), this, (int)State.load());

	if (State.load() != ConnectionState::Connected)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("HttpSession %p: ExecuteRequest called but connection not established (State=%d)"), this, (int)State.load());
		return DiversionHttp::HTTPCallResponse(TEXT("Connection not established"));
	}

	// Validate socket is still open
	try
	{
		if (!IsSocketOpen())
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("HttpSession %p: Socket is not open"), this);
			return DiversionHttp::HTTPCallResponse(TEXT("Socket connection lost"));
		}
	}
	catch (...)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("HttpSession %p: Failed to check socket status"), this);
		return DiversionHttp::HTTPCallResponse(TEXT("Failed to validate connection"));
	}


	// Reset response value members individually to avoid container iteration issues
	ResponseValue.Contents.Empty();
	ResponseValue.Error.Reset();
	ResponseValue.Headers.Empty();
	ResponseValue.ResponseCode = 0;

	try
	{
		// Protect HTTP object access with the same mutex used in ResetState
		std::lock_guard<std::mutex> RequestLock(ResetMutex);

		// Reset promise for new request
		response_promise = std::promise<DiversionHttp::HTTPCallResponse>{};

		OutputFilePath = InOutputFilePath;
		Request = InRequest;
		StartTime = std::chrono::system_clock::now();

		// Clear buffers for new request
		Buffer.clear();
		Response = http::response<http::string_body>{};

		auto response_future = response_promise.get_future();

		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("HttpSession %p: Starting PerformRequest"), this);

		// Start request execution - this is where crashes often happen
		PerformRequest();

		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("HttpSession %p: Waiting for response"), this);

		// Wait until the request completes
		return response_future.get();
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("HttpSession %p: ExecuteRequest failed: %s"), this, UTF8_TO_TCHAR(Ex.what()));
		return DiversionHttp::HTTPCallResponse(FString::Printf(TEXT("Request execution failed: %s"), UTF8_TO_TCHAR(Ex.what())));
	}
	catch (...)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("HttpSession %p: ExecuteRequest failed with unknown exception"), this);
		return DiversionHttp::HTTPCallResponse(TEXT("Request execution failed with unknown error"));
	}
}


// Connection establishment callbacks
template <typename StreamType>
void FHttpSession<StreamType>::OnResolveForConnection(beast::error_code ec, net::ip::tcp::resolver::results_type results)
{
	if (ec)
	{
		State.store(ConnectionState::Failed);
		connection_promise.set_value(false);
		return;
	}

	// Set a timeout on the operation
	TcpStream().expires_after(ConnectionTimeout);
	TcpStream().async_connect(results,
		beast::bind_front_handler(&FHttpSession<StreamType>::OnConnectComplete, this->shared_from_this()));
}


template <typename StreamType>
void FHttpSession<StreamType>::OnConnectComplete(beast::error_code ec, net::ip::tcp::resolver::results_type::endpoint_type)
{
	if (ec)
	{
		LogTimeoutErrorIfExists(ec);
		State.store(ConnectionState::Failed);
		connection_promise.set_value(false);
		return;
	}

	// TCP connection established - derived classes may need additional setup (like SSL handshake)
	EnableSocketKeepAlive();
	State.store(ConnectionState::Connected);
	connection_promise.set_value(true);
}


template <typename StreamType>
void FHttpSession<StreamType>::PerformRequest()
{
	TcpStream().expires_after(RequestTimeout);

	http::async_write(Stream, Request, beast::bind_front_handler(&FHttpSession<StreamType>::OnWrite, this->shared_from_this()));
}


template <typename StreamType>
void FHttpSession<StreamType>::OnWrite(beast::error_code ec, std::size_t bytes_transferred)
{

	boost::ignore_unused(bytes_transferred);

	if (ec)
	{
		LogTimeoutErrorIfExists(ec);
		response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR(("Write error: " + ec.message()).c_str())));
		return;
	}

	if (OutputFilePath.IsEmpty())
	{
		// Parse the response directly into a string
		http::async_read(Stream, Buffer, Response, beast::bind_front_handler(&FHttpSession::OnReadStringResponse, this->shared_from_this()));
	}
	else
	{
		beast::error_code file_ec;
		CompressedFilePath = FPaths::GetPath(OutputFilePath) / TEXT("compressed_response.gz");
		FileResponse.get().body().open(TCHAR_TO_UTF8(*CompressedFilePath), beast::file_mode::write, file_ec);
		if (file_ec)
		{
			response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR(("Failed opening output file: " + ec.message()).c_str())));
			return;
		}

		http::async_read_header(Stream, Buffer, FileResponse, beast::bind_front_handler(&FHttpSession<StreamType>::OnReadFileResponseHeaders, this->shared_from_this()));
	}
}


template <typename StreamType>
void FHttpSession<StreamType>::OnReadStringResponse(beast::error_code ec, std::size_t bytes_transferred)
{

	boost::ignore_unused(bytes_transferred);

	if (ec)
	{
		LogTimeoutErrorIfExists(ec);
		response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR(("String read error: " + ec.message()).c_str())));
		return;
	}

	// 204 No Content has no body by spec; skip decompression even if infrastructure adds Content-Encoding: gzip
	if (Response.result_int() == 204)
	{
		ResponseValue = DiversionHttp::HTTPCallResponse(FString(), Response.result_int(), ExtractResponseHeaders(Response));
	}
	else if (Response[http::field::content_encoding] == "gzip")
	{
		TArray<uint8> ResponseBody;
		TArray<uint8> DecompressedBody;
		ConvertHttpResponseToTArray(Response, ResponseBody);
		DecompressedBody = DecompressGzipFromArray(ResponseBody, GetUncompressedSizeFromGzip(ResponseBody));
		if (DecompressedBody.IsEmpty())
		{
			response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR("Failed to decompress response body")));
			return;
		}
		// Convert decompressed UTF-8 data to FString with explicit size handling
		FString ResponseString = DiversionUtils::UTF8ToFStringSafe(DecompressedBody.GetData(), DecompressedBody.Num());
		ResponseValue = DiversionHttp::HTTPCallResponse(ResponseString, Response.result_int(), ExtractResponseHeaders(Response));
	}
	else
	{
		const auto& body = Response.body();
		FString ResponseString = DiversionUtils::UTF8ToFStringSafe(body.data(), body.size());
		ResponseValue = DiversionHttp::HTTPCallResponse(ResponseString, Response.result_int(), ExtractResponseHeaders(Response));
	}

	// Consume any leftover data in buffer to prevent bleeding into next request
	Buffer.consume(Buffer.size());

	response_promise.set_value(ResponseValue);
}


template <typename StreamType>
void FHttpSession<StreamType>::OnReadFileResponseHeaders(beast::error_code ec, std::size_t bytes_transferred)
{

	boost::ignore_unused(bytes_transferred);

	if (ec)
	{
		LogTimeoutErrorIfExists(ec);
		response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR(("File headers read error: " + ec.message()).c_str())));
		return;
	}

	// Set the content encoding according to the headers values
	const boost::beast::string_view ContentEncoding = FileResponse.get()[http::field::content_encoding];
	if (ContentEncoding == "gzip")
	{
		Compression = CompressionType::Gzip;
	}
	else if (ContentEncoding == "")
	{
		Compression = CompressionType::None;
	}
	else
	{
		response_promise.set_value(DiversionHttp::HTTPCallResponse(FString("File headers read error: Unsupported content encodng")));
		return;
	}

	http::async_read(Stream, Buffer, FileResponse, beast::bind_front_handler(&FHttpSession<StreamType>::OnReadFileResponseBody, this->shared_from_this()));
}


template <typename StreamType>
void FHttpSession<StreamType>::OnReadFileResponseBody(beast::error_code ec, std::size_t bytes_transferred)
{
	boost::ignore_unused(bytes_transferred);

	if (ec)
	{
		LogTimeoutErrorIfExists(ec);
		response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR(("File read error: " + ec.message()).c_str())));
		return;
	}

	FileResponse.get().body().close();

	if (Compression == CompressionType::Gzip)
	{
		std::string DecompressionError;
		bool res = DecompressGzipWithZlib(TCHAR_TO_UTF8(*CompressedFilePath), TCHAR_TO_UTF8(*OutputFilePath), DecompressionError);
		if (!res)
		{
			response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR(("Decompression error: " + DecompressionError).c_str())));
			return;
		}
	}
	else if (Compression == CompressionType::None)
	{
		// Rename the compressed file to the output file
		int result = std::rename(TCHAR_TO_UTF8(*CompressedFilePath), TCHAR_TO_UTF8(*OutputFilePath));
		if (result != 0)
		{
			std::error_code rename_ec(errno, std::system_category());
			std::string errorMessage = rename_ec.message();
			response_promise.set_value(DiversionHttp::HTTPCallResponse(UTF8_TO_TCHAR(("Failed to rename file: " + errorMessage).c_str())));
			return;
		}
	}
	else
	{
		response_promise.set_value(DiversionHttp::HTTPCallResponse(TEXT("Failed Decompression downloaded file. Unsupported compression format.")));
		return;
	}

	// Return the result path to the output file as a validation mechanism
	ResponseValue = DiversionHttp::HTTPCallResponse(OutputFilePath, Response.result_int(), ExtractResponseHeaders(Response));

	response_promise.set_value(ResponseValue);
}

template <typename StreamType>
void FHttpSession<StreamType>::LogTimeoutErrorIfExists(const beast::error_code& ec) const
{
	if(ec == boost::beast::error::timeout)
	{
		const auto DeltaTimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now() - StartTime).count();

		if (!Request.target().empty())
		{
			std::string TargetUrl = Request.target().data();
			UE_LOG(LogDiversionHttp, Error, TEXT("Call to url: %hs timed out after %lld seconds"),
				TargetUrl.c_str(), DeltaTimeSeconds);
		}
		else
		{
			// During connection phase we might timeout without having a request set up
			UE_LOG(LogDiversionHttp, Error, TEXT("Connection to %hs:%hs timed out after %lld seconds"),
				Host.c_str(), Port.c_str(), DeltaTimeSeconds);
		}
	}
}

template <typename StreamType>
void FHttpSession<StreamType>::EnableSocketKeepAlive()
{
	boost::system::error_code ec;
	auto& Socket = TcpStream().socket();
	auto NativeHandle = Socket.native_handle();

	Socket.set_option(boost::asio::socket_base::keep_alive(true), ec);
	if (ec)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("HttpSession %p: Failed to enable SO_KEEPALIVE: %hs"), this, ec.message().c_str());
		return;
	}

#if PLATFORM_WINDOWS
	// Windows: use WSAIoctl with SIO_KEEPALIVE_VALS
	tcp_keepalive KeepAliveVals = {};
	KeepAliveVals.onoff = 1;
	KeepAliveVals.keepalivetime = 30000;      // 30s before first probe
	KeepAliveVals.keepaliveinterval = 1000;   // 1s between probes
	DWORD BytesReturned = 0;
	if (WSAIoctl(NativeHandle, SIO_KEEPALIVE_VALS, &KeepAliveVals, sizeof(KeepAliveVals),
		nullptr, 0, &BytesReturned, nullptr, nullptr) != 0)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("HttpSession %p: Failed to set keepalive params (WSAIoctl error %d)"), this, WSAGetLastError());
		return;
	}
#elif PLATFORM_LINUX || PLATFORM_MAC
	int IdleTime = 30;   // 30s before first probe
	int Interval = 1;    // 1s between probes
	int ProbeCount = 5;  // 5 failed probes to declare dead

#if PLATFORM_MAC
	if (setsockopt(NativeHandle, IPPROTO_TCP, TCP_KEEPALIVE, &IdleTime, sizeof(IdleTime)) < 0)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("HttpSession %p: Failed to set TCP_KEEPALIVE: %hs"), this, strerror(errno));
		return;
	}
#else
	if (setsockopt(NativeHandle, IPPROTO_TCP, TCP_KEEPIDLE, &IdleTime, sizeof(IdleTime)) < 0)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("HttpSession %p: Failed to set TCP_KEEPIDLE: %hs"), this, strerror(errno));
		return;
	}
#endif
	if (setsockopt(NativeHandle, IPPROTO_TCP, TCP_KEEPINTVL, &Interval, sizeof(Interval)) < 0)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("HttpSession %p: Failed to set TCP_KEEPINTVL: %hs"), this, strerror(errno));
		return;
	}
	if (setsockopt(NativeHandle, IPPROTO_TCP, TCP_KEEPCNT, &ProbeCount, sizeof(ProbeCount)) < 0)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("HttpSession %p: Failed to set TCP_KEEPCNT: %hs"), this, strerror(errno));
		return;
	}
#endif

	UE_LOG(LogDiversionHttp, Verbose, TEXT("HttpSession %p: TCP keepalive enabled (idle=30s, interval=1s)"), this);
}


