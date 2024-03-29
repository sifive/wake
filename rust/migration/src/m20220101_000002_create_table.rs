use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

trait EzBlob {
    fn ezblob(&mut self) -> &mut Self;
}

impl EzBlob for ColumnDef {
    fn ezblob(&mut self) -> &mut Self {
        self.blob(BlobSize::Blob(None)).not_null()
    }
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        // We create two indexes here implicitly, one on the primary key, and one on
        // the hash because its unique.
        manager
            .create_table(
                Table::create()
                    .table(Job::Table)
                    .col(
                        ColumnDef::new(Job::Id)
                            .uuid()
                            .not_null()
                            .primary_key()
                            .default(SimpleExpr::FunctionCall(PgFunc::gen_random_uuid())),
                    )
                    .col(ColumnDef::new(Job::Hash).string().not_null().unique_key())
                    .col(ColumnDef::new(Job::Cmd).ezblob().not_null())
                    .col(ColumnDef::new(Job::Env).ezblob())
                    .col(ColumnDef::new(Job::Cwd).string().not_null())
                    .col(ColumnDef::new(Job::Stdin).string().not_null())
                    .col(ColumnDef::new(Job::IsAtty).boolean().not_null())
                    .col(ColumnDef::new(Job::HiddenInfo).ezblob())
                    .col(ColumnDef::new(Job::StdoutBlobId).uuid().not_null())
                    .col(ColumnDef::new(Job::StderrBlobId).uuid().not_null())
                    .col(ColumnDef::new(Job::Status).integer().not_null())
                    .col(ColumnDef::new(Job::Runtime).double().not_null())
                    .col(ColumnDef::new(Job::Cputime).double().not_null())
                    .col(ColumnDef::new(Job::Memory).big_unsigned().not_null())
                    .col(ColumnDef::new(Job::IBytes).big_unsigned().not_null())
                    .col(ColumnDef::new(Job::OBytes).big_unsigned().not_null())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-stdout_blob_id-blob")
                            .from_tbl(Job::Table)
                            .from_col(Job::StdoutBlobId)
                            .to_tbl(Blob::Table)
                            .to_col(Blob::Id)
                            // A blob may not be deleted if there exists a
                            // job that depends on it.
                            .on_delete(ForeignKeyAction::Restrict),
                    )
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-stderr_blob_id-blob")
                            .from_tbl(Job::Table)
                            .from_col(Job::StderrBlobId)
                            .to_tbl(Blob::Table)
                            .to_col(Blob::Id)
                            // A blob may not be deleted if there exists a
                            // job that depends on it.
                            .on_delete(ForeignKeyAction::Restrict),
                    )
                    .to_owned(),
            )
            .await?;

        // Here we implicitly create two indexes, one on the job_id and one on the
        // primary key.
        manager
            .create_table(
                Table::create()
                    .table(VisibleFile::Table)
                    .col(
                        ColumnDef::new(VisibleFile::Id)
                            .uuid()
                            .not_null()
                            .primary_key()
                            .default(SimpleExpr::FunctionCall(PgFunc::gen_random_uuid())),
                    )
                    .col(ColumnDef::new(VisibleFile::Path).string().not_null())
                    .col(ColumnDef::new(VisibleFile::Hash).string().not_null())
                    .col(ColumnDef::new(VisibleFile::JobId).uuid())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-visible_file-job")
                            .from_tbl(VisibleFile::Table)
                            .from_col(VisibleFile::JobId)
                            .to_tbl(Job::Table)
                            .to_col(Job::Id)
                            .on_delete(ForeignKeyAction::Cascade),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .create_table(
                Table::create()
                    .table(OutputFile::Table)
                    .col(
                        ColumnDef::new(OutputFile::Id)
                            .uuid()
                            .not_null()
                            .primary_key()
                            .default(SimpleExpr::FunctionCall(PgFunc::gen_random_uuid())),
                    )
                    .col(ColumnDef::new(OutputFile::Path).string().not_null())
                    .col(ColumnDef::new(OutputFile::Mode).integer().not_null())
                    .col(ColumnDef::new(OutputFile::JobId).uuid())
                    .col(ColumnDef::new(OutputFile::BlobId).uuid().not_null())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-output_file-job")
                            .from_tbl(OutputFile::Table)
                            .from_col(OutputFile::JobId)
                            .to_tbl(Job::Table)
                            .to_col(Job::Id)
                            .on_delete(ForeignKeyAction::Cascade),
                    )
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-blob_id-blob")
                            .from_tbl(OutputFile::Table)
                            .from_col(OutputFile::BlobId)
                            .to_tbl(Blob::Table)
                            .to_col(Blob::Id)
                            // A blob may not be deleted if there exists an
                            // output file that depends on it.
                            .on_delete(ForeignKeyAction::Restrict),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .create_table(
                Table::create()
                    .table(OutputSymlink::Table)
                    .col(
                        ColumnDef::new(OutputSymlink::Id)
                            .uuid()
                            .not_null()
                            .primary_key()
                            .default(SimpleExpr::FunctionCall(PgFunc::gen_random_uuid())),
                    )
                    .col(ColumnDef::new(OutputSymlink::Path).string().not_null())
                    .col(ColumnDef::new(OutputSymlink::Link).string().not_null())
                    .col(ColumnDef::new(OutputSymlink::JobId).uuid())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-output_file-job")
                            .from_tbl(OutputSymlink::Table)
                            .from_col(OutputSymlink::JobId)
                            .to_tbl(Job::Table)
                            .to_col(Job::Id)
                            .on_delete(ForeignKeyAction::Cascade),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .create_table(
                Table::create()
                    .table(OutputDir::Table)
                    .col(
                        ColumnDef::new(OutputDir::Id)
                            .uuid()
                            .not_null()
                            .primary_key()
                            .default(SimpleExpr::FunctionCall(PgFunc::gen_random_uuid())),
                    )
                    .col(ColumnDef::new(OutputDir::Path).string().not_null())
                    .col(ColumnDef::new(OutputDir::Mode).integer().not_null())
                    .col(ColumnDef::new(OutputDir::JobId).uuid())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-output_file-job")
                            .from_tbl(OutputDir::Table)
                            .from_col(OutputDir::JobId)
                            .to_tbl(Job::Table)
                            .to_col(Job::Id)
                            .on_delete(ForeignKeyAction::Cascade),
                    )
                    .to_owned(),
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(sea_query::Table::drop().table(OutputDir::Table).to_owned())
            .await?;
        manager
            .drop_table(sea_query::Table::drop().table(OutputFile::Table).to_owned())
            .await?;
        manager
            .drop_table(
                sea_query::Table::drop()
                    .table(OutputSymlink::Table)
                    .to_owned(),
            )
            .await?;
        manager
            .drop_table(
                sea_query::Table::drop()
                    .table(VisibleFile::Table)
                    .to_owned(),
            )
            .await?;
        manager
            .drop_table(sea_query::Table::drop().table(Job::Table).to_owned())
            .await?;
        Ok(())
    }
}

#[derive(DeriveIden)]
enum OutputDir {
    Table,
    Id,
    Path,
    Mode,
    JobId,
}

#[derive(DeriveIden)]
enum OutputSymlink {
    Table,
    Id,
    Path,
    Link,
    JobId,
}

#[derive(DeriveIden)]
enum OutputFile {
    Table,
    Id,
    Path,
    Mode,
    JobId,
    BlobId,
}

#[derive(DeriveIden)]
enum VisibleFile {
    Table,
    Id,
    Path,
    Hash,
    JobId,
}

// Only output jobs are ever added. We only index by Hash
// and the primary key. The hash must be unique has is the
// hash of all input content including visible files. We do
// not track the inputs that were actully read, only the
// visible files. This greatly simplifies lookup and allows
// for a fast lookup by hash alone.
#[derive(DeriveIden)]
pub enum Job {
    Table,
    Id,
    Hash,
    Cmd,
    Env,
    Cwd,
    Stdin,
    IsAtty,
    HiddenInfo,
    StdoutBlobId,
    StderrBlobId,
    Status,
    Runtime,
    Cputime,
    Memory,
    IBytes,
    OBytes,
}

#[derive(DeriveIden)]
enum Blob {
    Table,
    Id,
}
