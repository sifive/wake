use crate::blob;
use crate::types::{Dir, ReadJobPayload, ReadJobResponse, ResolvedBlob, ResolvedBlobFile, Symlink};
use axum::Json;
use entity::{job, job_use, output_dir, output_file, output_symlink};
use hyper::StatusCode;
use sea_orm::DatabaseTransaction;
use sea_orm::{
    prelude::Uuid, ActiveModelTrait, ActiveValue::*, ColumnTrait, DatabaseConnection, DbErr,
    EntityTrait, ModelTrait, QueryFilter, TransactionTrait,
};
use std::collections::HashMap;
use std::sync::Arc;
use tracing;

#[tracing::instrument(skip_all)]
async fn record_use(job_id: Uuid, conn: Arc<DatabaseConnection>) {
    let usage = job_use::ActiveModel {
        id: NotSet,
        created_at: NotSet,
        job_id: Set(job_id),
    };
    let _ = usage.insert(conn.as_ref()).await;
}

async fn resolve_blob(
    id: Uuid,
    db: &DatabaseTransaction,
    stores: &HashMap<Uuid, Arc<dyn blob::DebugBlobStore + Sync + Send>>,
) -> Result<ResolvedBlob, String> {
    let Ok(Some(blob)) = entity::prelude::Blob::find_by_id(id).one(db).await else {
        return Err(format!("Unable to find blob {} by id", id));
    };

    let Some(store) = stores.get(&blob.store_id) else {
        return Err(format!(
            "Unable to find backing store {} for blob {}",
            blob.store_id, id
        ));
    };

    return Ok(ResolvedBlob {
        id: blob.id,
        url: store.download_url(blob.key).await,
    });
}

#[tracing::instrument(skip_all)]
pub async fn read_job(
    Json(payload): Json<ReadJobPayload>,
    conn: Arc<DatabaseConnection>,
    blob_stores: HashMap<Uuid, Arc<dyn blob::DebugBlobStore + Sync + Send>>,
) -> (StatusCode, Json<ReadJobResponse>) {
    let hash = payload.hash();
    tracing::info!(hash);

    // TODO: This transaction is quite large with a bunch of "serialized" queries. If read_job
    // becomes a bottleneck it should be rewritten such that joining on promises is delayed for as
    // long as possible. Another option would be to collect all blob ids ahead of time and make a
    // single db query to list them all out instead of a query per blob id.
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
                    .map(|m| {
                        let stores_copy = blob_stores.clone();
                        async move {
                            let blob = resolve_blob(m.blob_id, txn, &stores_copy).await?;

                            Ok(ResolvedBlobFile {
                                path: m.path,
                                mode: m.mode,
                                blob,
                            })
                        }
                    });

                let output_files: Result<Vec<ResolvedBlobFile>, String> =
                    futures::future::join_all(output_files)
                        .await
                        .into_iter()
                        .collect();

                let output_files = match output_files {
                    Err(err) => {
                        tracing::error! {%err, "Failed to resolve all output files. Resolving job as a cache miss."};
                        return Ok((None, ReadJobResponse::NoMatch))
                    },
                    Ok(files) => files,
                };

                let output_symlinks = matching_job
                    .find_related(output_symlink::Entity)
                    .all(txn)
                    .await?
                    .into_iter()
                    .map(|m| Symlink {
                        path: m.path,
                        link: m.link,
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

                let stdout_blob = match resolve_blob(matching_job.stdout_blob_id, txn, &blob_stores).await {
                    Err(err) => {
                        tracing::error! {%err, "Failed to resolve stdout blob. Resolving job as a cache miss."};
                        return Ok((None, ReadJobResponse::NoMatch))
                    },
                    Ok(blob) => blob,
                };

                let stderr_blob = match resolve_blob(matching_job.stderr_blob_id, txn, &blob_stores).await {
                    Err(err) => {
                        tracing::error! {%err, "Failed to resolve stderr blob. Resolving job as a cache miss."};
                        return Ok((None, ReadJobResponse::NoMatch))
                    },
                    Ok(blob) => blob,
                };

                Ok((
                    Some(matching_job.id),
                    ReadJobResponse::Match {
                        output_symlinks,
                        output_dirs,
                        output_files,
                        stdout_blob,
                        stderr_blob,
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
              "failed to read job"
            };
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ReadJobResponse::NoMatch),
            )
        }
    }
}
