use axum::{http::StatusCode, routing::post, Json, Router};
use blake2::{
    digest::typenum::{private::IsNotEqualPrivate, U32},
    Blake2b,
};
use entity::{job, output_dir, output_file, output_symlink, visible_file};
use migration::{Migrator, MigratorTrait};
use sea_orm::{ActiveModelTrait, ActiveValue, DatabaseConnection, EntityTrait, TransactionTrait};
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
    #[serde(with = "serde_bytes")]
    env: Vec<u8>,
    cwd: String,
    stdin: String,
    is_atty: bool,
    #[serde(with = "serde_bytes")]
    hidden_info: Vec<u8>,
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
    #[serde(with = "serde_bytes")]
    hidden_info: Vec<u8>,
    visible_files: Vec<File>,
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
        hasher.finalize().into()
    }
}
#[tracing::instrument]
async fn add_job(Json(payload): Json<AddJobPayload>, conn: Arc<DatabaseConnection>) -> StatusCode {
    // First construct all the job details as an ActiveModel for insert
    let hash = payload.hash();
    let vis = payload.visible_files;
    let output_files = payload.output_files;
    let output_symlinks = payload.output_symlinks;
    let output_dirs = payload.output_dirs;
    let insert_job = job::ActiveModel {
        id: ActiveValue::NotSet,
        hash: ActiveValue::Set(hash.clone().into()),
        cmd: ActiveValue::Set(payload.cmd),
        env: ActiveValue::Set(payload.env),
        cwd: ActiveValue::Set(payload.cwd),
        stdin: ActiveValue::Set(payload.stdin),
        is_atty: ActiveValue::Set(payload.is_atty),
        hidden_info: ActiveValue::Set(payload.hidden_info),
        stdout: ActiveValue::Set(payload.stdout),
        stderr: ActiveValue::Set(payload.stderr),
        status: ActiveValue::Set(payload.status),
        runtime: ActiveValue::Set(payload.runtime),
        cputime: ActiveValue::Set(payload.cputime),
        memory: ActiveValue::Set(payload.memory as i64),
        i_bytes: ActiveValue::Set(payload.ibytes as i64),
        o_bytes: ActiveValue::Set(payload.obytes as i64),
    };
    let insert_vis = vec![];
    let insert_files = vec![];
    let insert_symlinks = vec![];
    let insert_dirs = vec![];
    for vis_file in vis {
        insert_vis.push(visible_file::ActiveModel {})
    }

    for out_file in output_files {
        insert_files.push(output_file::ActiveModel {})
    }
    for out_symlink in output_symlinks {
        insert_symlinks.push(output_symlink::ActiveModel {})
    }
    for dir in output_dirs {
        insert_dirs.push(output_dir::ActiveModel {})
    }

    // Now perform the insert as a single transaction
    let insert_result = conn
        .as_ref()
        .transaction(|txn| {
            Box::pin(async move {
                insert_job.save(txn);
                insert_vis.into_iter().map(|x| x.save(txn));
                insert_files.into_iter().map(|x| x.save(txn));
                insert_dirs.into_iter().map(|x| x.save(txn));
                insert_symlinks.into_iter().map(|x| x.save(txn));
                Ok(())
            })
        })
        .await;
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
