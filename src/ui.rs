use anyhow::Result;
use eframe::{egui, Frame};
use egui::{Align2, Color32, RichText};
use std::sync::mpsc;

use crate::config::LauncherConfig;
use crate::updater::Updater;
use crate::registry;

#[derive(PartialEq)]
enum LauncherState {
    Idle,
    CheckingBranches,
    CheckingUpdates,
    Downloading,
    Launching,
    UpdatingLauncher,
    Error(String),
}

pub struct LauncherApp {
    state: LauncherState,
    config: LauncherConfig,
    branches: Vec<(String, String)>,
    progress: f32,
    status_text: String,
    show_branch_selector: bool,
    client_ready: bool,
    branch_receiver: Option<mpsc::Receiver<Vec<(String, String)>>>,
    update_receiver: Option<mpsc::Receiver<UpdateProgress>>,
}

enum UpdateProgress {
    Progress(f32),
    Complete,
    Error(String),
}

impl LauncherApp {
    fn new() -> Self {
        let config = LauncherConfig::load().unwrap_or_default();
        let client_ready = config.get_client_path().map(|p| p.exists()).unwrap_or(false);

        let mut app = Self {
            state: LauncherState::Idle,
            config,
            branches: Vec::new(),
            progress: 0.0,
            status_text: String::new(),
            show_branch_selector: false,
            client_ready,
            branch_receiver: None,
            update_receiver: None,
        };

        // Set initial status
        if client_ready {
            app.status_text = "Ready to launch".to_string();
        } else {
            app.status_text = "Client not installed".to_string();
        }

        app
    }

    fn fetch_branches(&mut self) {
        if self.branch_receiver.is_some() {
            return; // Already fetching
        }

        self.state = LauncherState::CheckingBranches;
        let (tx, rx) = mpsc::channel();
        self.branch_receiver = Some(rx);

        std::thread::spawn(move || {
            let client = reqwest::blocking::Client::new();
            let branches_url = "https://raw.githubusercontent.com/ORB-I/Crumblt_Launcher/main/Branches.txt";

            match client.get(branches_url).send() {
                Ok(response) => {
                    if let Ok(text) = response.text() {
                        let mut branches = Vec::new();
                        for line in text.lines() {
                            if let Some((key, value)) = line.split_once(": ") {
                                branches.push((key.trim().to_string(), value.trim().to_string()));
                            }
                        }
                        let _ = tx.send(branches);
                    } else {
                        let _ = tx.send(Vec::new());
                    }
                }
                Err(_) => {
                    let _ = tx.send(Vec::new());
                }
            }
        });
    }

    fn check_receiver(&mut self) {
        if let Some(rx) = &self.branch_receiver {
            if let Ok(branches) = rx.try_recv() {
                self.branches = branches;
                self.branch_receiver = None;

                if self.branches.is_empty() {
                    self.state = LauncherState::Error("No branches found".to_string());
                } else {
                    self.state = LauncherState::Idle;
                }
            }
        }
    }

    fn check_update_receiver(&mut self) {
        if let Some(rx) = &self.update_receiver {
            match rx.try_recv() {
                Ok(UpdateProgress::Progress(p)) => {
                    self.progress = p;
                }
                Ok(UpdateProgress::Complete) => {
                    self.update_receiver = None;
                    self.state = LauncherState::Idle;
                    self.status_text = "Update complete! Restarting...".to_string();
                    // Launcher will restart via batch script
                }
                Ok(UpdateProgress::Error(e)) => {
                    self.update_receiver = None;
                    self.state = LauncherState::Error(e);
                }
                Err(mpsc::TryRecvError::Empty) => {}
                Err(mpsc::TryRecvError::Disconnected) => {
                    self.update_receiver = None;
                }
            }
        }
    }

    fn start_launcher_update(&mut self) {
        if self.update_receiver.is_some() {
            return;
        }

        self.state = LauncherState::UpdatingLauncher;
        self.status_text = "Downloading launcher update...".to_string();

        let config = self.config.clone();
        let (tx, rx) = mpsc::channel();
        self.update_receiver = Some(rx);

        std::thread::spawn(move || {
            let runtime = tokio::runtime::Runtime::new().unwrap();

            runtime.block_on(async {
                let updater = Updater::new(config);

                let result = updater.update_launcher(|p| {
                    let _ = tx.send(UpdateProgress::Progress(p));
                }).await;

                match result {
                    Ok(()) => {
                        let _ = tx.send(UpdateProgress::Complete);
                        // Give time for the message to be processed
                        std::thread::sleep(std::time::Duration::from_millis(500));
                        std::process::exit(0);
                    }
                    Err(e) => {
                        let _ = tx.send(UpdateProgress::Error(format!("Update failed: {}", e)));
                    }
                }
            });
        });
    }
}

impl eframe::App for LauncherApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut Frame) {
        // Check for async operation results
        self.check_receiver();
        self.check_update_receiver();

        // Register protocol on first run
        if !registry::is_protocol_registered() {
            let _ = registry::register_protocol();
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            // Title bar
            ui.horizontal(|ui| {
                ui.add_space(10.0);
                ui.label(RichText::new("Crumblt Launcher").size(20.0).strong());
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if ui.button("×").clicked() {
                        std::process::exit(0);
                    }
                });
            });

            ui.separator();
            ui.add_space(20.0);

            // Logo area
            ui.vertical_centered(|ui| {
                ui.heading("🎮 Crumblt");
                ui.label("Game Platform");
            });

            ui.add_space(30.0);

            // Status
            ui.horizontal(|ui| {
                ui.label("Status: ");
                ui.label(match &self.state {
                    LauncherState::Idle => RichText::new(&self.status_text).color(Color32::GRAY),
                    LauncherState::CheckingBranches => RichText::new("Fetching branches...").color(Color32::YELLOW),
                    LauncherState::CheckingUpdates => RichText::new("Checking for updates...").color(Color32::YELLOW),
                    LauncherState::Downloading => RichText::new("Downloading client...").color(Color32::LIGHT_BLUE),
                    LauncherState::UpdatingLauncher => RichText::new("Updating launcher...").color(Color32::LIGHT_BLUE),
                    LauncherState::Launching => RichText::new("Launching...").color(Color32::GREEN),
                    LauncherState::Error(msg) => RichText::new(msg).color(Color32::RED),
                });
            });

            // Progress bar
            if self.progress > 0.0 && self.progress < 1.0 {
                ui.add_space(10.0);
                ui.add(egui::ProgressBar::new(self.progress).desired_width(ui.available_width() - 40.0));
                ui.label(format!("{:.0}%", self.progress * 100.0));
            }

            ui.add_space(30.0);

            // Buttons
            ui.vertical_centered(|ui| {
                let button_width = 200.0;

                // Launch button
                ui.add_enabled_ui(self.client_ready && self.state != LauncherState::UpdatingLauncher, |ui| {
                    if ui.add_sized([button_width, 40.0], egui::Button::new("Launch Crumblt")).clicked() {
                        self.state = LauncherState::Launching;

                        if let Ok(client_path) = self.config.get_client_path() {
                            match std::process::Command::new(&client_path).spawn() {
                                Ok(_) => {
                                    std::process::exit(0);
                                }
                                Err(e) => {
                                    self.state = LauncherState::Error(format!("Failed to launch: {}", e));
                                }
                            }
                        }
                    }
                });

                if !self.client_ready {
                    ui.label(RichText::new("Client not installed").size(12.0).color(Color32::GRAY));
                    ui.label(RichText::new("Use Select Branch to install").size(11.0).color(Color32::GRAY));
                }

                ui.add_space(10.0);

                // Branch selector button
                ui.add_enabled_ui(self.state != LauncherState::UpdatingLauncher, |ui| {
                    if ui.add_sized([button_width, 40.0], egui::Button::new("Select Branch")).clicked() {
                        self.show_branch_selector = true;
                        self.fetch_branches();
                    }
                });
            });

            // Branch selector modal
            if self.show_branch_selector {
                egui::Window::new("Select Branch")
                    .anchor(Align2::CENTER_CENTER, [0.0, 0.0])
                    .collapsible(false)
                    .resizable(false)
                    .show(ctx, |ui| {
                        ui.label("Choose a branch to install/update:");
                        ui.separator();

                        if self.state == LauncherState::CheckingBranches {
                            ui.label("Loading branches...");
                        } else if self.branches.is_empty() {
                            ui.label("No branches available");
                        } else {
                            for (name, _url) in &self.branches {
                                let selected = self.config.selected_branch == *name;
                                if ui.radio(selected, name).clicked() {
                                    self.config.selected_branch = name.clone();
                                    let _ = self.config.save();
                                }
                            }
                        }

                        ui.separator();

                        ui.horizontal(|ui| {
                            if ui.button("Close").clicked() {
                                self.show_branch_selector = false;
                                self.state = LauncherState::Idle;
                            }
                        });
                    });
            }

            ui.add_space(20.0);

            // Footer
            ui.with_layout(egui::Layout::bottom_up(egui::Align::Center), |ui| {
                ui.label(format!("Launcher v{}", env!("CARGO_PKG_VERSION")));
                ui.hyperlink_to("crumblt.com", "https://crumblt.com");
            });
        });

        // Keep requesting repaints while waiting for async operations
        if self.branch_receiver.is_some() || self.update_receiver.is_some() {
            ctx.request_repaint();
        }
    }
}

pub async fn run_launcher_ui() -> Result<()> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([520.0, 400.0])
            .with_resizable(false)
            .with_decorations(true)
            .with_transparent(false),
        ..Default::default()
    };

    eframe::run_native(
        "Crumblt Launcher",
        options,
        Box::new(|_cc| Box::new(LauncherApp::new())),
    ).map_err(|e| anyhow::anyhow!("UI error: {}", e))?;

    Ok(())
}
