cargo build

if ($?) {
  cp target\debug\mfro_homekey_autologin_rust.dll C:\Windows\System32\HomeKeyCredentialProvider.dll
  rundll32 user32.dll,LockWorkStation
}
