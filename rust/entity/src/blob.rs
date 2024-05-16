//! `SeaORM` Entity. Generated by sea-orm-codegen 0.12.6

use sea_orm::entity::prelude::*;

#[derive(Clone, Debug, PartialEq, DeriveEntityModel, Eq)]
#[sea_orm(table_name = "blob")]
pub struct Model {
    #[sea_orm(primary_key, auto_increment = false)]
    pub id: Uuid,
    pub key: String,
    pub store_id: Uuid,
    pub size: i64,
    pub created_at: DateTime,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {
    #[sea_orm(
        belongs_to = "super::blob_store::Entity",
        from = "Column::StoreId",
        to = "super::blob_store::Column::Id",
        on_update = "NoAction",
        on_delete = "Restrict"
    )]
    BlobStore,
    #[sea_orm(has_many = "super::output_file::Entity")]
    OutputFile,
}

impl Related<super::blob_store::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::BlobStore.def()
    }
}

impl Related<super::output_file::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::OutputFile.def()
    }
}

impl ActiveModelBehavior for ActiveModel {}
