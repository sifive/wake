use axum::{http::StatusCode, routing::post, Json, Router};
use blake2::{digest::typenum::U32, Blake2b};
use entity::job;
use migration::{Migrator, MigratorTrait};
use sea_orm::{ActiveModelTrait, ActiveValue, DatabaseConnection};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tracing;

#[derive(Debug, Deserialize, Serialize)]
struct File {
    path: String,
    hash: [u8; 32],
}

#[derive(Debug, Deserialize, Serialize)]
struct Symlink {
    path: String,
    #[serde(with = "serde_bytes")]
    content: Vec<u8>,
}

#[derive(Debug, Deserialize, Serialize)]
struct AddJobPayload {
    cmd: String,
    env: Vec<String>,
    cwd: String,
    stdin: String,
    is_atty: bool,
    hidden_info: String,
    visible_files: Vec<File>,
    output_dirs: Vec<String>,
    output_symlinks: Vec<Symlink>,
    output_file: Vec<File>,
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
}

impl AddJobPayload {
    fn hash(&self) -> [u8; 32] {
        let mut hasher = Blake2b::<U32>::new();
        hasher.update(self.cmd.len());
        hasher.update(self.cmd);
        hasher.update(self.env.len());
        hasher.update(self.env);
        hasher.update(self.cwd.len());
        hasher.update(self.cwd);
        hasher.update(self.stdin.len());
        hasher.update(self.stdin);
        hasher.update(self.hidden_info.len());
        hasher.update(self.hidden_info);
        hasher.update(self.visible_files.len());
        for file in self.visible_files {
            hasher.update(file.path.len());
            hasher.update(file.path);
            hasher.update(file.hash.len());
            hasher.update(file.hash);
        }
        hasher.into()
    }
}

#[derive(Debug, Deserialize)]
struct ReadJobPayload {
    cmd: String,
    env: String,
    cwd: String,
    stdin: String,
    is_atty: bool,
    hidden_info: String,
    visible_files: Vec<String>,
}

impl ReadJobPayload {
    fn hash(&self) -> Vec<u8> {
        let mut hasher = Blake2b::<U32>::new();
        hasher.update(self.cmd.len());
        hasher.update(self.cmd);
        hasher.update(self.env.len());
        hasher.update(self.env);
        hasher.update(self.cwd.len());
        hasher.update(self.cwd);
        hasher.update(self.stdin.len());
        hasher.update(self.stdin);
        hasher.update(self.is_atty);
        hasher.update(self.hidden_info.len());
        hasher.update(self.hidden_info);
        hasher.update(self.visible_files.len());
        for file in self.visible_files {
            hasher.update(file.path.len());
            hasher.update(file.path);
            hasher.update(file.hash.len());
            hasher.update(file.hash);
        }
        hasher.finalize().into();
    }
}
#[tracing::instrument]
async fn add_job(Json(payload): Json<AddJobPayload>, conn: Arc<DatabaseConnection>) -> StatusCode {
    let hash = payload.hash();
    let insert_job = job::ActiveModel {
        hash: ActiveModel::Set(hash.clone().into()),
    };
    let insert_result = insert_job.insert(conn.as_ref()).await;
    match insert_result {
        Ok(_) => StatusCode::OK,
        Err(cause) => {
            tracing::error! {
              %cause,
              "failed to add job"
            };
            StatusCode::BAD_REQUEST
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // setup a subscriber so that we always have logging
    let subscriber = tracing_subscriber::FmtSubscriber::new();
    tracing::subscriber::set_global_default(subscriber)?;

    // connect to our db
    let connection = sea_orm::Database::connect("postgres://127.0.0.1/test").await?;
    Migrator::up(&connection, None).await?;
    let state = Arc::new(connection);

    // build our application with a single route
    let app = Router::new().route(
        "/job",
        post({
            let shared_state = state.clone();
            move |body| add_job(body, shared_state)
        }),
    );

    // run it with hyper on localhost:3000
    axum::Server::bind(&"127.0.0.1:3000".parse()?)
        .serve(app.into_make_service())
        .await?;
    Ok(())
}
