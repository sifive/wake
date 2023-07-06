use axum::{http::StatusCode, routing::post, Json, Router};
//use blake2::Digest;
//use blake2::{digest::typenum::U32, Blake2b};
use entity::{job, output_dir, output_file, output_symlink, visible_file};
use migration::{Migrator, MigratorTrait};
use sea_orm::{
    ActiveModelTrait, ActiveValue, ColumnTrait, DatabaseConnection, DbErr, EntityTrait,
    QueryFilter, TransactionTrait,
};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tracing;

#[derive(Debug, Deserialize, Serialize)]
struct VisibleFile {
    path: String,
    hash: [u8; 32],
}

#[derive(Debug, Deserialize, Serialize)]
struct File {
    path: String,
    mode: i32,
    #[serde(with = "serde_bytes")]
    hash: Vec<u8>,
}

#[derive(Debug, Deserialize, Serialize)]
struct Dir {
    path: String,
    mode: i32,
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
    env: String,
    cwd: String,
    stdin: String,
    is_atty: bool,
    #[serde(with = "serde_bytes")]
    hidden_info: Vec<u8>,
    visible_files: Vec<VisibleFile>,
    output_dirs: Vec<Dir>,
    output_symlinks: Vec<Symlink>,
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
}

impl AddJobPayload {
    fn hash(&self) -> [u8; 32] {
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
struct ReadJobPayload {
    cmd: String,
    env: String,
    cwd: String,
    stdin: String,
    is_atty: bool,
    #[serde(with = "serde_bytes")]
    hidden_info: Vec<u8>,
    visible_files: Vec<VisibleFile>,
}

impl ReadJobPayload {
    // TODO: Figure out a way to de-dup this with AddJobPayload somehow
    fn hash(&self) -> [u8; 32] {
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
enum ReadJobResponse {
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

#[tracing::instrument]
async fn read_job(
    Json(payload): Json<ReadJobPayload>,
    conn: Arc<DatabaseConnection>,
) -> Json<ReadJobResponse> {
    // First find the hash so we can look up the exact job
    let hash: Vec<u8> = payload.hash().into();

    let result = conn
        .as_ref()
        .transaction::<_, ReadJobResponse, DbErr>(|txn| {
            Box::pin(async move {
                let Some(matching_job) = job::Entity::find()
                    .filter(job::Column::Hash.eq(hash))
                    .one(txn)
                    .await?
                else {
                  return Ok(ReadJobResponse::NoMatch);
                };

                let output_files = output_file::Entity::find()
                    .filter(output_file::Column::JobId.eq(matching_job.id))
                    .all(txn)
                    .await?
                    .into_iter()
                    .map(|m| File {
                        path: m.path,
                        hash: m.hash,
                        mode: m.mode,
                    })
                    .collect();

                let output_symlinks = output_symlink::Entity::find()
                    .filter(output_symlink::Column::JobId.eq(matching_job.id))
                    .all(txn)
                    .await?
                    .into_iter()
                    .map(|m| Symlink {
                        path: m.path,
                        content: m.content,
                    })
                    .collect();

                let output_dirs = output_dir::Entity::find()
                    .filter(output_dir::Column::JobId.eq(matching_job.id))
                    .all(txn)
                    .await?
                    .into_iter()
                    .map(|m| Dir {
                        path: m.path,
                        mode: m.mode,
                    })
                    .collect();

                Ok(ReadJobResponse::Match {
                    output_symlinks,
                    output_dirs,
                    output_files,
                    stdout: matching_job.stdout,
                    stderr: matching_job.stderr,
                    status: matching_job.status,
                    runtime: matching_job.runtime,
                    cputime: matching_job.cputime,
                    memory: matching_job.memory as u64,
                    ibytes: matching_job.i_bytes as u64,
                    obytes: matching_job.o_bytes as u64,
                })
            })
        })
        .await;

    match result {
        Ok(result) => Json(result),
        Err(cause) => {
            tracing::error! {
              %cause,
              "failed to add job"
            };
            Json(ReadJobResponse::NoMatch)
        }
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
        env: ActiveValue::Set(payload.env.as_bytes().into()),
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

    // Now perform the insert as a single transaction
    let insert_result = conn
        .as_ref()
        .transaction::<_, (), DbErr>(|txn| {
            Box::pin(async move {
                let job = insert_job.save(txn).await?;
                let job_id = job.id.unwrap();

                for vis_file in vis {
                    visible_file::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(vis_file.path),
                        hash: ActiveValue::Set(vis_file.hash.into()),
                        job_id: ActiveValue::Set(job_id),
                    }
                    .save(txn)
                    .await?;
                }

                for out_file in output_files {
                    output_file::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(out_file.path),
                        hash: ActiveValue::Set(out_file.hash.into()),
                        mode: ActiveValue::Set(out_file.mode),
                        job_id: ActiveValue::Set(job_id),
                    }
                    .save(txn)
                    .await?;
                }
                for out_symlink in output_symlinks {
                    output_symlink::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(out_symlink.path),
                        content: ActiveValue::Set(out_symlink.content),
                        job_id: ActiveValue::Set(job_id),
                    }
                    .save(txn)
                    .await?;
                }
                for dir in output_dirs {
                    output_dir::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(dir.path),
                        mode: ActiveValue::Set(dir.mode),
                        job_id: ActiveValue::Set(job_id),
                    }
                    .save(txn)
                    .await?;
                }
                Ok(())
            })
        })
        .await;
    match insert_result {
        Ok(_) => StatusCode::OK,
        // TODO: We should returnn 500 on some errors
        //       but 4** when the user trys to add an invalid job
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
    let app = Router::new()
        .route(
            "/job",
            post({
                let shared_state = state.clone();
                move |body| add_job(body, shared_state)
            }),
        )
        .route(
            "/job/matching",
            post({
                let shared_state = state.clone();
                move |body| read_job(body, shared_state)
            }),
        );

    // run it with hyper on localhost:3000
    axum::Server::bind(&"127.0.0.1:3000".parse()?)
        .serve(app.into_make_service())
        .await?;
    Ok(())
}
