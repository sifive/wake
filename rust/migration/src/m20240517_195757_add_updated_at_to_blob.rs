use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .alter_table(
                Table::alter()
                    .table(Blob::Table)
                    .add_column(
                        ColumnDef::new(Blob::UpdatedAt)
                            .timestamp()
                            .not_null()
                            .default(SimpleExpr::Keyword(Keyword::CurrentTimestamp)),
                    )
                    .to_owned(),
            )
            .await
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .alter_table(
                Table::alter()
                    .table(Blob::Table)
                    .drop_column(Blob::UpdatedAt)
                    .to_owned(),
            )
            .await
    }
}

#[derive(DeriveIden)]
enum Blob {
    Table,
    UpdatedAt,
}
