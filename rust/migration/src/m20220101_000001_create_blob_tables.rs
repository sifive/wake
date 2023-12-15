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
                    .col(ColumnDef::new(BlobStore::Type).string().not_null())
                    .to_owned(),
            )
            .await?;

        // Insert basic backing store since it must always exist.
        let insert = Query::insert()
            .into_table(BlobStore::Table)
            .columns([BlobStore::Id, BlobStore::Type])
            .values_panic([1.into(), "LocalBlobStore".into()])
            .to_owned();

        manager.exec_stmt(insert).await?;

        manager
            .create_table(
                Table::create()
                    .table(Blob::Table)
                    .col(
                        ColumnDef::new(Blob::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(Blob::Key).string().not_null())
                    .col(ColumnDef::new(Blob::StoreId).integer().not_null())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-store_id-blob_store")
                            .from_tbl(Blob::Table)
                            .from_col(Blob::StoreId)
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
                    .table(LocalBlobStore::Table)
                    .col(
                        ColumnDef::new(LocalBlobStore::Id)
                            .integer()
                            .not_null()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(LocalBlobStore::Root).string().not_null())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-id-blob_store")
                            .from_tbl(LocalBlobStore::Table)
                            .from_col(LocalBlobStore::Id)
                            .to_tbl(BlobStore::Table)
                            .to_col(BlobStore::Id)
                            // All local blob stores for a given store must be
                            // manually deleted before the blob store may be deleted.
                            .on_delete(ForeignKeyAction::Restrict),
                    )
                    .to_owned(),
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(LocalBlobStore::Table).to_owned())
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
    Type,
}

#[derive(DeriveIden)]
enum Blob {
    Table,
    Id,
    Key,
    StoreId,
}

#[derive(DeriveIden)]
enum LocalBlobStore {
    Table,
    Id,
    Root,
}
