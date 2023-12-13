//! `SeaORM` Entity. Generated by sea-orm-codegen 0.12.6

use sea_orm::entity::prelude::*;

#[derive(Clone, Debug, PartialEq, DeriveEntityModel, Eq)]
#[sea_orm(table_name = "blob")]
pub struct Model {
    #[sea_orm(primary_key, auto_increment = false)]
    pub id: String,
    pub store: i32,
    pub workspace_path: String,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {
    #[sea_orm(
        belongs_to = "super::blob_store::Entity",
        from = "Column::Store",
        to = "super::blob_store::Column::Id",
        on_update = "NoAction",
        on_delete = "Restrict"
    )]
    BlobStore,
    #[sea_orm(has_many = "super::job_blob::Entity")]
    JobBlob,
}

impl Related<super::blob_store::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::BlobStore.def()
    }
}

impl Related<super::job_blob::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::JobBlob.def()
    }
}

impl Related<super::job::Entity> for Entity {
    fn to() -> RelationDef {
        super::job_blob::Relation::Job.def()
    }
    fn via() -> Option<RelationDef> {
        Some(super::job_blob::Relation::Blob.def().rev())
    }
}

impl ActiveModelBehavior for ActiveModel {}
