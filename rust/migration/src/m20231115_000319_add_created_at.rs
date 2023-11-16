use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .alter_table(
                Table::alter()
                    .table(Job::Table)
                    .add_column(
                        ColumnDef::new(Job::CreatedAt)
                            .timestamp()
                            .not_null()
                            .default(SimpleExpr::Keyword(Keyword::CurrentTimestamp))
                    )
                    .to_owned()
            ).await
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .alter_table(
                Table::alter()
                    .table(Job::Table)
                    .drop_column(Job::CreatedAt)
                    .to_owned()
            ).await
    }
}

#[derive(Iden)]
pub enum Job {
    Table,
    CreatedAt,
}
