use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .get_connection()
            .execute_unprepared(
                "
                CREATE INDEX IF NOT EXISTS blob_updated_at_idx
                ON blob(updated_at)
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                CREATE INDEX IF NOT EXISTS job_created_at_idx
                ON job(created_at)
                ",
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .get_connection()
            .execute_unprepared(
                "
                DROP INDEX IF EXISTS job_created_at_idx
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                DROP INDEX IF EXISTS blob_updated_at_idx
                ",
            )
            .await?;

        Ok(())
    }
}
