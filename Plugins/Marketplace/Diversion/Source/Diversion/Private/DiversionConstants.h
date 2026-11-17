// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

// Define constants for plugin wide use

constexpr float	SECONDS_TO_POLL_STATUS = 5.f;
constexpr float SECONDS_TO_POLL_POTENTIAL_CLASHES = 60.f;
constexpr float SECONDS_TO_POLL_CONFLICTED_FILES = 20.f;
/** How old a resolved hard-locks eligibility (and lockable-pattern list) may get before the
 * potential-clashes poll re-fetches it; an unresolved one is retried every poll */
constexpr float SECONDS_TO_REFRESH_HARD_LOCKS_ELIGIBILITY = 600.f;

#define DIVERSION_APP_URL "diversion://"
#define DIVERSION_WEB_URL "https://app.diversion.dev/"

// Core API endpoint
#define DIVERSION_API_HOST TEXT("api.diversion.dev")
#define DIVERSION_API_PORT TEXT("443")
