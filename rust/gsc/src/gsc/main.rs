use axum::{routing::post, Router};
use gumdrop::Options;
use migration::{Migrator, MigratorTrait};
use std::io::{Error, ErrorKind};
use std::sync::Arc;
use tracing;

mod add_job;
mod read_job;
mod types;

#[path = "../common/config.rs"]
mod config;

#[derive(Debug, Options)]
struct ServerOptions {
    #[options(help_flag, help = "print help message")]
    help: bool,

    #[options(help = "Specify a config override file", meta = "CONFIG", no_short)]
    config_override: Option<String>,

    #[options(help = "Show's the config and then exits", no_short)]
    show_config: bool,

    #[options(
        help = "Specify an override for the bind address",
        meta = "SERVER_IP[:SERVER_PORT]",
        no_short
    )]
    server_addr: Option<String>,

    #[options(
        help = "Specify an override for the database url",
        meta = "DATABASE_URL",
        no_short
    )]
    database_url: Option<String>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // setup a subscriber so that we always have logging
    let subscriber = tracing_subscriber::FmtSubscriber::new();
    tracing::subscriber::set_global_default(subscriber)?;

    // Parse our arguments
    let args = ServerOptions::parse_args_default_or_exit();

    // Get our configuration
    let config = config::GSCConfig::new(config::GSCConfigOverride {
        config_override: args.config_override,
        server_addr: args.server_addr,
        database_url: args.database_url,
    })?;

    if args.show_config {
        println!("{}", serde_json::to_string(&config).unwrap());
        return Ok(());
    }

    // connect to our db
    let connection = sea_orm::Database::connect(&config.database_url).await?;
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
    axum::Server::bind(&config.server_addr.parse()?)
        .serve(app.into_make_service())
        .await?;
    Ok(())
}
