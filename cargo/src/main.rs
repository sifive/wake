// TODO: Setup a postgres hello world
// TODO: Setup a hyper hello world
// TODO: Commit that basic repo to wake
use entity::job;
use migration::{Migrator, MigratorTrait};
use sea_orm::{ActiveModelTrait, ActiveValue};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let connection = sea_orm::Database::connect("postgres://127.0.0.1/test").await?;
    Migrator::up(&connection, None).await?;
    let job = job::ActiveModel {
        cmd: ActiveValue::Set("cowsay".into()),
        env: ActiveValue::Set("PATH=/usr/bin:/bin".into()),
        id: ActiveValue::NotSet,
    };
    job.insert(&connection).await?;
    Ok(())
}
