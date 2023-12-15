//! `SeaORM` Entity. Generated by sea-orm-codegen 0.12.6

use sea_orm::entity::prelude::*;

#[derive(Clone, Debug, PartialEq, DeriveEntityModel, Eq)]
#[sea_orm(table_name = "blob_store")]
pub struct Model {
    #[sea_orm(primary_key)]
    pub id: i32,
    pub r#type: String,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {
    #[sea_orm(has_many = "super::blob::Entity")]
    Blob,
    #[sea_orm(has_many = "super::local_blob_store::Entity")]
    LocalBlobStore,
}

impl Related<super::blob::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::Blob.def()
    }
}

impl Related<super::local_blob_store::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::LocalBlobStore.def()
    }
}

impl ActiveModelBehavior for ActiveModel {}