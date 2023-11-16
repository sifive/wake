use sea_orm_migration::prelude::*;

use crate::m20220101_000001_create_table::Job;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(JobUses::Table)
                    .col(
                        ColumnDef::new(JobUses::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(JobUses::Time).timestamp().not_null())
                    .col(ColumnDef::new(JobUses::JobId).integer().not_null())
                    .foreign_key(
                        ForeignKeyCreateStatement::new()
                            .name("fk-job-use-job")
                            .from_tbl(JobUses::Table)
                            .from_col(JobUses::JobId)
                            .to_tbl(Job::Table)
                            .to_col(Job::Id)
                            .on_delete(ForeignKeyAction::Cascade),
                    )
                    .to_owned(),
            )
            .await
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(JobUses::Table).to_owned())
            .await
    }
}

/// Learn more at https://docs.rs/sea-query#iden
#[derive(DeriveIden)]
enum JobUses {
    Table,
    Id,
    Time,
    JobId,
}
