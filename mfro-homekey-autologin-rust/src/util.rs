use std::{ffi::OsStr, os::windows::ffi::OsStrExt};
use windows::Win32::{
    Security::Credentials::CredProtectW, System::WindowsProgramming::GetComputerNameW,
};
use windows_core::{PWSTR, Result};

pub fn default<T: Default>() -> T {
    Default::default()
}

pub fn log_to_file(str: &str) {
    use std::{fs::File, io::Write};
    let mut file = File::options()
        .write(true)
        .append(true)
        .create(true)
        .open(r"E:\persistent\code\mfro-automation\mfro-homekey-autologin-rust\log.txt")
        .unwrap();

    file.write_all(str.as_bytes()).unwrap();
    file.write_all(b"\n").unwrap();
}

macro_rules! log {
    ($expression:literal $(, $arg:expr)*) => {
        crate::util::log_to_file(&format!($expression $(, $arg )*))
    };
}
pub(crate) use log;

pub fn wide_chars(str: &str) -> Vec<u16> {
    OsStr::new(str).encode_wide().collect()
}

pub fn protect_password(password: &[u16]) -> Result<Vec<u16>> {
    unsafe {
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

        Ok(protected_password)
    }
}

pub fn get_local_domain() -> Result<Vec<u16>> {
    unsafe {
        let mut domain_size = 0;
        let _ = GetComputerNameW(None, &mut domain_size);
        let mut domain = vec![0; domain_size as _];
        GetComputerNameW(Some(PWSTR(domain.as_mut_ptr())), &mut domain_size)?;

        Ok(domain)
    }
}
