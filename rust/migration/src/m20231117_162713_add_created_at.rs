use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[macro_export]
macro_rules! static_for {
    ({ $( $x:expr ),* } as $var:pat_param in $body:block) => {
        {
            $(
                {
                    let $var = $x;
                    $body;
                }
            )*
        }
    };
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        static_for!({
          (Job::Table, Job::CreatedAt),
          (OutputDir::Table, OutputDir::CreatedAt),
          (OutputSymlink::Table, OutputSymlink::CreatedAt),
          (OutputFile::Table, OutputFile::CreatedAt),
          (VisibleFile::Table, VisibleFile::CreatedAt),
          (ApiKey::Table, ApiKey::CreatedAt),
          (JobUses::Table, JobUses::CreatedAt)
        } as (t, c) in {
            manager
                .alter_table(
                    Table::alter()
                        .table(t)
                        .add_column(
                            ColumnDef::new(c)
                                .timestamp()
                                .not_null()
                                .default(SimpleExpr::Keyword(Keyword::CurrentTimestamp))
                        )
                        .to_owned()
                ).await?
        });

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        static_for!({
          (Job::Table, Job::CreatedAt),
          (OutputDir::Table, OutputDir::CreatedAt),
          (OutputSymlink::Table, OutputSymlink::CreatedAt),
          (OutputFile::Table, OutputFile::CreatedAt),
          (VisibleFile::Table, VisibleFile::CreatedAt),
          (ApiKey::Table, ApiKey::CreatedAt),
          (JobUses::Table, JobUses::CreatedAt)
        } as (t, c) in {
            manager
                .alter_table(
                    Table::alter()
                        .table(t)
                        .drop_column(c)
                        .to_owned()
                ).await?
        });

        Ok(())
    }
}

#[derive(DeriveIden)]
pub enum Job {
    Table,
    CreatedAt,
}

#[derive(DeriveIden)]
enum OutputDir {
    Table,
    CreatedAt,
}

#[derive(DeriveIden)]
enum OutputSymlink {
    Table,
    CreatedAt,
}

#[derive(DeriveIden)]
enum OutputFile {
    Table,
    CreatedAt,
}

#[derive(DeriveIden)]
enum VisibleFile {
    Table,
    CreatedAt,
}

#[derive(DeriveIden)]
enum ApiKey {
    Table,
    CreatedAt,
}

#[derive(DeriveIden)]
enum JobUses {
    Table,
    CreatedAt,
}
