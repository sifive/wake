use axum::{routing::post, Router};
use clap::Parser;
use migration::{Migrator, MigratorTrait};
use std::io::{Error, ErrorKind};
use std::sync::Arc;
use tracing;

use sea_orm::EntityTrait;
mod add_job;
mod api_key_check;
mod read_job;
mod types;

#[path = "../common/config.rs"]
mod config;

#[derive(Debug, Parser)]
struct ServerOptions {
    #[arg(help = "Specify a config override file", value_name = "CONFIG", long)]
    config_override: Option<String>,

    #[arg(help = "Show's the config and then exits", long)]
    show_config: bool,

    #[arg(
        help = "Specify an override for the bind address",
        value_name = "SERVER_IP[:SERVER_PORT]",
        long
    )]
    server_addr: Option<String>,

    #[arg(
        help = "Specify an override for the database url",
        value_name = "DATABASE_URL",
        long
    )]
    database_url: Option<String>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // setup a subscriber so that we always have logging
    let subscriber = tracing_subscriber::FmtSubscriber::new();
    tracing::subscriber::set_global_default(subscriber)?;

    // Parse our arguments
    let args = ServerOptions::parse();

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
            })
            .layer(axum::middleware::from_fn({
                let shared_state = state.clone();
                move |req, next| {
                    api_key_check::api_key_check_middleware(req, next, shared_state.clone())
                }
            })),
        )
        .route(
            "/job/matching",
            post({
                let shared_state = state.clone();
                move |body| read_job::read_job(body, shared_state)
            }),
        );

    // Start TTL Job Eviction
    let ttl_state = state.clone();
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(std::time::Duration::from_millis(1000));
        loop {
            interval.tick().await;
            println!("tick");

            let jobs: Vec<entity::job::Model> = entity::job::Entity::find()
                .all(ttl_state.as_ref())
                .await
                .unwrap();

            //            println!("{:?}", jobs[0]);
        }
    });

    // run it with hyper on localhost:3000
    axum::Server::bind(&config.server_addr.parse()?)
        .serve(app.into_make_service())
        .await?;
    Ok(())
}
