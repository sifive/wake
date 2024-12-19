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
    EntityTrait, ModelTrait, QueryFilter, TransactionTrait, ConnectionTrait,
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
async fn resolve_blob<T: ConnectionTrait>(
    id: Uuid,
    db: &T,
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
async fn verify_job<T: ConnectionTrait>(
    job:job::Model,
    db: &T,
    txn_output_file:Vec<output_file::Model>,
    txn_output_symlink:Vec<output_symlink::Model>,
    txn_output_dir:Vec<output_dir::Model>
) -> Result<bool, DbErr> {
    let updated_output_file = job.find_related(output_file::Entity).all(db).await?;
    let updated_output_symlink = job.find_related(output_symlink::Entity).all(db).await?;
    let updated_output_dir = job.find_related(output_dir::Entity).all(db).await?;
    // light check to verify that job contents were not deleted from database
    if  updated_output_file.len() != txn_output_file.len() ||
        updated_output_symlink.len() != txn_output_symlink.len() ||
        updated_output_dir.len() != txn_output_dir.len()
    {
        return Ok(false);
    }

    return Ok(true);
}

#[tracing::instrument(skip_all)]
pub async fn read_job(
    Json(payload): Json<ReadJobPayload>,
    conn: Arc<DatabaseConnection>,
    blob_stores: HashMap<Uuid, Arc<dyn blob::DebugBlobStore + Sync + Send>>,
) -> (StatusCode, Json<ReadJobResponse>) {
    let hash = payload.hash();
    let hash_for_spawns = hash.clone();

    // Fetch the job and related entities in a single transaction
    let fetch_result = conn
        .as_ref()
        .transaction::<_, Option<(job::Model, Vec<output_file::Model>, Vec<output_symlink::Model>, Vec<output_dir::Model>)>, DbErr>(|txn| {
            Box::pin(async move {
                let Some(matching_job) = job::Entity::find()
                    .filter(job::Column::Hash.eq(hash.clone()))
                    .one(txn)
                    .await?
                else {
                    tracing::info!(%hash, "Miss");
                    return Ok(None);
                };
            
                let output_files = matching_job.find_related(output_file::Entity).all(txn).await?;
                let output_symlinks = matching_job.find_related(output_symlink::Entity).all(txn).await?;
                let output_dirs = matching_job.find_related(output_dir::Entity).all(txn).await?;

                Ok(Some((matching_job, output_files, output_symlinks, output_dirs)))
            })
        })
        .await;
    
    let hash_copy = hash_for_spawns.clone();
    let Some((matching_job, output_files, output_symlinks, output_dirs)) = fetch_result.ok().flatten() else {
        tokio::spawn(async move {
            record_miss(hash_copy, conn.clone()).await;
        });
        return (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch));
    };

    let txn_output_files = output_files.clone();
    let txn_output_symlinks = output_symlinks.clone();
    let txn_output_dirs = output_dirs.clone();

    // Resolve blobs outside the transaction
    let output_files = futures::future::join_all(output_files.into_iter().map(|m| {
        let blob_stores_copy = blob_stores.clone();
        let blob_id = m.blob_id;
        let conn_copy = conn.clone();
        async move {
            resolve_blob(blob_id, conn_copy.as_ref(), &blob_stores_copy).await.map(|resolved_blob| ResolvedBlobFile {
                path: m.path,
                mode: m.mode,
                blob: resolved_blob,
            })
        }
    }))
    .await
    .into_iter()
    .collect::<Result<Vec<ResolvedBlobFile>, _>>();

    let output_files = match output_files {
        Ok(files) => files,
        Err(err) => {
            tracing::error!(%err, "Failed to resolve all output files. Resolving job as a cache miss.");
            return (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch));
        }
    };

    // Collect other resolved entities
    let output_symlinks: Vec<Symlink> = output_symlinks
        .into_iter()
        .map(|m| Symlink {
            path: m.path,
            link: m.link,
        })
        .collect();

    let output_dirs: Vec<Dir> = output_dirs
        .into_iter()
        .map(|m| Dir {
            path: m.path,
            mode: m.mode,
            hidden: Some(m.hidden),
        })
        .collect();

    // Resolve stdout and stderr blobs
    let stdout_blob = match resolve_blob(matching_job.stdout_blob_id, conn.as_ref(), &blob_stores).await {
            Err(err) => {
                tracing::error! {%err, "Failed to resolve stdout blob. Resolving job as a cache miss."};
                return (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch));
            },
            Ok(blob) => blob,
    };

    let stderr_blob = match resolve_blob(matching_job.stderr_blob_id, conn.as_ref(), &blob_stores).await {
            Err(err) => {
                tracing::error!(%err, "Failed to resolve stderr blob. Resolving job as a cache miss.");
                return (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch));
            },
            Ok(resolved_blob) => resolved_blob,
    };

    // Construct response
    let response = ReadJobResponse::Match {
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
    };

    // Verify that objects from transaction did not change
    let job_id = matching_job.id;
    let hash_copy = hash_for_spawns.clone();
    match verify_job(matching_job, conn.as_ref(), txn_output_files, txn_output_symlinks, txn_output_dirs).await {
        Ok(true) => {
            tracing::info!(%hash_copy, "Hit");
            tokio::spawn(async move {
                record_hit(job_id, hash_copy, conn.clone()).await;
            });

            (StatusCode::OK, Json(response))
        }
        Ok(false) | Err(_) => {
            tracing::error!("Job transaction instance is out of date. Resolving job as a cache miss.");
            tokio::spawn(async move {
                record_miss(hash_copy, conn.clone()).await;
            });
            
            (StatusCode::NOT_FOUND, Json(ReadJobResponse::NoMatch))
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
