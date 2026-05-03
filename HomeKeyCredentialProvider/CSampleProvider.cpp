//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// CSampleProvider implements ICredentialProvider, which is the main
// interface that logonUI uses to decide which tiles to display.
// This sample illustrates processing asynchronous external events and 
// using them to provide the user with an appropriate set of credentials.
// In this sample, we provide two credentials: one for when the system
// is "connected" and one for when it isn't. When it's "connected", the
// tile provides the user with a field to log in as the administrator.
// Otherwise, the tile asks the user to connect first.
//

#include "common.h"
#include "CSampleProvider.h"

static ULONG HttpThreadProc(__in LPVOID lpParameter) {
	CSampleProvider* instance = static_cast<CSampleProvider*>(lpParameter);

	httplib::Server svr;

	instance->httpServer = &svr;

	svr.Get("/", [&](const httplib::Request& request, httplib::Response& res) {
		std::string password = request.get_param_value("password");

		instance->Unlock(password);
	});

	svr.listen("10.8.1.9", 25563);

	return 0;
}

CSampleProvider::CSampleProvider()
{
	DllAddRef();

	CreateThread(NULL, 0, HttpThreadProc, this, 0, NULL);
}

CSampleProvider::~CSampleProvider()
{
	httpServer->stop();

	DllRelease();
}

void CSampleProvider::Unlock(std::string password)
{
	if (advisee)
	{
		UnlockData unlockData = {
			.scenario = scenario,
			.password = password,
		};

		credential->BeginAuth(unlockData);
		advisee->CredentialsChanged(adviseeContext);
	}
}

HRESULT CSampleProvider::SetUsageScenario(
	__in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
	__in DWORD dwFlags
)
{
	switch (cpus)
	{
	case CPUS_LOGON:
	case CPUS_UNLOCK_WORKSTATION:
		scenario = cpus;

		if (!credential)
		{
			credential = new CMessageCredential();
		}

		return S_OK;

	default:
		return E_NOTIMPL;
	}
}


HRESULT CSampleProvider::Advise(
	__in ICredentialProviderEvents* pcpe,
	__in UINT_PTR upAdviseContext
)
{
	if (advisee != NULL)
	{
		advisee->Release();
	}
	advisee = pcpe;
	advisee->AddRef();
	adviseeContext = upAdviseContext;
	return S_OK;
}

HRESULT CSampleProvider::UnAdvise()
{
	if (advisee != NULL)
	{
		advisee->Release();
		advisee = NULL;
	}
	return S_OK;
}

HRESULT CSampleProvider::GetFieldDescriptorCount(
	__out DWORD* pdwCount
)
{
	*pdwCount = 0;
	return S_OK;
}

HRESULT CSampleProvider::GetFieldDescriptorAt(
	__in DWORD dwIndex,
	__deref_out CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd
)
{
	return E_NOTIMPL;
}

HRESULT CSampleProvider::GetCredentialCount(
	__out DWORD* pdwCount,
	__out_range(< , *pdwCount) DWORD* pdwDefault,
	__out BOOL* pbAutoLogonWithDefault
)
{
	*pdwCount = 1;
	*pdwDefault = 0;
	*pbAutoLogonWithDefault = FALSE;
	return S_OK;
}

HRESULT CSampleProvider::GetCredentialAt(
	__in DWORD dwIndex,
	__deref_out ICredentialProviderCredential** ppcpc
)
{
	*ppcpc = credential;
	return S_OK;
}

HRESULT CSample_CreateInstance(__in REFIID riid, __in void** ppv)
{
	HRESULT hr;

	CSampleProvider* pProvider = new CSampleProvider();

	if (pProvider)
	{
		hr = pProvider->QueryInterface(riid, ppv);
		pProvider->Release();
	}
	else
	{
		hr = E_OUTOFMEMORY;
	}

	return hr;
}

HRESULT CSampleProvider::SetSerialization(
	__in const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs
)
{
	UNREFERENCED_PARAMETER(pcpcs);
	return E_NOTIMPL;
}