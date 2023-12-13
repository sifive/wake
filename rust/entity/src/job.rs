//! `SeaORM` Entity. Generated by sea-orm-codegen 0.12.6

use sea_orm::entity::prelude::*;

#[derive(Clone, Debug, PartialEq, DeriveEntityModel)]
#[sea_orm(table_name = "job")]
pub struct Model {
    #[sea_orm(primary_key)]
    pub id: i32,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))", unique)]
    pub hash: Vec<u8>,
    pub cmd: String,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))")]
    pub env: Vec<u8>,
    pub cwd: String,
    pub stdin: String,
    pub is_atty: bool,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))")]
    pub hidden_info: Vec<u8>,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))")]
    pub stdout: Vec<u8>,
    #[sea_orm(column_type = "Binary(BlobSize::Blob(None))")]
    pub stderr: Vec<u8>,
    pub status: i32,
    #[sea_orm(column_type = "Double")]
    pub runtime: f64,
    #[sea_orm(column_type = "Double")]
    pub cputime: f64,
    pub memory: i64,
    pub i_bytes: i64,
    pub o_bytes: i64,
    pub created_at: DateTime,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {
    #[sea_orm(has_many = "super::job_blob::Entity")]
    JobBlob,
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

impl Related<super::job_blob::Entity> for Entity {
    fn to() -> RelationDef {
        Relation::JobBlob.def()
    }
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

impl Related<super::blob::Entity> for Entity {
    fn to() -> RelationDef {
        super::job_blob::Relation::Blob.def()
    }
    fn via() -> Option<RelationDef> {
        Some(super::job_blob::Relation::Job.def().rev())
    }
}

impl ActiveModelBehavior for ActiveModel {}
