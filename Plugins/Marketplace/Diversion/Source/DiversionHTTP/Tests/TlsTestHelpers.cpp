// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "TlsTestHelpers.h"
#include "BoostHeaders.h"

#if PLATFORM_WINDOWS
// windows.h is already pulled in via BoostHeaders.h (boost::asio); wincrypt only adds the cert store API
#include <wincrypt.h>
// wincrypt.h defines macros that collide with OpenSSL type names; drop them so any
// future use of the OpenSSL types in this file doesn't fail confusingly
#undef X509_NAME
#undef X509_EXTENSIONS
#undef X509_CERT_PAIR
#undef OCSP_REQUEST
#undef OCSP_RESPONSE
#undef PKCS7_ISSUER_AND_SERIAL
#undef PKCS7_SIGNER_INFO
#endif

namespace DiversionTlsTest
{
	bool IsLocalPortListening(uint16 Port)
	{
		boost::asio::io_context ProbeIo;
		boost::asio::ip::tcp::socket ProbeSocket(ProbeIo);
		boost::system::error_code Ec;
		ProbeSocket.connect(boost::asio::ip::tcp::endpoint(
			boost::asio::ip::make_address("127.0.0.1"), Port), Ec);
		const bool bConnected = !Ec;
		ProbeSocket.close(Ec);
		return bConnected;
	}

	bool IsTestRootCaTrusted()
	{
#if PLATFORM_WINDOWS
		// Look in the per-user ROOT store, which is where the harness instructions install
		// the throwaway CA (certutil -user -addstore Root). CertOpenSystemStoreW with
		// hProv = 0 opens the CurrentUser view, which also reflects LocalMachine roots.
		HCERTSTORE Store = CertOpenSystemStoreW(0, L"ROOT");
		if (!Store)
		{
			return false;
		}

		bool bFound = false;
		PCCERT_CONTEXT Ctx = nullptr;
		while ((Ctx = CertEnumCertificatesInStore(Store, Ctx)) != nullptr)
		{
			WCHAR Name[256] = { 0 };
			// CERT_NAME_SIMPLE_DISPLAY_TYPE yields the subject CN (e.g. "DiversionTestRootCA")
			if (CertGetNameStringW(Ctx, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, Name, UE_ARRAY_COUNT(Name)) > 1
				&& FCStringWide::Strcmp(Name, TestRootCaCommonName) == 0)
			{
				bFound = true;
				CertFreeCertificateContext(Ctx);
				break;
			}
		}

		CertCloseStore(Store, 0);
		return bFound;
#else
		return false;
#endif
	}
}
