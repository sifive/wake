use entity::local_blob_store;
use entity::prelude::LocalBlobStore;
use sea_orm::DbErr;
use sea_orm::{prelude::Uuid, DatabaseConnection, EntityTrait};

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
