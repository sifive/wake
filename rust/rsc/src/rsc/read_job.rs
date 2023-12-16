use crate::types::{Dir, File, ReadJobPayload, ReadJobResponse, Symlink};
use axum::Json;
use entity::{job, job_use, output_dir, output_file, output_symlink};
use hyper::StatusCode;
use sea_orm::{
    prelude::Uuid, ActiveModelTrait, ActiveValue::*, ColumnTrait, DatabaseConnection, DbErr,
    EntityTrait, ModelTrait, QueryFilter, TransactionTrait,
};
use std::sync::Arc;
use tracing;

#[tracing::instrument]
async fn record_use(job_id: Uuid, conn: Arc<DatabaseConnection>) {
    let usage = job_use::ActiveModel {
        id: NotSet,
        created_at: NotSet,
        job_id: Set(job_id),
    };
    let _ = usage.insert(conn.as_ref()).await;
}

#[tracing::instrument]
pub async fn read_job(
    Json(payload): Json<ReadJobPayload>,
    conn: Arc<DatabaseConnection>,
) -> (StatusCode, Json<ReadJobResponse>) {
    // First find the hash so we can look up the exact job
    let hash: Vec<u8> = payload.hash().into();

    let result = conn
        .as_ref()
        .transaction::<_, (Option<Uuid>, ReadJobResponse), DbErr>(|txn| {
            Box::pin(async move {
                let Some(matching_job) = job::Entity::find()
                    .filter(job::Column::Hash.eq(hash))
                    .one(txn)
                    .await?
                else {
                    return Ok((None, ReadJobResponse::NoMatch));
                };

                let output_files = matching_job
                    .find_related(output_file::Entity)
                    .all(txn)
                    .await?
                    .into_iter()
                    .map(|m| File {
                        path: m.path,
                        mode: m.mode,
                        blob_id: m.blob_id,
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

                Ok((
                    Some(matching_job.id),
                    ReadJobResponse::Match {
                        output_symlinks,
                        output_dirs,
                        output_files,
                        stdout_blob_id: matching_job.stdout_blob_id,
                        stderr_blob_id: matching_job.stderr_blob_id,
                        status: matching_job.status,
                        runtime: matching_job.runtime,
                        cputime: matching_job.cputime,
                        memory: matching_job.memory as u64,
                        ibytes: matching_job.i_bytes as u64,
                        obytes: matching_job.o_bytes as u64,
                    },
                ))
            })
        })
        .await;

    match result {
        Ok((Some(job_id), response)) => {
            // If we get a match we want to record the use but we don't
            // want to block sending the response on it so we spawn a task
            // to go do that.
            let mut status = StatusCode::NOT_FOUND;
            if let ReadJobResponse::Match { .. } = response {
                status = StatusCode::OK;
                let shared_conn = conn.clone();
                tokio::spawn(async move {
                    record_use(job_id, shared_conn).await;
                });
            }
            (status, Json(response))
        }
        Ok((None, _)) => (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch)),
        Err(cause) => {
            tracing::error! {
              %cause,
              "failed to add job"
            };
            (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch))
        }
    }
}
