//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//
#include "MessageCredential.h"

CMessageCredential::CMessageCredential()
{
	DllAddRef();
}

CMessageCredential::~CMessageCredential()
{
	DllRelease();
}

HRESULT CMessageCredential::SetSelected(__out BOOL* pbAutoLogon)
{
	*pbAutoLogon = this->beginAuth;

	return S_OK;
}

HRESULT CMessageCredential::SetDeselected()
{
	return S_OK;
}

HRESULT CMessageCredential::GetStringValue(
	__in DWORD dwFieldID,
	__deref_out PWSTR* ppwsz
)
{
	return SHStrDupW(L"", ppwsz);
}

void CMessageCredential::BeginAuth(UnlockData data) {
	this->unlockData = data;
	this->beginAuth = true;
}

// We're not providing a way to log on from this credential, so we don't need serialization.
HRESULT CMessageCredential::GetSerialization(
	__out CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* response,
	__out CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* result,
	__deref_out_opt PWSTR* ppwszOptionalStatusText,
	__out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
)
{
	if (beginAuth) {
		beginAuth = false;

		std::wstring password(unlockData.password.begin(), unlockData.password.end());

		PWSTR protectedPassword;
		ProtectIfNecessaryAndCopyPassword(password.c_str(), unlockData.scenario, &protectedPassword);

		KERB_INTERACTIVE_UNLOCK_LOGON logon;
		KerbInteractiveUnlockLogonInit(L"mfro-desktop", L"Max", protectedPassword, unlockData.scenario, &logon);

		KerbInteractiveUnlockLogonPack(logon, &result->rgbSerialization, &result->cbSerialization);

		ULONG authPackage;
		RetrieveNegotiateAuthPackage(&authPackage);

		result->ulAuthenticationPackage = authPackage;
		result->clsidCredentialProvider = CLSID_CSample;
		*response = CPGSR_RETURN_CREDENTIAL_FINISHED;
	}

	return S_OK;
}

// LogonUI calls this in order to give us a callback in case we need to notify it of
// anything, such as for getting and setting values.
HRESULT CMessageCredential::Advise(
	__in ICredentialProviderCredentialEvents* pcpce
)
{
	return E_NOTIMPL;
}

// LogonUI calls this to tell us to release the callback.
HRESULT CMessageCredential::UnAdvise()
{
	return E_NOTIMPL;
}

HRESULT CMessageCredential::GetFieldState(
	DWORD dwFieldID,
	CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
	CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis
)
{
	return E_NOTIMPL;
}

// Called to request the image value of the indicated field.
HRESULT CMessageCredential::GetBitmapValue(
	__in DWORD dwFieldID,
	__out HBITMAP* phbmp
)
{
	return E_NOTIMPL;
}

// Since this credential isn't intended to provide a way for the user to submit their
// information, we do without a Submit button.
HRESULT CMessageCredential::GetSubmitButtonValue(
	__in DWORD dwFieldID,
	__out DWORD* pdwAdjacentTo
)
{
	return E_NOTIMPL;
}

// Our credential doesn't have any settable strings.
HRESULT CMessageCredential::SetStringValue(
	__in DWORD dwFieldID,
	__in PCWSTR pwz
)
{
	return E_NOTIMPL;
}

// Our credential doesn't have any checkable boxes.
HRESULT CMessageCredential::GetCheckboxValue(
	__in DWORD dwFieldID,
	__out BOOL* pbChecked,
	__deref_out PWSTR* ppwszLabel
)
{
	return E_NOTIMPL;
}

// Our credential doesn't have a checkbox.
HRESULT CMessageCredential::SetCheckboxValue(
	__in DWORD dwFieldID,
	__in BOOL bChecked
)
{
	return E_NOTIMPL;
}

// Our credential doesn't have a combobox.
HRESULT CMessageCredential::GetComboBoxValueCount(
	__in DWORD dwFieldID,
	__out DWORD* pcItems,
	__out_range(< , *pcItems) DWORD* pdwSelectedItem
)
{
	return E_NOTIMPL;
}

// Our credential doesn't have a combobox.
HRESULT CMessageCredential::GetComboBoxValueAt(
	__in DWORD dwFieldID,
	__out DWORD dwItem,
	__deref_out PWSTR* ppwszItem
)
{
	return E_NOTIMPL;
}

// Our credential doesn't have a combobox.
HRESULT CMessageCredential::SetComboBoxSelectedValue(
	__in DWORD dwFieldId,
	__in DWORD dwSelectedItem
)
{
	return E_NOTIMPL;
}

// Our credential doesn't have a command link.
HRESULT CMessageCredential::CommandLinkClicked(__in DWORD dwFieldID)
{
	return E_NOTIMPL;
}

// We're not providing a way to log on from this credential, so it can't have a result.
HRESULT CMessageCredential::ReportResult(
	__in NTSTATUS ntsStatus,
	__in NTSTATUS ntsSubstatus,
	__deref_out_opt PWSTR* ppwszOptionalStatusText,
	__out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
)
{
	return E_NOTIMPL;
}
