use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .rename_table(
                Table::rename()
                    .table(JobUses::Table, JobUse::Table)
                    .to_owned(),
            )
            .await
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .rename_table(
                Table::rename()
                    .table(JobUse::Table, JobUses::Table)
                    .to_owned(),
            )
            .await
    }
}

#[derive(DeriveIden)]
enum JobUses {
    Table,
}

#[derive(DeriveIden)]
enum JobUse {
    Table,
}
