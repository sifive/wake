use data_encoding::BASE64;
use entity::{api_key, blob_store, job, local_blob_store};
use rand_core::{OsRng, RngCore};
use sea_orm::prelude::Uuid;
use sea_orm::{
    ActiveModelTrait, ActiveValue::*, DatabaseConnection, DbErr, DeleteResult, EntityTrait, Query,
};
use tracing;
use entity::prelude::{ApiKey};

// --------------------------------------------------
// ----------          Api Key             ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_api_key(
    db: &DatabaseConnection,
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

pub async fn create_many_keys(
    db: &DatabaseConnection,
    descriptions: Vec<String>,
) -> Result<Unit, DbErr> {
    // let query = Query::delete()
    //     .from_table(Glyph::Table)
    //     .and_where(Expr::col(Glyph::Id).eq(1))
    //     .returning(Query::returning().column(Glyph::Id))
    //     .to_owned();
    // let query = Query::insert()  
    //    .into_table(api_key::Model);

}

// ----------             Read             ----------
pub async fn read_api_key(
    db: &DatabaseConnection,
    id: Uuid,
) -> Result<Option<api_key::Model>, DbErr> {
    api_key::Entity::find_by_id(id).one(db).await
}

pub async fn read_api_keys(db: &DatabaseConnection) -> Result<Vec<api_key::Model>, DbErr> {
    api_key::Entity::find().all(db).await
}

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_api_key(
    db: &DatabaseConnection,
    key: api_key::Model,
) -> Result<DeleteResult, DbErr> {
    tracing::info!("Deleting api key {}", key.key);
    api_key::Entity::delete_by_id(key.id).exec(db).await
}

// --------------------------------------------------
// ----------       Local Blob Store       ----------
// --------------------------------------------------

// ----------            Create            ----------
pub async fn create_local_blob_store(
    db: &DatabaseConnection,
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
pub async fn read_local_blob_store(
    db: &DatabaseConnection,
    id: Uuid,
) -> Result<Option<local_blob_store::Model>, DbErr> {
    local_blob_store::Entity::find_by_id(id).one(db).await
}

pub async fn read_local_blob_stores(
    db: &DatabaseConnection,
) -> Result<Vec<local_blob_store::Model>, DbErr> {
    local_blob_store::Entity::find().all(db).await
}

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_local_blob_store(
    db: &DatabaseConnection,
    store: local_blob_store::Model,
) -> Result<DeleteResult, DbErr> {
    tracing::info!("Deleting local blob store {}", store.id);
    local_blob_store::Entity::delete_by_id(store.id)
        .exec(db)
        .await?;
    blob_store::Entity::delete_by_id(store.id).exec(db).await
}

// --------------------------------------------------
// ----------             Job              ----------
// --------------------------------------------------

// ----------            Create            ----------

// ----------             Read             ----------

// ----------            Update            ----------

// ----------            Delete            ----------
pub async fn delete_all_jobs(db: &DatabaseConnection) -> Result<DeleteResult, DbErr> {
    tracing::info!("Deleting ALL jobs");
    job::Entity::delete_many().exec(db).await
}
