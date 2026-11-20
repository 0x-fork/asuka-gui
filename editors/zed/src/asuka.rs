//! Asuka for Zed: runs the language server, `asuka lsp`.
//!
//! The `asuka` command is, in order: the path in the `lsp.asuka.binary`
//! setting, `asuka` on the worktree's PATH (where the install script puts
//! it), or the CLI of the latest release, downloaded into the extension's
//! own directory.

use std::fs;

use zed_extension_api::{
    self as zed, serde_json, settings::LspSettings, Architecture, DownloadedFileType,
    GithubReleaseOptions, LanguageServerId, LanguageServerInstallationStatus, Os, Result,
};

const REPO: &str = "egoist/asuka";
const SERVER: &str = "asuka";

struct Asuka {
    /// The downloaded CLI, once found or fetched in this session.
    downloaded: Option<String>,
}

impl Asuka {
    /// The CLI of the latest release, downloaded when it isn't already.
    fn download(&mut self, id: &LanguageServerId) -> Result<String> {
        if let Some(path) = &self.downloaded {
            if fs::metadata(path).is_ok_and(|m| m.is_file()) {
                return Ok(path.clone());
            }
        }
        zed::set_language_server_installation_status(
            id,
            &LanguageServerInstallationStatus::CheckingForUpdate,
        );
        let release = match zed::latest_github_release(
            REPO,
            GithubReleaseOptions {
                require_assets: true,
                pre_release: false,
            },
        ) {
            Ok(release) => release,
            Err(err) => {
                // Offline: the newest version downloaded before, if any.
                if let Some(path) = newest_downloaded() {
                    self.downloaded = Some(path.clone());
                    return Ok(path);
                }
                return Err(format!("looking up Asuka's latest release: {err}"));
            }
        };

        let (os, arch) = zed::current_platform();
        let (asset_name, file_type, exe) = match (os, arch) {
            (Os::Mac, Architecture::Aarch64) => ("asuka-darwin-arm64.tar.gz", DownloadedFileType::GzipTar, "asuka"),
            (Os::Linux, Architecture::X8664) => ("asuka-linux-x64.tar.gz", DownloadedFileType::GzipTar, "asuka"),
            (Os::Linux, Architecture::Aarch64) => ("asuka-linux-arm64.tar.gz", DownloadedFileType::GzipTar, "asuka"),
            (Os::Windows, Architecture::X8664) => ("asuka-windows-x64.zip", DownloadedFileType::Zip, "asuka.exe"),
            (Os::Windows, Architecture::Aarch64) => ("asuka-windows-arm64.zip", DownloadedFileType::Zip, "asuka.exe"),
            _ => {
                return Err(
                    "Asuka's releases have no CLI for this computer: build it (go build ./cmd/asuka) and set lsp.asuka.binary.path"
                        .into(),
                )
            }
        };
        let asset = release
            .assets
            .iter()
            .find(|a| a.name == asset_name)
            .ok_or_else(|| format!("Asuka {} has no {asset_name}", release.version))?;

        let dir = format!("asuka-{}", release.version);
        let path = format!("{dir}/{exe}");
        if !fs::metadata(&path).is_ok_and(|m| m.is_file()) {
            zed::set_language_server_installation_status(
                id,
                &LanguageServerInstallationStatus::Downloading,
            );
            zed::download_file(&asset.download_url, &dir, file_type)
                .map_err(|err| format!("downloading {asset_name}: {err}"))?;
            zed::make_file_executable(&path)?;
            // The versions before this one.
            if let Ok(entries) = fs::read_dir(".") {
                for entry in entries.flatten() {
                    let name = entry.file_name().to_string_lossy().into_owned();
                    if name.starts_with("asuka-") && name != dir {
                        fs::remove_dir_all(entry.path()).ok();
                    }
                }
            }
        }
        zed::set_language_server_installation_status(id, &LanguageServerInstallationStatus::None);
        self.downloaded = Some(path.clone());
        Ok(path)
    }
}

/// The CLI of the newest release downloaded before, for when GitHub can't
/// be reached.
fn newest_downloaded() -> Option<String> {
    let mut found: Vec<String> = fs::read_dir(".")
        .ok()?
        .flatten()
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.starts_with("asuka-"))
        .collect();
    found.sort();
    let dir = found.pop()?;
    ["asuka", "asuka.exe"]
        .iter()
        .map(|exe| format!("{dir}/{exe}"))
        .find(|p| fs::metadata(p).is_ok_and(|m| m.is_file()))
}

impl zed::Extension for Asuka {
    fn new() -> Self {
        Asuka { downloaded: None }
    }

    fn language_server_command(
        &mut self,
        id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<zed::Command> {
        let binary = LspSettings::for_worktree(SERVER, worktree)
            .ok()
            .and_then(|s| s.binary);
        let args = binary
            .as_ref()
            .and_then(|b| b.arguments.clone())
            .unwrap_or_else(|| vec!["lsp".into()]);
        let mut env = worktree.shell_env();
        if let Some(extra) = binary.as_ref().and_then(|b| b.env.clone()) {
            env.extend(extra);
        }
        let command = match binary.and_then(|b| b.path) {
            Some(path) => path,
            None => match worktree.which("asuka") {
                Some(path) => path,
                None => self.download(id)?,
            },
        };
        Ok(zed::Command { command, args, env })
    }

    fn language_server_initialization_options(
        &mut self,
        _id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<Option<serde_json::Value>> {
        Ok(LspSettings::for_worktree(SERVER, worktree)
            .ok()
            .and_then(|s| s.initialization_options))
    }

    fn language_server_workspace_configuration(
        &mut self,
        _id: &LanguageServerId,
        worktree: &zed::Worktree,
    ) -> Result<Option<serde_json::Value>> {
        Ok(LspSettings::for_worktree(SERVER, worktree)
            .ok()
            .and_then(|s| s.settings))
    }
}

zed::register_extension!(Asuka);
