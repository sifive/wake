use sea_orm::{prelude::Uuid, ColumnTrait, DatabaseConnection, EntityTrait, QueryFilter};

pub async fn fetch_local_blob_store(
    db: &DatabaseConnection,
) -> Result<Uuid, Box<dyn std::error::Error>> {
    let active_store = entity::prelude::BlobStore::find()
        .filter(entity::blob_store::Column::Type.eq("LocalBlobStore"))
        .one(db)
        .await?;

    let Some(active_store) = active_store else {
        return Err("Could not find active store".into());
    };

    Ok(active_store.id)
}
