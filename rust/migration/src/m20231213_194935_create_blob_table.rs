use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(BlobStore::Table)
                    .col(
                        ColumnDef::new(BlobStore::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(BlobStore::Name).string().not_null())
                    .to_owned(),
            )
            .await?;

        // Insert basic backing stores since they must always exist.
        let insert = Query::insert()
            .into_table(BlobStore::Table)
            .columns([BlobStore::Id, BlobStore::Name])
            .values_panic([1.into(), "local".into()])
            .values_panic([2.into(), "artifactory".into()])
            .values_panic([3.into(), "aws".into()])
            .values_panic([4.into(), "gcs".into()])
            .values_panic([5.into(), "azure".into()])
            .to_owned();

        manager.exec_stmt(insert).await?;

        manager
            .create_table(
                Table::create()
                    .table(Blob::Table)
                    .col(ColumnDef::new(Blob::Id).string().not_null().primary_key())
                    .col(ColumnDef::new(Blob::Store).integer().not_null())
                    .col(ColumnDef::new(Blob::WorkspacePath).string().not_null())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-store-blob_store")
                            .from_tbl(Blob::Table)
                            .from_col(Blob::Store)
                            .to_tbl(BlobStore::Table)
                            .to_col(BlobStore::Id)
                            // All blobs for a given store must be manually
                            // deleted before the blob store may be deleted.
                            .on_delete(ForeignKeyAction::Restrict),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .create_table(
                Table::create()
                    .table(JobBlob::Table)
                    .col(ColumnDef::new(JobBlob::JobId).integer().not_null())
                    .col(ColumnDef::new(JobBlob::BlobId).string().not_null())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-job_id-job")
                            .from_tbl(JobBlob::Table)
                            .from_col(JobBlob::JobId)
                            .to_tbl(Job::Table)
                            .to_col(Job::Id)
                            // If the associated job is deleted (likely via eviction)
                            // then the reference to the blob should be removed so the
                            // blob may later be cleaned up.
                            .on_delete(ForeignKeyAction::Cascade),
                    )
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-blob_id-blob")
                            .from_tbl(JobBlob::Table)
                            .from_col(JobBlob::BlobId)
                            .to_tbl(Blob::Table)
                            .to_col(Blob::Id)
                            // A blob must not be deleted if it is still referenced by
                            // a job. Restrict such deletions.
                            .on_delete(ForeignKeyAction::Restrict),
                    )
                    .primary_key(Index::create().col(JobBlob::JobId).col(JobBlob::BlobId))
                    .to_owned(),
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(JobBlob::Table).to_owned())
            .await?;

        manager
            .drop_table(Table::drop().table(Blob::Table).to_owned())
            .await?;

        manager
            .drop_table(Table::drop().table(BlobStore::Table).to_owned())
            .await?;

        Ok(())
    }
}

#[derive(DeriveIden)]
enum BlobStore {
    Table,
    Id,
    Name,
}

#[derive(DeriveIden)]
enum Blob {
    Table,
    Id,
    Store,
    WorkspacePath,
    // Job relation
}

#[derive(DeriveIden)]
enum Job {
    Table,
    Id,
}

#[derive(DeriveIden)]
enum JobBlob {
    Table,
    JobId,
    BlobId,
}
