use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(JobHistory::Table)
                    .col(
                        ColumnDef::new(JobHistory::Hash)
                            .string()
                            .not_null()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(JobHistory::Hits).integer().not_null())
                    .col(ColumnDef::new(JobHistory::Misses).integer().not_null())
                    .col(ColumnDef::new(JobHistory::Evictions).integer().not_null())
                    .col(
                        ColumnDef::new(JobHistory::CreatedAt)
                            .timestamp()
                            .not_null()
                            .default(SimpleExpr::Keyword(Keyword::CurrentTimestamp)),
                    )
                    .col(
                        ColumnDef::new(JobHistory::UpdatedAt)
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
            .drop_table(Table::drop().table(JobHistory::Table).to_owned())
            .await
    }
}

#[derive(DeriveIden)]
enum JobHistory {
    Table,
    Hash,
    Hits,
    Misses,
    Evictions,
    CreatedAt,
    UpdatedAt,
}
