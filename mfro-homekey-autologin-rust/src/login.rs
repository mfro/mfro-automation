use std::sync::{Arc, Mutex, mpsc::Sender};

use bytemuck::cast_slice;
use windows::{
    Win32::{
        Foundation::*,
        Graphics::Gdi::HBITMAP,
        Security::Authentication::Identity::{
            KERB_INTERACTIVE_UNLOCK_LOGON, KerbInteractiveLogon, KerbWorkstationUnlockLogon,
            LSA_STRING, LsaConnectUntrusted, LsaLookupAuthenticationPackage,
        },
        UI::Shell::*,
    },
    core::implement,
};
use windows_core::{BOOL, PCWSTR, PSTR, PWSTR, Ref, Result};

use crate::util::*;

pub struct UnlockCredentials {
    pub username: String,
    pub password: String,
}

struct RawUnlockCredentials {
    domain: Vec<u16>,
    username: Vec<u16>,
    protected_password: Vec<u16>,
}

// I can't find direct confirmation that this is thread safe,
// but I can't imagine ICredentialProviderEvents::CredentialsChanged isn't
unsafe impl Send for Main {}
pub struct Main {
    scenario: CREDENTIAL_PROVIDER_USAGE_SCENARIO,
    advisee: Option<(ICredentialProviderEvents, usize)>,
    unlock: Option<RawUnlockCredentials>,
}

impl Main {
    fn new() -> Self {
        Self {
            scenario: CPUS_INVALID,
            advisee: None,
            unlock: None,
        }
    }

    pub fn unlock(&mut self, args: UnlockCredentials) {
        if let Some((advisee, context)) = self.advisee.as_ref() {
            let domain = get_local_domain().unwrap();
            let username = wide_chars(&args.username);

            let mut password = wide_chars(&args.password);
            password.push(0x00); // need null terminator
            let protected_password = protect_password(&password).unwrap();

            self.unlock = Some(RawUnlockCredentials {
                domain,
                username,
                protected_password,
            });

            unsafe { advisee.CredentialsChanged(*context).unwrap() };
        }
    }
}

#[implement(ICredentialProvider)]
pub struct MyProvider {
    main: Arc<Mutex<Main>>,
    credential: ICredentialProviderCredential,
    stop: Sender<()>,
}

impl MyProvider {
    pub fn new() -> Self {
        crate::global_ref_add();

        let main = Arc::new(Mutex::new(Main::new()));

        let stop = crate::connect::run(main.clone());

        let credential = MyCredential::new(main.clone());
        let credential = credential.into();

        MyProvider {
            main,
            credential,
            stop,
        }
    }
}

impl Drop for MyProvider {
    fn drop(&mut self) {
        let _ = self.stop.send(());

        crate::global_ref_release();
    }
}

impl ICredentialProvider_Impl for MyProvider_Impl {
    fn SetUsageScenario(
        &self,
        scenario: CREDENTIAL_PROVIDER_USAGE_SCENARIO,
        flags: u32,
    ) -> Result<()> {
        log!(
            "ICredentialProvider.SetUsageScenario {:?} {:?}",
            scenario,
            flags
        );

        match scenario {
            CPUS_LOGON | CPUS_UNLOCK_WORKSTATION => {
                self.this.main.lock().unwrap().scenario = scenario;
                Ok(())
            }

            _ => Err(E_NOTIMPL.into()),
        }
    }

    fn SetSerialization(
        &self,
        _pcpcs: *const CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION,
    ) -> Result<()> {
        log!("ICredentialProvider.SetSerialization");
        Ok(())
    }

    fn Advise(
        &self,
        advisee: Ref<ICredentialProviderEvents>,
        advisee_context: usize,
    ) -> Result<()> {
        log!("ICredentialProvider.Advise");

        let advisee = advisee.unwrap().clone();

        let mut main = self.this.main.lock().unwrap();
        main.advisee = Some((advisee, advisee_context));

        Ok(())
    }

    fn UnAdvise(&self) -> Result<()> {
        log!("ICredentialProvider.UnAdvise");

        let mut main = self.this.main.lock().unwrap();
        main.advisee = None;

        Ok(())
    }

    fn GetFieldDescriptorCount(&self) -> Result<u32> {
        log!("ICredentialProvider.GetFieldDescriptorCount");
        Ok(0)
    }

    fn GetFieldDescriptorAt(&self, _: u32) -> Result<*mut CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR> {
        log!("ICredentialProvider.GetFieldDescriptorAt");
        Err(E_NOTIMPL.into())
    }

    fn GetCredentialCount(
        &self,
        count: *mut u32,
        default: *mut u32,
        default_auto_logon: *mut BOOL,
    ) -> Result<()> {
        log!("ICredentialProvider.GetCredentialCount");

        unsafe {
            let main = self.this.main.lock().unwrap();

            if main.unlock.is_some() {
                *count = 1;
                *default_auto_logon = TRUE;
            } else {
                *count = 0;
                *default_auto_logon = FALSE;
            }

            *default = 0;
        }

        Ok(())
    }

    fn GetCredentialAt(&self, _index: u32) -> Result<ICredentialProviderCredential> {
        log!("ICredentialProvider.GetCredentialAt");

        return Ok(self.this.credential.clone());
    }
}

#[implement(ICredentialProviderCredential)]
#[derive(Clone)]
struct MyCredential {
    main: Arc<Mutex<Main>>,
}

impl MyCredential {
    fn new(main: Arc<Mutex<Main>>) -> Self {
        MyCredential { main }
    }
}

impl ICredentialProviderCredential_Impl for MyCredential_Impl {
    fn Advise(&self, _pcpce: Ref<ICredentialProviderCredentialEvents>) -> Result<()> {
        log!("ICrdentialProviderCredential.Advise");
        Ok(())
    }

    fn UnAdvise(&self) -> Result<()> {
        log!("ICrdentialProviderCredential.UnAdvise");
        Ok(())
    }

    fn SetSelected(&self) -> Result<BOOL> {
        log!("ICrdentialProviderCredential.SetSelected");
        let main = self.this.main.lock().unwrap();

        Ok(main.unlock.is_some().into())
    }

    fn SetDeselected(&self) -> Result<()> {
        log!("ICrdentialProviderCredential.SetDeselected");
        Ok(())
    }

    fn GetFieldState(
        &self,
        _dwfieldid: u32,
        _pcpfs: *mut CREDENTIAL_PROVIDER_FIELD_STATE,
        _pcpfis: *mut CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE,
    ) -> Result<()> {
        log!("ICrdentialProviderCredential.GetFieldState");
        Err(E_NOTIMPL.into())
    }

    fn GetStringValue(&self, _dwfieldid: u32) -> Result<PWSTR> {
        log!("ICrdentialProviderCredential.GetStringValue");
        Err(E_NOTIMPL.into())
    }

    fn GetBitmapValue(&self, _dwfieldid: u32) -> Result<HBITMAP> {
        log!("ICrdentialProviderCredential.GetBitmapValue");
        Err(E_NOTIMPL.into())
    }

    fn GetCheckboxValue(
        &self,
        _dwfieldid: u32,
        _pbchecked: *mut BOOL,
        _ppszlabel: *mut PWSTR,
    ) -> Result<()> {
        log!("ICrdentialProviderCredential.GetCheckboxValue");
        Err(E_NOTIMPL.into())
    }

    fn GetSubmitButtonValue(&self, _dwfieldid: u32) -> Result<u32> {
        log!("ICrdentialProviderCredential.GetSubmitButtonValue");
        Err(E_NOTIMPL.into())
    }

    fn GetComboBoxValueCount(
        &self,
        _dwfieldid: u32,
        _pcitems: *mut u32,
        _pdwselecteditem: *mut u32,
    ) -> Result<()> {
        log!("ICrdentialProviderCredential.GetComboBoxValueCount");
        Err(E_NOTIMPL.into())
    }

    fn GetComboBoxValueAt(&self, _dwfieldid: u32, _dwitem: u32) -> Result<PWSTR> {
        log!("ICrdentialProviderCredential.GetComboBoxValueAt");
        Err(E_NOTIMPL.into())
    }

    fn SetStringValue(&self, _dwfieldid: u32, _psz: &PCWSTR) -> Result<()> {
        log!("ICrdentialProviderCredential.SetStringValue");
        Err(E_NOTIMPL.into())
    }

    fn SetCheckboxValue(&self, _dwfieldid: u32, _bchecked: BOOL) -> Result<()> {
        log!("ICrdentialProviderCredential.SetCheckboxValue");
        Err(E_NOTIMPL.into())
    }

    fn SetComboBoxSelectedValue(&self, _dwfieldid: u32, _dwselecteditem: u32) -> Result<()> {
        log!("ICrdentialProviderCredential.SetComboBoxSelectedValue");
        Err(E_NOTIMPL.into())
    }

    fn CommandLinkClicked(&self, _dwfieldid: u32) -> Result<()> {
        log!("ICrdentialProviderCredential.CommandLinkClicked");
        Err(E_NOTIMPL.into())
    }

    fn GetSerialization(
        &self,
        response: *mut CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE,
        result: *mut CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION,
        _ppszoptionalstatustext: *mut PWSTR,
        _pcpsioptionalstatusicon: *mut CREDENTIAL_PROVIDER_STATUS_ICON,
    ) -> Result<()> {
        log!("ICrdentialProviderCredential.GetSerialization");

        let mut main = self.this.main.lock().unwrap();

        if let Some(info) = main.unlock.take() {
            match login(main.scenario, info, result) {
                Ok(()) => unsafe { *response = CPGSR_RETURN_CREDENTIAL_FINISHED },
                Err(e) => log!("login error: {:?}", e),
            }
        }

        Ok(())
    }

    fn ReportResult(
        &self,
        _ntsstatus: NTSTATUS,
        _ntssubstatus: NTSTATUS,
        _ppszoptionalstatustext: *mut PWSTR,
        _pcpsioptionalstatusicon: *mut CREDENTIAL_PROVIDER_STATUS_ICON,
    ) -> Result<()> {
        Ok(())
    }
}

fn login(
    scenario: CREDENTIAL_PROVIDER_USAGE_SCENARIO,
    credentials: RawUnlockCredentials,
    result: *mut CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION,
) -> Result<()> {
    unsafe {
        let domain = cast_slice(&credentials.domain);
        let user = cast_slice(&credentials.username);
        let password = cast_slice(&credentials.protected_password);

        let offset_domain = std::mem::size_of::<KERB_INTERACTIVE_UNLOCK_LOGON>();
        let offset_user = offset_domain + domain.len();
        let offset_password = offset_user + user.len();
        let total_size = offset_password + password.len();

        let buffer = vec![0u8; total_size].leak();

        let logon = &mut buffer[0] as *mut u8 as *mut KERB_INTERACTIVE_UNLOCK_LOGON;
        (*logon).Logon.MessageType = match scenario {
            CPUS_UNLOCK_WORKSTATION => KerbWorkstationUnlockLogon,
            CPUS_LOGON => KerbInteractiveLogon,
            _ => return Err(E_FAIL.into()),
        };

        buffer[offset_domain..offset_user].copy_from_slice(&domain);
        (*logon).Logon.LogonDomainName.Buffer = PWSTR(offset_domain as _);
        (*logon).Logon.LogonDomainName.Length = domain.len() as _;
        (*logon).Logon.LogonDomainName.MaximumLength = domain.len() as _;

        buffer[offset_user..offset_password].copy_from_slice(&user);
        (*logon).Logon.UserName.Buffer = PWSTR(offset_user as _);
        (*logon).Logon.UserName.Length = user.len() as _;
        (*logon).Logon.UserName.MaximumLength = user.len() as _;

        buffer[offset_password..total_size].copy_from_slice(&password);
        (*logon).Logon.Password.Buffer = PWSTR(offset_password as _);
        (*logon).Logon.Password.Length = password.len() as _;
        (*logon).Logon.Password.MaximumLength = password.len() as _;

        let mut lsa = default();
        LsaConnectUntrusted(&mut lsa).ok()?;

        let mut kerberos_name = "Negotiate".to_owned();
        let kerberos_name = LSA_STRING {
            Buffer: PSTR(kerberos_name.as_mut_ptr()),
            Length: kerberos_name.len() as _,
            MaximumLength: kerberos_name.len() as _,
        };

        let result = &mut *result;

        let auth_package = &mut result.ulAuthenticationPackage;
        LsaLookupAuthenticationPackage(lsa, &kerberos_name, auth_package).ok()?;

        result.rgbSerialization = buffer.as_mut_ptr();
        result.cbSerialization = total_size as _;
        result.clsidCredentialProvider = crate::MY_CLASS_ID;
    }

    Ok(())
}
