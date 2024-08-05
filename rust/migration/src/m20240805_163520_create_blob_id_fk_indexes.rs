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
                CREATE INDEX IF NOT EXISTS output_file_blob_id_idx
                ON output_file(blob_id)
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                CREATE INDEX IF NOT EXISTS job_stdout_blob_id_idx
                ON job(stdout_blob_id)
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                CREATE INDEX IF NOT EXISTS job_stderr_blob_id_idx
                ON job(stderr_blob_id)
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
                DROP INDEX IF EXISTS output_file_blob_id_idx
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                DROP INDEX IF EXISTS job_stdout_blob_id_idx
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                DROP INDEX IF EXISTS job_stderr_blob_id_idx
                ",
            )
            .await?;

        Ok(())
    }
}
