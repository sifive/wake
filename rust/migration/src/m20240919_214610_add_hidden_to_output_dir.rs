use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .alter_table(
                Table::alter()
                    .table(OutputDir::Table)
                    .add_column(ColumnDef::new(OutputDir::Hidden).boolean().not_null().default(false))
                    .to_owned(),
            )
            .await
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .alter_table(
                Table::alter()
                    .table(OutputDir::Table)
                    .drop_column(OutputDir::Hidden)
                    .to_owned(),
            )
            .await
    }
}

#[derive(DeriveIden)]
enum OutputDir {
    Table,
    Hidden,
}

