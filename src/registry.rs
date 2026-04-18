use anyhow::Result;
use std::env;
use winreg::enums::*;
use winreg::RegKey;

pub fn register_protocol() -> Result<()> {
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    let path = "Software\\Classes\\crumblt";

    let (key, _) = hkcu.create_subkey(path)?;
    key.set_value("", &"Crumblt Game Launcher")?;
    key.set_value("URL Protocol", &"")?;

    // DefaultIcon
    let (icon_key, _) = hkcu.create_subkey(&format!("{}\\DefaultIcon", path))?;
    let exe_path = env::current_exe()?;
    icon_key.set_value("", &exe_path.to_string_lossy().to_string())?;

    // Shell command
    let (cmd_key, _) = hkcu.create_subkey(&format!("{}\\shell\\open\\command", path))?;
    let command = format!("\"{}\" \"%1\"", exe_path.to_string_lossy());
    cmd_key.set_value("", &command)?;

    Ok(())
}

pub fn is_protocol_registered() -> bool {
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    hkcu.open_subkey("Software\\Classes\\crumblt").is_ok()
}
