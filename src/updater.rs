use anyhow::Result;
use reqwest::Client;
use serde::Deserialize;
use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::io::Write;
use futures::StreamExt;

const BRANCHES_URL: &str = "https://raw.githubusercontent.com/ORB-I/Crumblt_Launcher/main/Branches.txt";
const LAUNCHER_VERSION_URL: &str = "https://github.com/ORB-I/Crumblt_Launcher/releases/latest/download/version.txt";
const LAUNCHER_UPDATE_URL: &str = "https://github.com/ORB-I/Crumblt_Launcher/releases/latest/download/CrumbltLauncher.exe";
const CURRENT_LAUNCHER_VERSION: &str = env!("CARGO_PKG_VERSION");

#[derive(Debug, Deserialize)]
struct BranchesFile {
    branches: HashMap<String, String>,
}

pub struct Updater {
    client: Client,
    config: super::config::LauncherConfig,
}

impl Updater {
    pub fn new(config: super::config::LauncherConfig) -> Self {
        Self {
            client: Client::new(),
            config,
        }
    }

    pub async fn fetch_branches(&self) -> Result<HashMap<String, String>> {
        let response = self.client
            .get(BRANCHES_URL)
            .send()
            .await?
            .text()
            .await?;

        let mut branches = HashMap::new();
        for line in response.lines() {
            if let Some((key, value)) = line.split_once(": ") {
                branches.insert(key.trim().to_string(), value.trim().to_string());
            }
        }

        Ok(branches)
    }

    pub async fn check_for_updates(&self, branch_url: &str) -> Result<Option<String>> {
        let version_url = format!("{}/version.txt", branch_url.trim_end_matches('/'));

        let response = self.client
            .get(&version_url)
            .send()
            .await?;

        if !response.status().is_success() {
            return Ok(None);
        }

        let remote_version = response.text().await?.trim().to_string();

        let local_version_path = self.config.get_version_path();
        let local_version = if local_version_path.exists() {
            fs::read_to_string(local_version_path)?.trim().to_string()
        } else {
            "0.0.0".to_string()
        };

        if is_newer_version(&local_version, &remote_version) {
            Ok(Some(remote_version))
        } else {
            Ok(None)
        }
    }

    pub async fn download_and_install(
        &self,
        branch_url: &str,
        _version: &str,
        progress_callback: impl Fn(f32) + Send + Sync,
    ) -> Result<()> {
        let download_url = format!("{}/Crumblt.zip", branch_url.trim_end_matches('/'));

        let zip_path = self.config.install_dir.join("update.zip");

        // Create install directory if it doesn't exist
        fs::create_dir_all(&self.config.install_dir)?;

        // Download
        let response = self.client
            .get(&download_url)
            .send()
            .await?;

        let total_size = response.content_length().unwrap_or(0);
        let mut downloaded = 0;
        let mut file = fs::File::create(&zip_path)?;
        let mut stream = response.bytes_stream();

        while let Some(chunk) = stream.next().await {
            let chunk = chunk?;
            file.write_all(&chunk)?;
            downloaded += chunk.len() as u64;

            if total_size > 0 {
                progress_callback(downloaded as f32 / total_size as f32 * 0.8);
            }
        }

        progress_callback(0.85);

        // Extract
        extract_zip(&zip_path, &self.config.install_dir)?;

        progress_callback(0.95);

        // Clean up
        fs::remove_file(&zip_path)?;

        // Save version
        fs::write(self.config.get_version_path(), _version)?;

        progress_callback(1.0);

        Ok(())
    }

    pub fn set_branch(&mut self, branch: String) -> Result<()> {
        self.config.selected_branch = branch;
        self.config.save()?;
        Ok(())
    }

    pub fn get_config(&self) -> &super::config::LauncherConfig {
        &self.config
    }

    pub fn get_branches_sync(&self) -> Result<HashMap<String, String>> {
        let response = reqwest::blocking::Client::new()
            .get(BRANCHES_URL)
            .send()?
            .text()?;

        let mut branches = HashMap::new();
        for line in response.lines() {
            if let Some((key, value)) = line.split_once(": ") {
                branches.insert(key.trim().to_string(), value.trim().to_string());
            }
        }

        Ok(branches)
    }
    /// Check if launcher itself needs an update
       pub async fn check_launcher_update(&self) -> Result<Option<String>> {
           let response = self.client
               .get(LAUNCHER_VERSION_URL)
               .send()
               .await?;

           if !response.status().is_success() {
               return Ok(None);
           }

           let remote_version = response.text().await?.trim().to_string();

           if is_newer_version(CURRENT_LAUNCHER_VERSION, &remote_version) {
               Ok(Some(remote_version))
           } else {
               Ok(None)
           }
       }

       /// Download and apply launcher update (requires restart)
       pub async fn update_launcher(
           &self,
           progress_callback: impl Fn(f32) + Send + Sync,
       ) -> Result<()> {
           // Get current exe path
           let current_exe = std::env::current_exe()?;
           let parent_dir = current_exe.parent()
               .ok_or_else(|| anyhow::anyhow!("Failed to get parent directory"))?;

           let new_exe = parent_dir.join("CrumbltLauncher_new.exe");
           let bat_path = parent_dir.join("_crumblt_update.bat");

           // Download new launcher
           let response = self.client
               .get(LAUNCHER_UPDATE_URL)
               .send()
               .await?;

           let total_size = response.content_length().unwrap_or(0);
           let mut downloaded = 0;
           let mut file = fs::File::create(&new_exe)?;
           let mut stream = response.bytes_stream();

           while let Some(chunk) = stream.next().await {
               let chunk = chunk?;
               file.write_all(&chunk)?;
               downloaded += chunk.len() as u64;

               if total_size > 0 {
                   progress_callback(downloaded as f32 / total_size as f32);
               }
           }

           progress_callback(1.0);

           // Create batch script to replace launcher
           let bat_content = format!(
               "@echo off\r\n\
                timeout /t 2 /nobreak >nul\r\n\
                copy /Y \"{}\" \"{}\"\r\n\
                del \"{}\"\r\n\
                start \"\" \"{}\"\r\n\
                del \"%~f0\"\r\n",
               new_exe.display(),
               current_exe.display(),
               new_exe.display(),
               current_exe.display()
           );

           fs::write(&bat_path, bat_content)?;

           // Launch batch script
           std::process::Command::new("cmd.exe")
               .arg("/C")
               .arg(&bat_path)
               .spawn()?;

           Ok(())
       }

       /// Full check: launcher first, then client
       pub async fn check_all_updates(
           &self,
           branch_url: &str,
       ) -> Result<(Option<String>, Option<String>)> {
           // Check launcher update first
           let launcher_update = self.check_launcher_update().await?;

           // If launcher needs update, don't bother checking client yet
           if launcher_update.is_some() {
               return Ok((launcher_update, None));
           }

           // Check client update
           let client_update = self.check_for_updates(branch_url).await?;
           Ok((None, client_update))
       }
   }

fn is_newer_version(local: &str, remote: &str) -> bool {
    let local_parts: Vec<u32> = local
        .split('.')
        .filter_map(|s| s.parse().ok())
        .collect();
    let remote_parts: Vec<u32> = remote
        .split('.')
        .filter_map(|s| s.parse().ok())
        .collect();

    for i in 0..3 {
        let l = local_parts.get(i).copied().unwrap_or(0);
        let r = remote_parts.get(i).copied().unwrap_or(0);

        if r > l {
            return true;
        } else if l > r {
            return false;
        }
    }

    false
}

fn extract_zip(zip_path: &Path, dest: &Path) -> Result<()> {
    let file = fs::File::open(zip_path)?;
    let mut archive = zip::ZipArchive::new(file)?;

    for i in 0..archive.len() {
        let mut file = archive.by_index(i)?;
        let outpath = dest.join(file.name());

        if file.is_dir() {
            fs::create_dir_all(&outpath)?;
        } else {
            if let Some(parent) = outpath.parent() {
                fs::create_dir_all(parent)?;
            }
            let mut outfile = fs::File::create(&outpath)?;
            std::io::copy(&mut file, &mut outfile)?;
        }
    }

    Ok(())
}
