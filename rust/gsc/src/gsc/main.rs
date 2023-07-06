use axum::{routing::post, Router};
use migration::{Migrator, MigratorTrait};
use std::sync::Arc;
use tracing;

mod add_job;
mod read_job;
mod types;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // setup a subscriber so that we always have logging
    let subscriber = tracing_subscriber::FmtSubscriber::new();
    tracing::subscriber::set_global_default(subscriber)?;

    // connect to our db
    let connection = sea_orm::Database::connect("postgres://127.0.0.1/test").await?;
    let pending_migrations = Migrator::get_pending_migrations(&connection).await?;
    if pending_migrations.len() != 0 {
        let err = Error::new(
            ErrorKind::Other,
            format!(
                "This gsc version expects {:?} additional migrations to be applied",
                pending_migrations.len()
            ),
        );
        tracing::error! {%err, "unperformed migrations, please apply these migrations before starting gsc"};
        Err(err)?;
    }
    //Migrator::up(&connection, None).await?;
    let state = Arc::new(connection);

    // build our application with a single route
    let app = Router::new()
        .route(
            "/job",
            post({
                let shared_state = state.clone();
                move |body| add_job::add_job(body, shared_state)
            }),
        )
        .route(
            "/job/matching",
            post({
                let shared_state = state.clone();
                move |body| read_job::read_job(body, shared_state)
            }),
        );

    // run it with hyper on localhost:3000
    axum::Server::bind(&"127.0.0.1:3000".parse()?)
        .serve(app.into_make_service())
        .await?;
    Ok(())
}
