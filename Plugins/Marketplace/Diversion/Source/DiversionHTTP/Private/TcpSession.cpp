// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "TcpSession.h"

using namespace DiversionHttp;

std::future<bool> FHttpTcpSession::ConnectAsync()
{
	// TCP sessions can use the base class connection logic directly
	return FHttpSession<tcp_stream>::ConnectAsync();
}

HTTPCallResponse FHttpTcpSession::ExecuteRequest(const http::request<http::string_body>& InRequest, FString InOutputFilePath)
{
	// TCP sessions can use the base class request logic directly
	return FHttpSession<tcp_stream>::ExecuteRequest(InRequest, InOutputFilePath);
}
