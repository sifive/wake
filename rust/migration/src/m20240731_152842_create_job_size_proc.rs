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
                CREATE OR REPLACE PROCEDURE calculate_job_size(
                   job_lim int,
                   INOUT updated_count int
                )
                language plpgsql
                as $$
                BEGIN

                -- Run the query that find the jobs, calcs their sizes, and then updates the table
                WITH
                eligible_jobs as (
                    SELECT id, stdout_blob_id, stderr_blob_id
                    FROM job
                    WHERE size IS NULL
                    ORDER BY created_at
                    ASC
                    LIMIT job_lim
                ),
                job_blob_size as (
                    SELECT ej.id, SUM(COALESCE(b.size,0)) as size
                    FROM eligible_jobs ej
                    LEFT JOIN output_file o
                    ON ej.id = o.job_id
                    LEFT JOIN blob b
                    ON o.blob_id = b.id
                    GROUP BY ej.id
                ),
                full_size as (
                    SELECT
                        ej.id,
                        CAST(jb.size + stdout.size + stderr.size as BIGINT) as size
                    FROM eligible_jobs ej
                    INNER JOIN job_blob_size jb
                    ON ej.id = jb.id
                    INNER JOIN blob stdout
                    ON ej.stdout_blob_id = stdout.id
                    INNER JOIN blob stderr
                    ON ej.stderr_blob_id = stderr.id
                )
                UPDATE job j
                SET size = f.size
                FROM full_size f
                WHERE j.id = f.id;

                -- Grab the rows affected count
                GET DIAGNOSTICS updated_count = ROW_COUNT;

                END;
                $$;
                ",
            )
            .await?;
        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .get_connection()
            .execute_unprepared("DROP PROCEDURE IF EXISTS calculate_job_size(int, int)")
            .await?;
        Ok(())
    }
}
