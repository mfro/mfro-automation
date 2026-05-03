#pragma once

#define WIN32_LEAN_AND_MEAN

#include <credentialprovider.h>
#include <ntsecapi.h>
#define SECURITY_WIN32
#include <security.h>
#include <intsafe.h>
#include <shlwapi.h>
#include <string>
#include "guid.h"

struct UnlockData
{
	CREDENTIAL_PROVIDER_USAGE_SCENARIO scenario;
	std::string password;
};
