// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// Shared setup-detection helpers for the manual TLS integration tests
// (WindowsStoreTrustTest, HostnameVerificationTest). Both tests depend on the local
// harness in Tools/TlsTestServer being running AND its throwaway CA being trusted in
// the Windows certificate store. These helpers let the tests detect when that setup is
// absent and warn-skip (neither fail nor cleanly pass) instead of producing a
// misleading result.
namespace DiversionTlsTest
{
	// Throwaway CA subject that Tools/TlsTestServer/tls_test_server.py generates
	// (-subj "/CN=DiversionTestRootCA"). Trust it with:
	//   certutil -user -addstore Root Tools/TlsTestServer/certs/ca.pem
	inline const TCHAR* TestRootCaCommonName = TEXT("DiversionTestRootCA");

	// True if a plain TCP connection to 127.0.0.1:Port succeeds (i.e. the harness is up).
	bool IsLocalPortListening(uint16 Port);

	// True if the harness CA (TestRootCaCommonName) is trusted in the Windows ROOT store.
	// On non-Windows this can't be checked the same way, so it returns false and callers
	// warn-skip - these tests target the Windows certificate-store trust scenario.
	bool IsTestRootCaTrusted();
}
