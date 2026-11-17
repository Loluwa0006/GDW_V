// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

// Manual integration test for the Windows certificate store trust import.
//
// Validates that FHttpRequestManager (whose ConfigureSslContext imports the Windows
// ROOT/CA stores on Windows) can complete a TLS handshake with a server whose CA
// exists ONLY in the Windows certificate store - i.e. NOT in the bundled cacert.pem.
// This is the corporate TLS-inspection scenario.
//
// Setup (see Tools/TlsTestServer/tls_test_server.py for details):
//   1. python Tools/TlsTestServer/tls_test_server.py        (serves https://localhost:8443)
//   2. certutil -user -addstore Root Tools/TlsTestServer/certs/ca.pem
//
// Expected results:
//   - CA installed in store + fix present:  test passes (HTTP 200)
//   - CA not installed (or pre-fix build):  test fails with a connection error, and the
//     log shows "Certificate verification failed at depth ..." from the verify callback
//   - Server not running:                   test is skipped with a warning
//
// Cleanup: certutil -user -delstore Root DiversionTestRootCA

#include "Misc/AutomationTest.h"
#include "DiversionHttpManager.h"
#include "Types.h"
#include "BoostHeaders.h"
#include "TlsTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWindowsStoreTrustLocalServerTest,
								 "DiversionHTTP.WindowsStoreTrust.Integration.LocalServer",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWindowsStoreTrustLocalServerTest::RunTest(const FString& Parameters)
{
	// --- Setup-prerequisite gate ---------------------------------------------------------
	// This test needs the local harness running AND its CA trusted in the Windows ROOT store
	// (and NOT in the bundled cacert.pem - that's the whole point). When the setup is absent
	// we warn-skip rather than fail: the result shows as completed-with-warnings, so it
	// neither fails CI nor masquerades as a real pass. Both the certs and the CA are local
	// and gitignored - each developer generates and trusts them before running this test.

	// Probe plain TCP first so "server not running" is distinguishable from a TLS failure.
	if (!DiversionTlsTest::IsLocalPortListening(8443))
	{
		AddWarning(TEXT("Local TLS test server is not running on 127.0.0.1:8443 - skipping "
						"(neither passed nor failed). Generate the certs and start the server with: "
						"python Tools/TlsTestServer/tls_test_server.py"));
		return true;
	}

	// If the server is up but the CA isn't trusted, the handshake would fail for the WRONG
	// reason (CA distrust, not a broken store import). Detect that and warn-skip with the
	// setup instructions instead of reporting a misleading failure.
	if (!DiversionTlsTest::IsTestRootCaTrusted())
	{
		AddWarning(TEXT("Harness CA 'DiversionTestRootCA' is not trusted in the Windows ROOT store - "
						"skipping (neither passed nor failed). Trust the locally generated CA with: "
						"certutil -user -addstore Root Tools\\TlsTestServer\\certs\\ca.pem "
						"(remove later with: certutil -user -delstore Root DiversionTestRootCA)"));
		return true;
	}
	// -------------------------------------------------------------------------------------

	// Goes through the full production path: ConfigureSslContext (bundled cacert.pem +
	// Windows store import), connection pool, SSL handshake with certificate verification
	DiversionHttp::FHttpRequestManager Manager(TEXT("https://localhost:8443"), /*bRequireEditorFocus*/ false);

	const DiversionHttp::HTTPCallResponse Response = Manager.SendRequest(
		TEXT("/"), DiversionHttp::HttpMethod::GET,
		/*Token*/ TEXT(""), TEXT("application/json"), /*Content*/ TEXT(""),
		/*Headers*/ {}, /*ConnectionTimeoutSeconds*/ 10, /*RequestTimeoutSeconds*/ 30);

	if (Response.Error.IsSet())
	{
		// The CA is confirmed trusted (gated above), so a handshake failure here means the
		// plugin's Windows-store import is NOT picking it up - i.e. the feature under test is
		// broken. This is a genuine failure, not a setup problem.
		AddError(FString::Printf(
			TEXT("Request failed even though 'DiversionTestRootCA' is trusted in the Windows store: %s. "
				 "The Windows certificate-store import appears broken (the bundled cacert.pem does not "
				 "contain this CA, so the import is the only way the chain can validate)."),
			*Response.Error.GetValue()));
		return false;
	}

	TestEqual(TEXT("Local TLS server should return HTTP 200"), Response.ResponseCode, 200);
	TestTrue(TEXT("Response body should contain expected payload"),
			 Response.Contents.Contains(TEXT("\"status\"")));

	return true;
}
