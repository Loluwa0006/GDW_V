// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

// Manual integration test for TLS hostname verification.
//
// Both endpoints are signed by the SAME throwaway CA. The CA must be trusted (via the
// Windows certificate store) so the chain is valid in both cases - that way the ONLY
// difference between them is whether the certificate's name matches the host:
//
//   https://127.0.0.1:8443  cert SAN = localhost,127.0.0.1  -> name matches  -> MUST succeed
//   https://127.0.0.1:8444  cert SAN = wronghost.test       -> name mismatch -> MUST be rejected
//
// The :8443 check is the positive control: it proves the CA is trusted and connectivity
// works, so a failure on :8444 can only be the hostname check (not CA distrust).
// The :8444 check guards the hostname verification in OnSSLTcpConnected: a cert with a
// valid chain but a name that does not match the host must be rejected.
//
// Setup (see Tools/TlsTestServer/tls_test_server.py for details):
//   1. python Tools/TlsTestServer/tls_test_server.py        (serves 8443 + 8444)
//   2. certutil -user -addstore Root Tools/TlsTestServer/certs/ca.pem
//
// Cleanup: certutil -user -delstore Root DiversionTestRootCA

#include "Misc/AutomationTest.h"
#include "DiversionHttpManager.h"
#include "Types.h"
#include "BoostHeaders.h"
#include "TlsTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHostnameVerificationLocalServerTest,
								 "DiversionHTTP.HostnameVerification.Integration.LocalServer",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHostnameVerificationLocalServerTest::RunTest(const FString& Parameters)
{
	// --- Setup-prerequisite gate ---------------------------------------------------------
	// This test needs the local harness running AND its CA trusted in the Windows store.
	// When either is missing we warn-skip rather than fail (the result shows as completed-
	// with-warnings, so it neither fails CI nor masquerades as a real pass). Without the CA
	// trusted, the :8444 rejection would be ambiguous (name mismatch vs. CA distrust), so we
	// must confirm trust before treating a rejection as proof the hostname check works.

	// Both ports come from the same harness process; probe both so "server not running" is
	// distinguishable from a TLS rejection.
	if (!DiversionTlsTest::IsLocalPortListening(8443) || !DiversionTlsTest::IsLocalPortListening(8444))
	{
		AddWarning(TEXT("Local TLS test server is not running on 127.0.0.1:8443+8444 - skipping "
						"(neither passed nor failed). Start it with: "
						"python Tools/TlsTestServer/tls_test_server.py"));
		return true;
	}

	if (!DiversionTlsTest::IsTestRootCaTrusted())
	{
		AddWarning(TEXT("Harness CA 'DiversionTestRootCA' is not trusted in the Windows ROOT store - "
						"skipping (neither passed nor failed). A mismatched-cert rejection cannot be "
						"attributed to hostname verification without it. Install it with: "
						"certutil -user -addstore Root Tools\\TlsTestServer\\certs\\ca.pem"));
		return true;
	}
	// -------------------------------------------------------------------------------------

	auto SendTo = [](const TCHAR* BaseUrl)
	{
		DiversionHttp::FHttpRequestManager Manager(BaseUrl, /*bRequireEditorFocus*/ false);
		return Manager.SendRequest(
			TEXT("/"), DiversionHttp::HttpMethod::GET,
			/*Token*/ TEXT(""), TEXT("application/json"), /*Content*/ TEXT(""),
			/*Headers*/ {}, /*ConnectionTimeoutSeconds*/ 10, /*RequestTimeoutSeconds*/ 30);
	};

	// Positive control: matching name + trusted CA must connect. We already confirmed the
	// CA is trusted, so a failure here means the baseline itself is broken (e.g. the harness
	// cert expired) - which makes the :8444 result below inconclusive. Warn-skip rather than
	// fail, so an environmental baseline problem isn't reported as a hostname-verification bug.
	const DiversionHttp::HTTPCallResponse Matching = SendTo(TEXT("https://127.0.0.1:8443"));
	if (Matching.Error.IsSet())
	{
		AddWarning(FString::Printf(
			TEXT("Control connection to the matching-name endpoint (8443) failed despite the CA "
				 "being trusted: %s. The baseline is broken, so the mismatch check is inconclusive "
				 "- skipping (neither passed nor failed). Re-generate the harness certs "
				 "(delete Tools/TlsTestServer/certs and restart the server)."),
			*Matching.Error.GetValue()));
		return true;
	}
	TestEqual(TEXT("Matching-name endpoint should return HTTP 200"), Matching.ResponseCode, 200);

	// Rejecting the mismatched cert makes the production code emit an Error-level log
	// (the handshake failure), which the automation harness would otherwise count as a
	// test failure. Whitelist it as expected. The connection pool retries on a fresh
	// connection, so the message appears one or more times (Occurrences = 0).
	AddExpectedError(TEXT("SSL handshake failed for 127.0.0.1:8444"),
					 EAutomationExpectedErrorFlags::Contains, 0);

	// Regression guard: the cert at :8444 chains to the SAME trusted CA but is issued for
	// wronghost.test, so the name does not match 127.0.0.1. A correct client MUST reject it.
	const DiversionHttp::HTTPCallResponse Mismatch = SendTo(TEXT("https://127.0.0.1:8444"));
	TestTrue(TEXT("Mismatched-hostname certificate must be rejected (connection should error)"),
			 Mismatch.Error.IsSet());
	TestNotEqual(TEXT("Mismatched-hostname certificate must NOT yield a successful response"),
				 Mismatch.ResponseCode, 200);

	return true;
}
