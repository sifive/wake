use chrono::NaiveDateTime;
use data_encoding::BASE64;
use entity::prelude::{
    ApiKey, Blob, BlobStore, Job, JobHistory, LocalBlobStore, OutputDir, OutputFile, OutputSymlink,
};
use entity::{
    api_key, blob, blob_store, job, job_history, local_blob_store, output_dir, output_file,
    output_symlink,
};
use itertools::Itertools;
use migration::OnConflict;
use rand_core::{OsRng, RngCore};
use sea_orm::{
    prelude::{DateTime, Expr, Uuid},
    ActiveModelTrait,
    ActiveValue::*,
    ColumnTrait, ConnectionTrait, DbBackend, DbErr, DeleteResult, EntityTrait, PaginatorTrait,
    QueryFilter, QueryOrder, Statement,
};
use sea_orm::{ExecResult, FromQueryResult};
use std::sync::Arc;
use tracing;

// The actual max is 65536, but adding an arbritrary buffer of 36 for any incidental parameters
const MAX_SQLX_PARAMS: u16 = u16::MAX - 36;

// --------------------------------------------------
// ----------          Api Key             ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_api_key<T: ConnectionTrait>(
    db: &T,
    key: Option<String>,
    description: String,
) -> Result<api_key::Model, DbErr> {
    // If a key wasn't specified, generate one.
    let key = match key {
        None => {
            let mut buf = [0u8; 24];
            OsRng.fill_bytes(&mut buf);
            BASE64.encode(&buf)
        }
        Some(key) => key,
    };

    tracing::info!("Adding key = {} as valid API key", &key);
    let active_key = api_key::ActiveModel {
        id: NotSet,
        created_at: NotSet,
        key: Set(key),
        desc: Set(description),
    };

    active_key.insert(db).await
}

// ----------             Read             ----------
pub async fn read_api_key<T: ConnectionTrait>(
    db: &T,
    id: Uuid,
) -> Result<Option<api_key::Model>, DbErr> {
    ApiKey::find_by_id(id).one(db).await
}

pub async fn read_api_keys<T: ConnectionTrait>(db: &T) -> Result<Vec<api_key::Model>, DbErr> {
    ApiKey::find().all(db).await
}

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_api_key<T: ConnectionTrait>(
    db: &T,
    key: api_key::Model,
) -> Result<DeleteResult, DbErr> {
    tracing::info!("Deleting api key {}", key.key);
    ApiKey::delete_by_id(key.id).exec(db).await
}

// --------------------------------------------------
// ----------          Blob Store          ----------
// --------------------------------------------------

// ----------            Create            ----------

// ----------             Read             ----------
pub async fn read_test_blob_stores<T: ConnectionTrait>(
    db: &T,
) -> Result<Vec<blob_store::Model>, DbErr> {
    BlobStore::find()
        .filter(blob_store::Column::Type.eq("TestBlobStore"))
        .all(db)
        .await
}

// ----------            Update            ----------

// ----------            Delete            ----------

// --------------------------------------------------
// ----------       Local Blob Store       ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_local_blob_store<T: ConnectionTrait>(
    db: &T,
    root: String,
) -> Result<local_blob_store::Model, DbErr> {
    let active_blob_store = blob_store::ActiveModel {
        id: NotSet,
        r#type: Set("LocalBlobStore".to_string()),
    };

    let store = active_blob_store.insert(db).await?;

    let active_local_blob_store = local_blob_store::ActiveModel {
        id: Set(store.id),
        created_at: NotSet,
        root: Set(root),
    };

    active_local_blob_store.insert(db).await
}

// ----------             Read             ----------
pub async fn read_local_blob_store<T: ConnectionTrait>(
    db: &T,
    id: Uuid,
) -> Result<Option<local_blob_store::Model>, DbErr> {
    LocalBlobStore::find_by_id(id).one(db).await
}

pub async fn read_local_blob_stores<T: ConnectionTrait>(
    db: &T,
) -> Result<Vec<local_blob_store::Model>, DbErr> {
    LocalBlobStore::find().all(db).await
}

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_local_blob_store<T: ConnectionTrait>(
    db: &T,
    store: local_blob_store::Model,
) -> Result<DeleteResult, DbErr> {
    tracing::info!("Deleting local blob store {}", store.id);
    LocalBlobStore::delete_by_id(store.id).exec(db).await?;
    BlobStore::delete_by_id(store.id).exec(db).await
}

// --------------------------------------------------
// ----------       DbOnly Blob Store       ----------
// --------------------------------------------------
// ----------            Create            ----------
pub async fn create_dbonly_blob_store<T: ConnectionTrait>(
    db: &T,
) -> Result<blob_store::Model, DbErr> {
    let active_blob_store = blob_store::ActiveModel {
        id: Set(read_dbonly_blob_store_id()),
        r#type: Set("DbOnlyBlobStore".to_string()),
    };

    active_blob_store.insert(db).await
}

// ----------             Read             ----------
// This creates a single source of truth for the DbOnly Blob Store id in case it ever needs to
// change. Unwrap is safe here since the string being parsed is static.
pub fn read_dbonly_blob_store_id() -> Uuid {
    Uuid::parse_str("00000000-0000-0000-0000-000000000000").unwrap()
}

pub async fn read_dbonly_blob_store<T: ConnectionTrait>(
    db: &T,
) -> Result<Option<blob_store::Model>, DbErr> {
    BlobStore::find_by_id(read_dbonly_blob_store_id())
        .one(db)
        .await
}

// ----------            Update            ----------

// ----------            Delete            ----------

// --------------------------------------------------
// ----------             Job              ----------
// --------------------------------------------------

// ----------            Create            ----------

// ----------             Read             ----------
pub async fn read_job<T: ConnectionTrait>(db: &T, id: Uuid) -> Result<Option<job::Model>, DbErr> {
    Job::find_by_id(id).one(db).await
}

pub async fn search_jobs_by_label<T: ConnectionTrait>(
    db: &T,
    label: &String,
) -> Result<Vec<job::Model>, DbErr> {
    Job::find()
        .filter(job::Column::Label.like(label))
        .order_by_asc(job::Column::Label)
        .all(db)
        .await
}

pub async fn count_jobs<T: ConnectionTrait>(db: &T) -> Result<u64, DbErr> {
    Job::find().count(db).await
}

#[derive(Debug, FromQueryResult)]
pub struct TimeSaved {
    pub savings: i64,
}

pub async fn time_saved<T: ConnectionTrait>(db: &T) -> Result<Option<TimeSaved>, DbErr> {
    TimeSaved::find_by_statement(Statement::from_string(
        DbBackend::Postgres,
        r#"
        SELECT CAST(round(sum(savings)) as BIGINT) as savings
        FROM (
            SELECT h.hits * j.runtime as savings
            FROM job_history h
            INNER JOIN job j
            ON j.hash = h.hash
        )
        "#,
    ))
    .one(db)
    .await
}

#[derive(Debug, FromQueryResult)]
pub struct OldestJobs {
    pub label: String,
    pub created_at: DateTime,
    pub reuses: i32,
    pub savings: i64,
}

pub async fn oldest_jobs<T: ConnectionTrait>(db: &T) -> Result<Vec<OldestJobs>, DbErr> {
    OldestJobs::find_by_statement(Statement::from_string(
        DbBackend::Postgres,
        r#"
        SELECT j.label, j.created_at, h.hits as reuses, CAST(round(h.hits * j.runtime) as BIGINT) as savings
        FROM job_history h
        INNER JOIN job j
        ON j.hash = h.hash
        ORDER BY j.created_at
        LIMIT 30
        "#,
    ))
    .all(db)
    .await
}

#[derive(Debug, FromQueryResult)]
pub struct MostReusedJob {
    pub label: String,
    pub reuses: i32,
    pub savings: i64,
}

pub async fn most_reused_jobs<T: ConnectionTrait>(db: &T) -> Result<Vec<MostReusedJob>, DbErr> {
    MostReusedJob::find_by_statement(Statement::from_string(
        DbBackend::Postgres,
        r#"
        SELECT j.label, h.hits as reuses, CAST(round(h.hits * j.runtime) as BIGINT) as savings
        FROM job_history h
        INNER JOIN job j
        ON j.hash = h.hash
        ORDER BY h.hits DESC
        LIMIT 30
        "#,
    ))
    .all(db)
    .await
}

#[derive(Debug, FromQueryResult)]
pub struct LostOpportunityJobs {
    pub label: String,
    pub reuses: i32,
    pub misses: i32,
    pub real_savings: i64,
    pub potential_savings: i64,
}

pub async fn lost_opportuinty_jobs<T: ConnectionTrait>(
    db: &T,
) -> Result<Vec<LostOpportunityJobs>, DbErr> {
    LostOpportunityJobs::find_by_statement(Statement::from_string(
        DbBackend::Postgres,
        r#"
        SELECT
            j.label,
            h.hits as reuses,
            h.misses - 1 as misses,
            CAST(round(h.hits * j.runtime) as BIGINT) as real_savings,
            CAST(round((h.hits + h.misses - 1) * j.runtime) as BIGINT) as potential_savings
        FROM job_history h
        INNER JOIN job j
        ON j.hash = h.hash
        ORDER BY potential_savings DESC
        LIMIT 30;
        "#,
    ))
    .all(db)
    .await
}

#[derive(Debug, FromQueryResult)]
pub struct SizeRuntimeValueJob {
    pub label: String,
    pub runtime: i64,
    pub disk_usage: i64,
    pub ms_saved_per_byte: i64,
}

pub async fn most_space_efficient_jobs<T: ConnectionTrait>(
    db: &T,
) -> Result<Vec<SizeRuntimeValueJob>, DbErr> {
    SizeRuntimeValueJob::find_by_statement(Statement::from_string(
        DbBackend::Postgres,
        r#"
        SELECT
            j.label,
            CAST(round(j.runtime) as BIGINT) as runtime,
            CAST(b.blob_size + stdout.size + stderr.size as BIGINT) as disk_usage,
            CAST(round(j.runtime / (b.blob_size + stdout.size + stderr.size) * 1000) as BIGINT) as ms_saved_per_byte
        FROM (
           SELECT o.job_id, sum(b.size) as blob_size
           FROM output_file o
           INNER JOIN blob b
           ON o.blob_id = b.id
           GROUP BY o.job_id
        ) b
        INNER JOIN job j
        ON j.id = b.job_id
        INNER JOIN blob stdout
        ON j.stdout_blob_id = stdout.id
        INNER JOIN blob stderr
        ON j.stderr_blob_id = stderr.id
        WHERE (b.blob_size + stdout.size + stderr.size) > 0
        ORDER BY ms_saved_per_byte DESC
        LIMIT 30;
        "#,
    ))
    .all(db)
    .await
}

pub async fn most_space_use_jobs<T: ConnectionTrait>(
    db: &T,
) -> Result<Vec<SizeRuntimeValueJob>, DbErr> {
    SizeRuntimeValueJob::find_by_statement(Statement::from_string(
        DbBackend::Postgres,
        r#"
        SELECT
            j.label,
            CAST(round(j.runtime) as BIGINT) as runtime,
            CAST(b.blob_size + stdout.size + stderr.size as BIGINT) as disk_usage,
            CAST(round(j.runtime / (b.blob_size + stdout.size + stderr.size) * 1000) as BIGINT) as ms_saved_per_byte
        FROM (
           SELECT o.job_id, sum(b.size) as blob_size
           FROM output_file o
           INNER JOIN blob b
           ON o.blob_id = b.id
           GROUP BY o.job_id
        ) b
        INNER JOIN job j
        ON j.id = b.job_id
        INNER JOIN blob stdout
        ON j.stdout_blob_id = stdout.id
        INNER JOIN blob stderr
        ON j.stderr_blob_id = stderr.id
        WHERE (b.blob_size + stdout.size + stderr.size) > 0
        ORDER BY disk_usage DESC
        LIMIT 30;
        "#,
    ))
    .all(db)
    .await
}

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_all_jobs<T: ConnectionTrait>(db: &T, chunk_size: u16) -> Result<u64, DbErr> {
    tracing::info!("Deleting ALL jobs");

    if chunk_size == 0 {
        panic!("Chunk size must be larger than zero");
    }

    let total = Job::find().count(db).await?;
    let mut affected = 0;

    loop {
        let exec: ExecResult = db
            .execute(Statement::from_sql_and_values(
                DbBackend::Postgres,
                r#"
                DELETE FROM job
                WHERE id IN
                (
                    SELECT id FROM job
                    LIMIT $1
                )
                "#,
                [chunk_size.into()],
            ))
            .await?;
        affected += exec.rows_affected();
        tracing::info!(%affected, %total, "Jobs deleted");

        if exec.rows_affected() <= chunk_size.into() {
            break Ok(affected);
        }
    }
}

pub async fn delete_job<T: ConnectionTrait>(
    db: &T,
    job: job::Model,
) -> Result<DeleteResult, DbErr> {
    tracing::info!(%job.id, "Deleting job");
    Job::delete_by_id(job.id).exec(db).await
}

#[derive(Debug, FromQueryResult)]
pub struct JobHistoryHash {
    hash: String,
}

pub async fn evict_jobs_ttl<T>(db: Arc<T>, ttl: NaiveDateTime) -> Result<usize, DbErr>
where
    T: ConnectionTrait + Send + 'static,
{
    let hashes = JobHistoryHash::find_by_statement(Statement::from_sql_and_values(
        DbBackend::Postgres,
        r#"
        DELETE FROM job 
        WHERE created_at <= $1
        RETURNING hash
        "#,
        [ttl.into()],
    ))
    .all(db.as_ref())
    .await?;

    let count = hashes.len();

    tokio::spawn(async move {
        for hash in hashes {
            let _ = upsert_job_evict(db.as_ref(), hash.hash).await;
        }
    });

    Ok(count)
}

// --------------------------------------------------
// ----------         Output File          ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_many_output_files<T: ConnectionTrait>(
    db: &T,
    output_files: Vec<output_file::ActiveModel>,
) -> Result<(), DbErr> {
    if output_files.len() == 0 {
        return Ok(());
    }

    let chunked: Vec<Vec<output_file::ActiveModel>> = output_files
        .into_iter()
        .chunks((MAX_SQLX_PARAMS / 6).into())
        .into_iter()
        .map(|chunk| chunk.collect())
        .collect();

    for chunk in chunked {
        OutputFile::insert_many(chunk).exec(db).await?;
    }

    Ok(())
}

// ----------             Read             ----------

// ----------            Update            ----------

// ----------            Delete            ----------

// --------------------------------------------------
// ----------        Output Symlink        ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_many_output_symlinks<T: ConnectionTrait>(
    db: &T,
    output_symlinks: Vec<output_symlink::ActiveModel>,
) -> Result<(), DbErr> {
    if output_symlinks.len() == 0 {
        return Ok(());
    }

    let chunked: Vec<Vec<output_symlink::ActiveModel>> = output_symlinks
        .into_iter()
        .chunks((MAX_SQLX_PARAMS / 5).into())
        .into_iter()
        .map(|chunk| chunk.collect())
        .collect();

    for chunk in chunked {
        OutputSymlink::insert_many(chunk).exec(db).await?;
    }

    Ok(())
}

// ----------             Read             ----------

// ----------            Update            ----------

// ----------            Delete            ----------

// --------------------------------------------------
// ----------          Output Dir          ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_many_output_dirs<T: ConnectionTrait>(
    db: &T,
    output_dirs: Vec<output_dir::ActiveModel>,
) -> Result<(), DbErr> {
    if output_dirs.len() == 0 {
        return Ok(());
    }

    let chunked: Vec<Vec<output_dir::ActiveModel>> = output_dirs
        .into_iter()
        .chunks((MAX_SQLX_PARAMS / 5).into())
        .into_iter()
        .map(|chunk| chunk.collect())
        .collect();

    for chunk in chunked {
        OutputDir::insert_many(chunk).exec(db).await?;
    }

    Ok(())
}

// ----------             Read             ----------

// ----------            Update            ----------

// ----------            Delete            ----------

// --------------------------------------------------
// ----------             Blob             ----------
// --------------------------------------------------
//
// ----------            Create            ----------
pub async fn upsert_blob<T: ConnectionTrait>(
    db: &T,
    blob: blob::ActiveModel,
) -> Result<Uuid, DbErr> {
    let result = Blob::insert(blob)
        .on_conflict(
            OnConflict::columns(vec![blob::Column::Key, blob::Column::StoreId])
                .update_column(blob::Column::UpdatedAt)
                .to_owned(),
        )
        .exec(db)
        .await?;

    Ok(result.last_insert_id)
}

// ----------             Read             ----------

// Reads blobs from the database that are unreferenced and have surpassed the allocated grace
// period to be referenced.
//
// For new blobs this allows the client to create several blobs and then reference them all at
// once. Existing blobs whose job was just evicted will likely be well past the grace period and
// thus quickly evicted themselves.
pub async fn read_unreferenced_blobs<T: ConnectionTrait>(
    db: &T,
    ttl: NaiveDateTime,
) -> Result<Vec<blob::Model>, DbErr> {
    // Limit = 16k as the query is also subject to parameter max.
    // Blob has 4 params so (2^16)/4 = 16384. Also generally best to chunk blob eviction
    // to avoid large eviction stalls.
    Blob::find()
        .from_raw_sql(Statement::from_sql_and_values(
            DbBackend::Postgres,
            r#"
            SELECT * FROM blob
            WHERE updated_at <= $1
            AND id IN
            (
                SELECT id FROM blob
                EXCEPT
                (
                    SELECT blob_id FROM output_file
                    UNION SELECT stdout_blob_id FROM job
                    UNION SELECT stderr_blob_id FROM job
                )
            )
            LIMIT $2
            "#,
            [ttl.into(), (MAX_SQLX_PARAMS / 4).into()],
        ))
        .all(db)
        .await
}

pub async fn count_blobs<T: ConnectionTrait>(db: &T) -> Result<u64, DbErr> {
    Blob::find().count(db).await
}

#[derive(Debug, FromQueryResult)]
pub struct TotalBlobSize {
    pub size: i64,
}

pub async fn total_blob_size<T: ConnectionTrait>(db: &T) -> Result<Option<TotalBlobSize>, DbErr> {
    TotalBlobSize::find_by_statement(Statement::from_string(
        DbBackend::Postgres,
        r#"
        SELECT CAST(sum(size) as BIGINT) as size
        FROM blob
        "#,
    ))
    .one(db)
    .await
}

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_blobs_by_ids<T: ConnectionTrait>(db: &T, ids: Vec<Uuid>) -> Result<u64, DbErr> {
    if ids.len() == 0 {
        return Ok(0);
    }

    let mut affected = 0;

    let chunked: Vec<Vec<Uuid>> = ids
        .into_iter()
        .chunks((MAX_SQLX_PARAMS / 1).into())
        .into_iter()
        .map(|chunk| chunk.collect())
        .collect();

    for chunk in chunked {
        let result = Blob::delete_many()
            .filter(
                entity::blob::Column::Id.in_subquery(
                    migration::Query::select()
                        .column(migration::Asterisk)
                        .from_values(chunk, migration::Alias::new("foo"))
                        .take(),
                ),
            )
            .exec(db)
            .await?;

        affected += result.rows_affected;
    }

    Ok(affected)
}

// --------------------------------------------------
// ----------          JobHistory          ----------
// --------------------------------------------------
pub async fn upsert_job_hit<T: ConnectionTrait>(db: &T, hash: String) -> Result<(), DbErr> {
    let active_model = job_history::ActiveModel {
        hash: Set(hash),
        hits: Set(1),
        misses: Set(0),
        evictions: Set(0),
        created_at: NotSet,
        updated_at: NotSet,
    };

    let _ = JobHistory::insert(active_model)
        .on_conflict(
            OnConflict::column(job_history::Column::Hash)
                .update_column(job_history::Column::UpdatedAt)
                .value(
                    job_history::Column::Hits,
                    Expr::col(job_history::Column::Hits.as_column_ref()).add(1),
                )
                .to_owned(),
        )
        .exec(db)
        .await?;

    Ok(())
}

pub async fn upsert_job_miss<T: ConnectionTrait>(db: &T, hash: String) -> Result<(), DbErr> {
    let active_model = job_history::ActiveModel {
        hash: Set(hash),
        hits: Set(0),
        misses: Set(1),
        evictions: Set(0),
        created_at: NotSet,
        updated_at: NotSet,
    };

    let _ = JobHistory::insert(active_model)
        .on_conflict(
            OnConflict::column(job_history::Column::Hash)
                .update_column(job_history::Column::UpdatedAt)
                .value(
                    job_history::Column::Misses,
                    Expr::col(job_history::Column::Misses.as_column_ref()).add(1),
                )
                .to_owned(),
        )
        .exec(db)
        .await?;

    Ok(())
}

pub async fn upsert_job_evict<T: ConnectionTrait>(db: &T, hash: String) -> Result<(), DbErr> {
    let active_model = job_history::ActiveModel {
        hash: Set(hash),
        hits: Set(0),
        misses: Set(0),
        evictions: Set(1),
        created_at: NotSet,
        updated_at: NotSet,
    };

    let _ = JobHistory::insert(active_model)
        .on_conflict(
            OnConflict::column(job_history::Column::Hash)
                .update_column(job_history::Column::UpdatedAt)
                .value(
                    job_history::Column::Evictions,
                    Expr::col(job_history::Column::Evictions.as_column_ref()).add(1),
                )
                .to_owned(),
        )
        .exec(db)
        .await?;

    Ok(())
}

// ----------            Create            ----------

// ----------             Read             ----------

// ----------            Update            ----------

// ----------            Delete            ----------
