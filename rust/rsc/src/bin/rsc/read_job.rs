use crate::blob;
use crate::types::{
    AllowJobPayload, Dir, JobKeyHash, ReadJobPayload, ReadJobResponse, ResolvedBlob,
    ResolvedBlobFile, Symlink,
};
use axum::Json;
use entity::{job, job_use, output_dir, output_file, output_symlink};
use hyper::StatusCode;
use rand::{thread_rng, Rng};
use rsc::database;
use sea_orm::DatabaseTransaction;
use sea_orm::{
    prelude::Uuid, ActiveModelTrait, ActiveValue::*, ColumnTrait, DatabaseConnection, DbErr,
    EntityTrait, ModelTrait, QueryFilter, TransactionTrait,
};
use std::collections::HashMap;
use std::sync::{Arc, RwLock};
use tracing;

#[tracing::instrument(skip(hash, conn))]
async fn record_hit(job_id: Uuid, hash: String, conn: Arc<DatabaseConnection>) {
    let usage = job_use::ActiveModel {
        id: NotSet,
        created_at: NotSet,
        job_id: Set(job_id),
    };
    let _ = usage.insert(conn.as_ref()).await;
    let _ = database::record_job_hit(conn.as_ref(), hash).await;
}

#[tracing::instrument(skip(hash, conn))]
async fn record_miss(hash: String, conn: Arc<DatabaseConnection>) {
    let _ = database::record_job_miss(conn.as_ref(), hash).await;
}

#[tracing::instrument(skip(db, stores))]
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

    // TODO: This transaction is quite large with a bunch of "serialized" queries. If read_job
    // becomes a bottleneck it should be rewritten such that joining on promises is delayed for as
    // long as possible. Another option would be to collect all blob ids ahead of time and make a
    // single db query to list them all out instead of a query per blob id.
    let result = conn
        .as_ref()
        .transaction::<_, (Option<Uuid>, ReadJobResponse), DbErr>(|txn| {
            let hash = hash.clone();
            Box::pin(async move {
                let Some(matching_job) = job::Entity::find()
                    .filter(job::Column::Hash.eq(hash.clone()))
                    .one(txn)
                    .await?
                else {
                    tracing::info!(%hash, "Miss");
                    return Ok((None, ReadJobResponse::NoMatch));
                };

                tracing::info!(%hash, "Hit");
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
                        hidden: Some(m.hidden),
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
                    record_hit(job_id, hash, shared_conn).await;
                });
            }
            (status, Json(response))
        }
        Ok((None, _)) => {
            let shared_conn = conn.clone();
            tokio::spawn(async move {
                record_miss(hash, shared_conn).await;
            });
            (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch))
        }
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

#[tracing::instrument(skip_all)]
pub async fn allow_job(
    Json(payload): Json<AllowJobPayload>,
    conn: Arc<DatabaseConnection>,
    target_load: f64,
    system_load: Arc<RwLock<f64>>,
    min_runtime: f64,
) -> StatusCode {
    let hash = payload.hash();

    // Reject a subset of jobs that are never worth caching
    if payload.runtime < min_runtime {
        let denied_hash = hash.clone();
        tokio::spawn(async move {
            let _ = database::record_job_denied(conn.as_ref(), denied_hash).await;
        });
        return StatusCode::NOT_ACCEPTABLE;
    }

    // Statistically reject jobs when the system should shed load
    let current_load = match system_load.read() {
        Ok(lock) => *lock,
        Err(err) => {
            tracing::error!(%err, "Unable to lock system load for reading. Returning load with 50% shed chance");
            target_load + target_load / 2.0
        }
    };

    // Determine the chance of shedding the job and clamp to 0.0-1.0
    let mut shed_chance = current_load / target_load - 1.0;
    shed_chance = f64::min(shed_chance, 1.0);
    shed_chance = f64::max(shed_chance, 0.0);

    // When under high load we expect that this route is being hit a lot. This has two effects
    // 1: The logs will get very spammy, 2: The act of logging will increase the load
    // This creates the opportunity of a feedback loop so we dampen the log rate based on the
    // current load. shed_chance = 0.5 log_chance = 0.51. shed_chance = 1.0, log_chance = 0.01
    if shed_chance > 0.5 && thread_rng().gen_bool(0.01 + (1.0 - shed_chance)) {
        tracing::warn!(%shed_chance, "Machine is highly loaded and more likely than not to shed a job");
    }

    if thread_rng().gen_bool(shed_chance) {
        let shed_hash = hash.clone();
        tokio::spawn(async move {
            let _ = database::record_job_shed(conn.as_ref(), shed_hash).await;
        });
        return StatusCode::TOO_MANY_REQUESTS;
    }

    // Reject jobs that are already cached
    match job::Entity::find()
        .filter(job::Column::Hash.eq(hash.clone()))
        .one(conn.as_ref())
        .await
    {
        // Unable to run the query to lookup the job
        Err(err) => {
            tracing::error!(%err, "Failed to search for cached job");
            StatusCode::INTERNAL_SERVER_ERROR
        }
        // Job is cached, don't try again
        Ok(Some(_)) => {
            tracing::warn!(%hash, "Rejecting job push for already cached job");
            tokio::spawn(async move {
                let _ = database::record_job_conflict(conn.as_ref(), hash).await;
            });
            StatusCode::CONFLICT
        }
        // Job is not cached, use the other deciding factors
        Ok(None) => StatusCode::OK,
    }
}
