use std::{ffi::OsStr, os::windows::ffi::OsStrExt};

pub fn default<T: Default>() -> T {
    Default::default()
}

pub fn wide_chars(str: &str) -> Vec<u16> {
    OsStr::new(str).encode_wide().collect()
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
