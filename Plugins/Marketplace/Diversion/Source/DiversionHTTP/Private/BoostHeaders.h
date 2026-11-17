// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#define UI UI_ST
THIRD_PARTY_INCLUDES_START

// Save the current definitions of the macros
#pragma push_macro("check")
#pragma push_macro("verify")
#pragma push_macro("TEXT")
#pragma push_macro("MIN")
#pragma push_macro("MAX")
#pragma warning(push)
#pragma warning(disable : 4702) // unreachable code

// Undefine the macros to prevent conflicts
#undef check
#undef verify
#undef TEXT
#undef MIN
#undef MAX

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/strand.hpp>

// Restore the original macro definitions
#pragma pop_macro("MAX")
#pragma pop_macro("MIN")
#pragma pop_macro("TEXT")
#pragma pop_macro("verify")
#pragma pop_macro("check")
#pragma warning(pop)

THIRD_PARTY_INCLUDES_END
#undef UI
