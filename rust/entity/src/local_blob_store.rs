//! `SeaORM` Entity. Generated by sea-orm-codegen 0.12.6

use sea_orm::entity::prelude::*;

#[derive(Clone, Debug, PartialEq, DeriveEntityModel, Eq)]
#[sea_orm(table_name = "local_blob_store")]
pub struct Model {
    #[sea_orm(primary_key, auto_increment = false)]
    pub id: i32,
    pub root: String,
    pub created_at: DateTime,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {
    #[sea_orm(
        belongs_to = "super::blob_store::Entity",
        from = "Column::Id",
        to = "super::blob_store::Column::Id",
        on_update = "NoAction",
        on_delete = "Restrict"
    )]
    BlobStore,
}

impl Related<super::blob_store::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::BlobStore.def()
    }
}

impl ActiveModelBehavior for ActiveModel {}
