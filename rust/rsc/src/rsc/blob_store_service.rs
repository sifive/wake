use chrono::NaiveDateTime;
use entity::prelude::{Blob, BlobStore, LocalBlobStore};
use entity::{blob, blob_store, local_blob_store};
use sea_orm::{prelude::Uuid, ColumnTrait, DatabaseConnection, EntityTrait, QueryFilter};
use sea_orm::{DbBackend, DbErr, Statement};

pub async fn fetch_local_blob_stores(
    db: &DatabaseConnection,
) -> Result<Vec<local_blob_store::Model>, DbErr> {
    LocalBlobStore::find().all(db).await
}

pub async fn fetch_test_blob_stores(
    db: &DatabaseConnection,
) -> Result<Vec<blob_store::Model>, DbErr> {
    BlobStore::find()
        .filter(blob_store::Column::Type.eq("TestBlobStore"))
        .all(db)
        .await
}

// Fetches blobs from the database that are unreferenced and have surpaced the alllotated grace
// period to be referenced.
//
// For new blobs this allows the client to create several blobs and then reference them all at
// once. Existing blobs whose job was just evicted will likely be well past the grace period and
// thus quickly evicted themselves.
pub async fn fetch_unreferenced_blobs(
    db: &DatabaseConnection,
    ttl: NaiveDateTime,
) -> Result<Vec<blob::Model>, DbErr> {
    // TODO: this can probably be refactored as a ORM query with something like
    // Blob::find()
    //     .filter(
    //         blob::Column::Id.not_in_subquery(
    //             sea_orm::query::Query::select()
    //                 .column(OutputFile::BlobId)
    //                 .from(OutputFile::Table
    //                 .union( *OTHER STUFF* ));
    //                 .take().

    // Limit = 16k as the query is also subject to parameter max.
    // Blob has 4 params so (2^16)/4 = 16384. Also generally best to chunk blob eviction
    // to avoid large eviction stalls.
    Blob::find()
        .from_raw_sql(Statement::from_sql_and_values(
            DbBackend::Postgres,
            r#"
            SELECT * FROM blob
            WHERE created_at <= $1
            AND id NOT IN
            (
                SELECT blob_id FROM output_file
                UNION SELECT stdout_blob_id FROM job
                UNION SELECT stderr_blob_id FROM job
            )
            LIMIT 16000
            "#,
            [ttl.into()],
        ))
        .all(db)
        .await
}

pub async fn delete_blobs_by_ids(db: &DatabaseConnection, ids: Vec<Uuid>) -> Result<u64, DbErr> {
    if ids.len() == 0 {
        return Ok(0);
    }

    entity::blob::Entity::delete_many()
        .filter(
            entity::blob::Column::Id.in_subquery(
                migration::Query::select()
                    .column(migration::Asterisk)
                    .from_values(ids, migration::Alias::new("foo"))
                    .take(),
            ),
        )
        .exec(db)
        .await
        .map(|i| i.rows_affected)
}
