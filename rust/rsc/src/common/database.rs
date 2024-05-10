use chrono::NaiveDateTime;
use data_encoding::BASE64;
use entity::prelude::{
    Blob, BlobStore, Job, LocalBlobStore, OutputDir, OutputFile, OutputSymlink, VisibleFile,
};
use entity::{
    api_key, blob, blob_store, job, local_blob_store, output_dir, output_file, output_symlink,
    visible_file,
};
use itertools::Itertools;
use rand_core::{OsRng, RngCore};
use sea_orm::ExecResult;
use sea_orm::{
    prelude::Uuid, ActiveModelTrait, ActiveValue::*, ColumnTrait, ConnectionTrait, DbBackend,
    DbErr, DeleteResult, EntityTrait, PaginatorTrait, QueryFilter, QueryOrder, QuerySelect, Statement,
};
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
    api_key::Entity::find_by_id(id).one(db).await
}

pub async fn read_api_keys<T: ConnectionTrait>(db: &T) -> Result<Vec<api_key::Model>, DbErr> {
    api_key::Entity::find().all(db).await
}

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_api_key<T: ConnectionTrait>(
    db: &T,
    key: api_key::Model,
) -> Result<DeleteResult, DbErr> {
    tracing::info!("Deleting api key {}", key.key);
    api_key::Entity::delete_by_id(key.id).exec(db).await
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
    local_blob_store::Entity::find_by_id(id).one(db).await
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
    local_blob_store::Entity::delete_by_id(store.id)
        .exec(db)
        .await?;
    blob_store::Entity::delete_by_id(store.id).exec(db).await
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
    blob_store::Entity::find_by_id(read_dbonly_blob_store_id())
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
    job::Entity::find_by_id(id).one(db).await
}

pub async fn search_jobs_by_label<T: ConnectionTrait>(
    db: &T,
    label: &String,
) -> Result<Vec<job::Model>, DbErr> {
    job::Entity::find()
        .filter(job::Column::Label.like(label))
        .order_by_asc(job::Column::Label)
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

    let total = entity::job::Entity::find().count(db).await?;
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
    job::Entity::delete_by_id(job.id).exec(db).await
}

// --------------------------------------------------
// ----------         Visible File         ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_many_visible_files<T: ConnectionTrait>(
    db: &T,
    visible_files: Vec<visible_file::ActiveModel>,
) -> Result<(), DbErr> {
    if visible_files.len() == 0 {
        return Ok(());
    }

    let chunked: Vec<Vec<visible_file::ActiveModel>> = visible_files
        .into_iter()
        .chunks((MAX_SQLX_PARAMS / 5).into())
        .into_iter()
        .map(|chunk| chunk.collect())
        .collect();

    for chunk in chunked {
        VisibleFile::insert_many(chunk).exec(db).await?;
    }

    Ok(())
}

// ----------             Read             ----------

// ----------            Update            ----------

// ----------            Delete            ----------

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

// ----------            Create            ----------

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
            WHERE created_at <= $1
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
        let result = entity::blob::Entity::delete_many()
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
