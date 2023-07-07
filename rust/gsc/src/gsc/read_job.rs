use axum::Json;
use entity::{job, output_dir, output_file, output_symlink};
use sea_orm::{
    ColumnTrait, DatabaseConnection, DbErr, EntityTrait, ModelTrait, QueryFilter, TransactionTrait,
};
use std::sync::Arc;
use tracing;

use crate::types::{Dir, File, ReadJobPayload, ReadJobResponse, Symlink};

#[tracing::instrument]
pub async fn read_job(
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

                let output_files = matching_job
                    .find_related(output_file::Entity)
                    .all(txn)
                    .await?
                    .into_iter()
                    .map(|m| File {
                        path: m.path,
                        hash: m.hash,
                        mode: m.mode,
                    })
                    .collect();

                let output_symlinks = matching_job
                    .find_related(output_symlink::Entity)
                    .all(txn)
                    .await?
                    .into_iter()
                    .map(|m| Symlink {
                        path: m.path,
                        content: m.content,
                    })
                    .collect();

                let output_dirs = matching_job
                    .find_related(output_dir::Entity)
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
