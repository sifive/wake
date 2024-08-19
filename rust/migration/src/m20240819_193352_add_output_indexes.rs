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
                CREATE INDEX IF NOT EXISTS output_file_job_id_idx
                ON output_file(job_id)
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                CREATE INDEX IF NOT EXISTS output_dir_job_id_idx
                ON output_dir(job_id)
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                CREATE INDEX IF NOT EXISTS output_symlink_job_id_idx
                ON output_symlink(job_id)
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
                DROP INDEX IF EXISTS output_file_job_id_idx
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                DROP INDEX IF EXISTS output_dir_job_id_idx
                ",
            )
            .await?;

        manager
            .get_connection()
            .execute_unprepared(
                "
                DROP INDEX IF EXISTS output_symlink_job_id_idx
                ",
            )
            .await?;

        Ok(())
    }
}
