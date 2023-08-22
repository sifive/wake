use blake3;
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Serialize)]
pub struct VisibleFile {
    pub path: String,
    pub hash: [u8; 32],
}

#[derive(Debug, Deserialize, Serialize)]
pub struct File {
    pub path: String,
    pub mode: i32,
    #[serde(with = "serde_bytes")]
    pub hash: Vec<u8>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct Dir {
    pub path: String,
    pub mode: i32,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct Symlink {
    pub path: String,
    #[serde(with = "serde_bytes")]
    pub content: Vec<u8>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct AddJobPayload {
    pub cmd: String,
    pub env: String,
    pub cwd: String,
    pub stdin: String,
    pub is_atty: bool,
    #[serde(with = "serde_bytes")]
    pub hidden_info: Vec<u8>,
    pub visible_files: Vec<VisibleFile>,
    pub output_dirs: Vec<Dir>,
    pub output_symlinks: Vec<Symlink>,
    pub output_files: Vec<File>,
    #[serde(with = "serde_bytes")]
    pub stdout: Vec<u8>,
    #[serde(with = "serde_bytes")]
    pub stderr: Vec<u8>,
    pub status: i32,
    pub runtime: f64,
    pub cputime: f64,
    pub memory: u64,
    pub ibytes: u64,
    pub obytes: u64,
}

impl AddJobPayload {
    pub fn hash(&self) -> [u8; 32] {
        let mut hasher = blake3::Hasher::new();
        hasher.update(&self.cmd.len().to_le_bytes());
        hasher.update(self.cmd.as_bytes());
        hasher.update(&self.env.len().to_le_bytes());
        hasher.update(self.env.as_bytes());
        hasher.update(&self.cwd.len().to_le_bytes());
        hasher.update(self.cwd.as_bytes());
        hasher.update(&self.stdin.len().to_le_bytes());
        hasher.update(self.stdin.as_bytes());
        hasher.update(&self.hidden_info.len().to_le_bytes());
        hasher.update(self.hidden_info.as_slice());
        hasher.update(&[self.is_atty as u8]);
        hasher.update(&self.visible_files.len().to_le_bytes());
        for file in &self.visible_files {
            hasher.update(&file.path.len().to_le_bytes());
            hasher.update(file.path.as_bytes());
            hasher.update(&file.hash.len().to_le_bytes());
            hasher.update(&file.hash);
        }
        hasher.finalize().into()
    }
}

#[derive(Debug, Deserialize)]
pub struct ReadJobPayload {
    pub cmd: String,
    pub env: String,
    pub cwd: String,
    pub stdin: String,
    pub is_atty: bool,
    #[serde(with = "serde_bytes")]
    pub hidden_info: Vec<u8>,
    pub visible_files: Vec<VisibleFile>,
}

impl ReadJobPayload {
    // TODO: Figure out a way to de-dup this with AddJobPayload somehow
    pub fn hash(&self) -> [u8; 32] {
        let mut hasher = blake3::Hasher::new();
        hasher.update(&self.cmd.len().to_le_bytes());
        hasher.update(self.cmd.as_bytes());
        hasher.update(&self.env.len().to_le_bytes());
        hasher.update(self.env.as_bytes());
        hasher.update(&self.cwd.len().to_le_bytes());
        hasher.update(self.cwd.as_bytes());
        hasher.update(&self.stdin.len().to_le_bytes());
        hasher.update(self.stdin.as_bytes());
        hasher.update(&self.hidden_info.len().to_le_bytes());
        hasher.update(self.hidden_info.as_slice());
        hasher.update(&[self.is_atty as u8]);
        hasher.update(&self.visible_files.len().to_le_bytes());
        for file in &self.visible_files {
            hasher.update(&file.path.len().to_le_bytes());
            hasher.update(file.path.as_bytes());
            hasher.update(&file.hash.len().to_le_bytes());
            hasher.update(&file.hash);
        }
        hasher.finalize().into()
    }
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum ReadJobResponse {
    NoMatch,
    Match {
        output_symlinks: Vec<Symlink>,
        output_dirs: Vec<Dir>,
        output_files: Vec<File>,
        #[serde(with = "serde_bytes")]
        stdout: Vec<u8>,
        #[serde(with = "serde_bytes")]
        stderr: Vec<u8>,
        status: i32,
        runtime: f64,
        cputime: f64,
        memory: u64,
        ibytes: u64,
        obytes: u64,
    },
}
