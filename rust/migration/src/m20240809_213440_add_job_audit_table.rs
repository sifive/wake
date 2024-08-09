use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(JobAudit::Table)
                    .col(
                        ColumnDef::new(JobAudit::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(JobAudit::Hash).string().not_null())
                    .col(ColumnDef::new(JobAudit::Event).string().not_null())
                    .col(
                        ColumnDef::new(JobAudit::CreatedAt)
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
            .drop_table(Table::drop().table(JobAudit::Table).to_owned())
            .await
    }
}

#[derive(DeriveIden)]
enum JobAudit {
    Table,
    Id,
    Hash,
    CreatedAt,
    Event,
}
