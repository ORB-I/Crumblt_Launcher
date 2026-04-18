mod config;
mod updater;
mod registry;
mod ui;

use anyhow::Result;
use config::LauncherConfig;
use std::env;

fn main() -> Result<()> {
    env_logger::init();

    // Check if launched via protocol
    let args: Vec<String> = env::args().collect();
    let protocol_url = args.get(1).cloned();

    if let Some(url) = protocol_url {
        // Protocol launch - skip UI, launch client directly
        println!("Protocol launch detected: {}", url);
        launch_client_with_url(&url)?;
    } else {
        // Normal launch - show UI
        println!("Normal launch - showing UI");

        // Create a new tokio runtime for the UI
        let runtime = tokio::runtime::Builder::new_multi_thread()
            .enable_all()
            .build()?;

        runtime.block_on(async {
            if let Err(e) = ui::run_launcher_ui().await {
                eprintln!("UI error: {}", e);
            }
        });
    }

    Ok(())
}

fn launch_client_with_url(url: &str) -> Result<()> {
    let config = LauncherConfig::load()?;
    let client_path = config.get_client_path()?;

    if !client_path.exists() {
        eprintln!("Client not found at: {:?}", client_path);
        anyhow::bail!("Client not installed. Please run the launcher first.");
    }

    println!("Launching client: {:?} with arg: {}", client_path, url);

    // Launch client with the URL as argument
    let mut child = std::process::Command::new(&client_path)
        .arg(url)
        .spawn()?;

    // Don't wait for the client to exit
    child.stdin = None;

    println!("Client launched successfully!");

    Ok(())
}
