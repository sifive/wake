use chrono::NaiveDateTime;
use entity::prelude::{Blob, LocalBlobStore};
use entity::{blob, local_blob_store};
use sea_orm::{prelude::Uuid, ColumnTrait, DatabaseConnection, EntityTrait, QueryFilter};
use sea_orm::{DbBackend, DbErr, DeleteResult, Statement};

pub async fn fetch_local_blob_store(
    db: &DatabaseConnection,
) -> Result<Uuid, Box<dyn std::error::Error>> {
    let active_store = LocalBlobStore::find().one(db).await?;

    let Some(active_store) = active_store else {
        return Err("Could not find active store".into());
    };

    Ok(active_store.id)
}

pub async fn fetch_local_blob_stores(
    db: &DatabaseConnection,
) -> Result<Vec<local_blob_store::Model>, DbErr> {
    LocalBlobStore::find().all(db).await
}

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
            "#,
            [ttl.into()],
        ))
        .all(db)
        .await
}

pub async fn delete_blobs_by_ids(
    db: &DatabaseConnection,
    ids: Vec<Uuid>,
) -> Result<DeleteResult, DbErr> {
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
}
