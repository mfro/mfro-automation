use std::sync::{Arc, Mutex, mpsc::Sender};

use bytemuck::cast_slice;
use windows::{
    Win32::{
        Foundation::*,
        Graphics::Gdi::HBITMAP,
        Security::{
            Authentication::Identity::{
                KERB_INTERACTIVE_UNLOCK_LOGON, KerbInteractiveLogon, KerbWorkstationUnlockLogon,
                LSA_STRING, LsaConnectUntrusted, LsaLookupAuthenticationPackage,
            },
            Credentials::CredProtectW,
        },
        UI::Shell::*,
    },
    core::implement,
};
use windows_core::{BOOL, PCWSTR, PSTR, PWSTR, Ref, Result};

use crate::util::*;

struct UnlockData {
    password: String,
}

unsafe impl Send for Main {}

pub struct Main {
    scenario: CREDENTIAL_PROVIDER_USAGE_SCENARIO,
    advisee: Option<(ICredentialProviderEvents, usize)>,
    unlock: Option<UnlockData>,
}

impl Main {
    fn new() -> Self {
        Self {
            scenario: CPUS_INVALID,
            advisee: None,
            unlock: None,
        }
    }

    pub fn unlock(&mut self, password: String) {
        if let Some((advisee, context)) = self.advisee.as_ref() {
            self.unlock = Some(UnlockData { password });
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

        let credential = MyCredential::new(main.clone());
        let credential = credential.into();

        let stop = crate::connect::run(main.clone());

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
        log!("SetUsageScenario {:?} {:?}", scenario, flags);

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
        log!("SetSerialization");
        Ok(())
    }

    fn Advise(
        &self,
        advisee: Ref<ICredentialProviderEvents>,
        advisee_context: usize,
    ) -> Result<()> {
        log!("Advise");

        let advisee = advisee.unwrap().clone();

        let mut main = self.this.main.lock().unwrap();
        main.advisee = Some((advisee, advisee_context));

        Ok(())
    }

    fn UnAdvise(&self) -> Result<()> {
        log!("UnAdvise");

        let mut main = self.this.main.lock().unwrap();
        main.advisee = None;

        Ok(())
    }

    fn GetFieldDescriptorCount(&self) -> Result<u32> {
        log!("GetFieldDescriptorCount");
        Ok(0)
    }

    fn GetFieldDescriptorAt(&self, _: u32) -> Result<*mut CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR> {
        log!("GetFieldDescriptorAt");
        Err(E_NOTIMPL.into())
    }

    fn GetCredentialCount(
        &self,
        count: *mut u32,
        default: *mut u32,
        default_auto_logon: *mut BOOL,
    ) -> Result<()> {
        log!("GetCredentialCount");

        unsafe {
            *count = 1;
            *default = 0;
            *default_auto_logon = FALSE;
        }

        Ok(())
    }

    fn GetCredentialAt(&self, _index: u32) -> Result<ICredentialProviderCredential> {
        log!("GetCredentialAt");

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
        log!("Advise");
        Ok(())
    }

    fn UnAdvise(&self) -> Result<()> {
        log!("UnAdvise");
        Ok(())
    }

    fn SetSelected(&self) -> Result<BOOL> {
        log!("SetSelected");
        let main = self.this.main.lock().unwrap();

        Ok(main.unlock.is_some().into())
    }

    fn SetDeselected(&self) -> Result<()> {
        log!("SetDeselected");
        Ok(())
    }

    fn GetFieldState(
        &self,
        _dwfieldid: u32,
        _pcpfs: *mut CREDENTIAL_PROVIDER_FIELD_STATE,
        _pcpfis: *mut CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE,
    ) -> Result<()> {
        Err(E_NOTIMPL.into())
    }

    fn GetStringValue(&self, _dwfieldid: u32) -> Result<PWSTR> {
        Err(E_NOTIMPL.into())
    }

    fn GetBitmapValue(&self, _dwfieldid: u32) -> Result<HBITMAP> {
        Err(E_NOTIMPL.into())
    }

    fn GetCheckboxValue(
        &self,
        _dwfieldid: u32,
        _pbchecked: *mut BOOL,
        _ppszlabel: *mut PWSTR,
    ) -> Result<()> {
        Err(E_NOTIMPL.into())
    }

    fn GetSubmitButtonValue(&self, _dwfieldid: u32) -> Result<u32> {
        Err(E_NOTIMPL.into())
    }

    fn GetComboBoxValueCount(
        &self,
        _dwfieldid: u32,
        _pcitems: *mut u32,
        _pdwselecteditem: *mut u32,
    ) -> Result<()> {
        Err(E_NOTIMPL.into())
    }

    fn GetComboBoxValueAt(&self, _dwfieldid: u32, _dwitem: u32) -> Result<PWSTR> {
        Err(E_NOTIMPL.into())
    }

    fn SetStringValue(&self, _dwfieldid: u32, _psz: &PCWSTR) -> Result<()> {
        Err(E_NOTIMPL.into())
    }

    fn SetCheckboxValue(&self, _dwfieldid: u32, _bchecked: BOOL) -> Result<()> {
        Err(E_NOTIMPL.into())
    }

    fn SetComboBoxSelectedValue(&self, _dwfieldid: u32, _dwselecteditem: u32) -> Result<()> {
        Err(E_NOTIMPL.into())
    }

    fn CommandLinkClicked(&self, _dwfieldid: u32) -> Result<()> {
        Err(E_NOTIMPL.into())
    }

    fn GetSerialization(
        &self,
        response: *mut CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE,
        result: *mut CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION,
        _ppszoptionalstatustext: *mut PWSTR,
        _pcpsioptionalstatusicon: *mut CREDENTIAL_PROVIDER_STATUS_ICON,
    ) -> Result<()> {
        log!("GetSerialization");

        let mut main = self.this.main.lock().unwrap();

        if let Some(info) = main.unlock.take() {
            if let Err(e) = do_login(info, main.scenario, response, result) {
                log!("error: {:?}", e);
            }
        }
        log!("GetSerialization done");

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

fn do_login(
    info: UnlockData,
    scenario: CREDENTIAL_PROVIDER_USAGE_SCENARIO,
    response: *mut CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE,
    result: *mut CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION,
) -> Result<()> {
    unsafe {
        let mut password = wide_chars(&info.password);
        password.push(0x00);

        let mut protected_size = 0;
        let _ = CredProtectW(false, &password, default(), &mut protected_size, default());
        let mut protected_password = vec![0; protected_size as _];
        CredProtectW(
            false,
            &password,
            PWSTR(protected_password.as_mut_ptr()),
            &mut protected_size,
            default(),
        )?;

        let domain = wide_chars("mfro-desktop");
        let user = wide_chars("Max");

        let bytes_domain = cast_slice(&domain);
        let bytes_user = cast_slice(&user);
        let bytes_password = cast_slice(&protected_password);

        let offset_domain = std::mem::size_of::<KERB_INTERACTIVE_UNLOCK_LOGON>();
        let offset_user = offset_domain + bytes_domain.len();
        let offset_password = offset_user + bytes_user.len();
        let total_size = offset_password + bytes_password.len();

        let buffer = vec![0u8; total_size].leak();

        let logon = &mut buffer[0] as *mut u8 as *mut KERB_INTERACTIVE_UNLOCK_LOGON;
        (*logon).Logon.MessageType = match scenario {
            CPUS_UNLOCK_WORKSTATION => KerbWorkstationUnlockLogon,
            CPUS_LOGON => KerbInteractiveLogon,
            _ => return Err(E_FAIL.into()),
        };

        buffer[offset_domain..offset_user].copy_from_slice(&bytes_domain);
        (*logon).Logon.LogonDomainName.Buffer = PWSTR(offset_domain as _);
        (*logon).Logon.LogonDomainName.Length = bytes_domain.len() as _;
        (*logon).Logon.LogonDomainName.MaximumLength = bytes_domain.len() as _;

        buffer[offset_user..offset_password].copy_from_slice(&bytes_user);
        (*logon).Logon.UserName.Buffer = PWSTR(offset_user as _);
        (*logon).Logon.UserName.Length = bytes_user.len() as _;
        (*logon).Logon.UserName.MaximumLength = bytes_user.len() as _;

        buffer[offset_password..total_size].copy_from_slice(&bytes_password);
        (*logon).Logon.Password.Buffer = PWSTR(offset_password as _);
        (*logon).Logon.Password.Length = bytes_password.len() as _;
        (*logon).Logon.Password.MaximumLength = bytes_password.len() as _;

        let mut lsa = default();
        LsaConnectUntrusted(&mut lsa).ok()?;

        let mut kerberos_name = "Negotiate".to_owned();
        let kerberos_name = LSA_STRING {
            Buffer: PSTR(kerberos_name.as_mut_ptr()),
            Length: kerberos_name.len() as _,
            MaximumLength: kerberos_name.len() as _,
        };

        let auth_package = &mut (*result).ulAuthenticationPackage;
        LsaLookupAuthenticationPackage(lsa, &kerberos_name, auth_package).ok()?;

        (*result).rgbSerialization = buffer.as_mut_ptr();
        (*result).cbSerialization = total_size as _;
        (*result).clsidCredentialProvider = crate::MY_CLASS_ID;

        *response = CPGSR_RETURN_CREDENTIAL_FINISHED;
    }

    Ok(())
}
