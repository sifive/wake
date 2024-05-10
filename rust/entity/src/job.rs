//! `SeaORM` Entity. Generated by sea-orm-codegen 0.12.6

use sea_orm::entity::prelude::*;

#[derive(Clone, Debug, PartialEq, DeriveEntityModel)]
#[sea_orm(table_name = "job")]
pub struct Model {
    #[sea_orm(primary_key, auto_increment = false)]
    pub id: Uuid,
    #[sea_orm(unique)]
    pub hash: String,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))")]
    pub cmd: Vec<u8>,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))")]
    pub env: Vec<u8>,
    pub cwd: String,
    pub stdin: String,
    pub is_atty: bool,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))")]
    pub hidden_info: Vec<u8>,
    pub stdout_blob_id: Uuid,
    pub stderr_blob_id: Uuid,
    pub status: i32,
    #[sea_orm(column_type = "Double")]
    pub runtime: f64,
    #[sea_orm(column_type = "Double")]
    pub cputime: f64,
    pub memory: i64,
    pub i_bytes: i64,
    pub o_bytes: i64,
    pub created_at: DateTime,
    pub label: String,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {
    #[sea_orm(
        belongs_to = "super::blob::Entity",
        from = "Column::StderrBlobId",
        to = "super::blob::Column::Id",
        on_update = "NoAction",
        on_delete = "Restrict"
    )]
    Blob2,
    #[sea_orm(
        belongs_to = "super::blob::Entity",
        from = "Column::StdoutBlobId",
        to = "super::blob::Column::Id",
        on_update = "NoAction",
        on_delete = "Restrict"
    )]
    Blob1,
    #[sea_orm(has_many = "super::job_use::Entity")]
    JobUse,
    #[sea_orm(has_many = "super::output_dir::Entity")]
    OutputDir,
    #[sea_orm(has_many = "super::output_file::Entity")]
    OutputFile,
    #[sea_orm(has_many = "super::output_symlink::Entity")]
    OutputSymlink,
    #[sea_orm(has_many = "super::visible_file::Entity")]
    VisibleFile,
}

impl Related<super::job_use::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::JobUse.def()
    }
}

impl Related<super::output_dir::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::OutputDir.def()
    }
}

impl Related<super::output_file::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::OutputFile.def()
    }
}

impl Related<super::output_symlink::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::OutputSymlink.def()
    }
}

impl Related<super::visible_file::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::VisibleFile.def()
    }
}

impl ActiveModelBehavior for ActiveModel {}
