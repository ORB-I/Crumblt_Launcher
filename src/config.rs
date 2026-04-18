use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LauncherConfig {
    pub selected_branch: String,
    pub install_dir: PathBuf,
    pub client_version: String,
    pub launcher_version: String,
}

impl Default for LauncherConfig {
    fn default() -> Self {
        let install_dir = dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("Crumblt");

        Self {
            selected_branch: "Client_Production".to_string(),
            install_dir,
            client_version: "0.0.0".to_string(),
            launcher_version: env!("CARGO_PKG_VERSION").to_string(),
        }
    }
}

impl LauncherConfig {
    pub fn load() -> Result<Self> {
        let config_path = Self::config_path();

        if config_path.exists() {
            let content = fs::read_to_string(config_path)?;
            Ok(serde_json::from_str(&content)?)
        } else {
            let config = Self::default();
            config.save()?;
            Ok(config)
        }
    }

    pub fn save(&self) -> Result<()> {
        let config_path = Self::config_path();
        fs::create_dir_all(config_path.parent().unwrap())?;
        let content = serde_json::to_string_pretty(self)?;
        fs::write(config_path, content)?;
        Ok(())
    }

    fn config_path() -> PathBuf {
        dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("Crumblt")
            .join("launcher_config.json")
    }

    pub fn get_client_path(&self) -> Result<PathBuf> {
        Ok(self.install_dir.join("CrumbltClient.exe"))
    }

    pub fn get_version_path(&self) -> PathBuf {
        self.install_dir.join("version.txt")
    }
}
