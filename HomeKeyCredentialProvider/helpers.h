#pragma once
#define WIN32_LEAN_AND_MEAN

#include "common.h"

//makes a copy of a field descriptor using CoTaskMemAlloc
HRESULT FieldDescriptorCoAllocCopy(
    __in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR& rcpfd,
    __deref_out CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR** ppcpfd
    );

//initializes a KERB_INTERACTIVE_UNLOCK_LOGON with weak references to the provided credentials
HRESULT KerbInteractiveUnlockLogonInit(
    __in PWSTR pwzDomain,
    __in PWSTR pwzUsername,
    __in PWSTR pwzPassword,
    __in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    __out KERB_INTERACTIVE_UNLOCK_LOGON* pkiul
    );

//packages the credentials into the buffer that the system expects
HRESULT KerbInteractiveUnlockLogonPack(
    __in const KERB_INTERACTIVE_UNLOCK_LOGON& rkiulIn,
    __deref_out_bcount(*pcb) BYTE** prgb,
    __out DWORD* pcb
    );

//get the authentication package that will be used for our logon attempt
HRESULT RetrieveNegotiateAuthPackage(
    __out ULONG * pulAuthPackage
    );

//encrypt a password (if necessary) and copy it; if not, just copy it
HRESULT ProtectIfNecessaryAndCopyPassword(
    __in PCWSTR pwzPassword,
    __in CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    __deref_out PWSTR* ppwzProtectedPassword
    );
