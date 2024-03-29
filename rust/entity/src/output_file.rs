//! `SeaORM` Entity. Generated by sea-orm-codegen 0.12.6

use sea_orm::entity::prelude::*;

#[derive(Clone, Debug, PartialEq, DeriveEntityModel, Eq)]
#[sea_orm(table_name = "output_file")]
pub struct Model {
    #[sea_orm(primary_key, auto_increment = false)]
    pub id: Uuid,
    pub path: String,
    pub mode: i32,
    pub job_id: Uuid,
    pub blob_id: Uuid,
    pub created_at: DateTime,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {
    #[sea_orm(
        belongs_to = "super::blob::Entity",
        from = "Column::BlobId",
        to = "super::blob::Column::Id",
        on_update = "NoAction",
        on_delete = "Restrict"
    )]
    Blob,
    #[sea_orm(
        belongs_to = "super::job::Entity",
        from = "Column::JobId",
        to = "super::job::Column::Id",
        on_update = "NoAction",
        on_delete = "Cascade"
    )]
    Job,
}

impl Related<super::blob::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::Blob.def()
    }
}

impl Related<super::job::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::Job.def()
    }
}

impl ActiveModelBehavior for ActiveModel {}
