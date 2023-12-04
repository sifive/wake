use axum::{routing::post, Router};
use clap::Parser;
use migration::{Migrator, MigratorTrait};
use std::io::{Error, ErrorKind};
use std::sync::Arc;
use tracing;

use sea_orm::{ColumnTrait, DeleteResult, EntityTrait, QueryFilter};

use chrono::{Duration, Utc};

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

fn make_app(state: Arc<sea_orm::DatabaseConnection>) -> Router {
    Router::new()
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
        )
}

fn launch_eviction(state: Arc<sea_orm::DatabaseConnection>, tick_interval: u64, deadline: i64) {
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(std::time::Duration::from_secs(tick_interval));
        loop {
            interval.tick().await;

            let deadline = (Utc::now() - Duration::seconds(deadline)).naive_utc();

            let res: DeleteResult = entity::job::Entity::delete_many()
                .filter(entity::job::Column::CreatedAt.lte(deadline))
                .exec(state.as_ref())
                .await
                .unwrap();

            tracing::info!(%res.rows_affected, "Performed TTL eviction tick.");
        }
    });
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // setup a subscriber for logging
    let subscriber = tracing_subscriber::FmtSubscriber::new();
    tracing::subscriber::set_global_default(subscriber)?;

    // Parse the arguments
    let args = ServerOptions::parse();

    // Get the configuration
    let config = config::RSCConfig::new(config::RSCConfigOverride {
        config_override: args.config_override,
        server_addr: args.server_addr,
        database_url: args.database_url,
    })?;

    if args.show_config {
        println!("{}", serde_json::to_string(&config).unwrap());
        return Ok(());
    }

    // connect to the db
    let connection = sea_orm::Database::connect(&config.database_url).await?;
    let pending_migrations = Migrator::get_pending_migrations(&connection).await?;
    if pending_migrations.len() != 0 {
        let err = Error::new(
            ErrorKind::Other,
            format!(
                "This rsc version expects {:?} additional migrations to be applied",
                pending_migrations.len()
            ),
        );
        tracing::error! {%err, "unperformed migrations, please apply these migrations before starting rsc"};
        Err(err)?;
    }
    let state = Arc::new(connection);

    // Launch the eviction thread
    launch_eviction(state.clone(), 60 * 10, 60 * 60 * 24 * 7);

    // Launch the server
    let app = make_app(state.clone());
    axum::Server::bind(&config.server_addr.parse()?)
        .serve(app.into_make_service())
        .await?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use data_encoding::BASE64;
    use migration::{Migrator, MigratorTrait};
    use sea_orm::{ActiveModelTrait, ActiveValue::*, DatabaseConnection, DbErr, PaginatorTrait};
    use std::sync::Arc;

    use axum::{
        body::Body,
        http::{self, Request, StatusCode},
    };
    use serde_json::{json, Value};
    use tower::Service;

    async fn make_db() -> Result<DatabaseConnection, DbErr> {
        let db = sea_orm::Database::connect("sqlite::memory:").await?;
        Migrator::up(&db, None).await?;
        let pending_migrations = Migrator::get_pending_migrations(&db).await?;
        assert_eq!(0, pending_migrations.len());
        Ok(db)
    }

    async fn create_api_key(db: &DatabaseConnection) -> Result<String, Box<dyn std::error::Error>> {
        let key = BASE64.encode(b"Test Api Key");

        let active_key = entity::api_key::ActiveModel {
            id: NotSet,
            created_at: NotSet,
            key: Set(key.clone()),
            desc: Set("Generated Test Key".into()),
        };

        let inserted_key = active_key.insert(db).await?;

        Ok(inserted_key.key)
    }

    #[tokio::test]
    async fn nominal() {
        let db = make_db().await.unwrap();
        let api_key = create_api_key(&db).await.unwrap();
        let mut app = make_app(Arc::new(db));

        // Non-existant route should 404
        let res = app
            .call(Request::builder().uri("/").body(Body::empty()).unwrap())
            .await
            .unwrap();
        assert_eq!(res.status(), StatusCode::NOT_FOUND);

        // Protected route without auth should 401
        let res = app
            .call(
                Request::builder()
                    .uri("/job")
                    .method(http::Method::POST)
                    .header("Content-Type", "application/json")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::UNAUTHORIZED);

        // Missing/malformed body should 400
        let res = app
            .call(
                Request::builder()
                    .uri("/job")
                    .method(http::Method::POST)
                    .header("Content-Type", "application/json")
                    .header("Authorization", api_key.clone())
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::BAD_REQUEST);

        // Correctly inserting a job should 200
        let res = app
            .call(
                Request::builder()
                    .uri("/job")
                    .method(http::Method::POST)
                    .header("Content-Type", "application/json")
                    .header("Authorization", api_key)
                    .body(Body::from(
                        serde_json::to_vec(&json!({
                            "cmd": "blarg",
                            "env":"PATH=/usr/bin",
                            "cwd":"/workspace",
                            "stdin":"",
                            "is_atty": false,
                            "hidden_info":"",
                            "visible_files": [],
                            "output_dirs": [],
                            "output_symlinks": [],
                            "output_files":[],
                            "stdout":"this is a test",
                            "stderr":"this is a very long string for a test",
                            "status": 0,
                            "runtime":1.0,
                            "cputime":1.0,
                            "memory": 1000,
                            "ibytes":100000,
                            "obytes":1000
                        }))
                        .unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::OK);

        // Non-matching job should 200 with expected body
        let res = app
            .call(
                Request::builder()
                    .uri("/job/matching")
                    .method(http::Method::POST)
                    .header("Content-Type", "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&json!({
                            "cmd": "blrg",
                            "env":"PATH=/usr/bin",
                            "cwd":"/workspace",
                            "stdin":"",
                            "is_atty": false,
                            "hidden_info":"",
                            "visible_files": []
                        }))
                        .unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::OK);

        let body = hyper::body::to_bytes(res).await.unwrap();
        let body: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(body, json!({ "type": "NoMatch" }));

        // Matching job should 200 with expected body
        let res = app
            .call(
                Request::builder()
                    .uri("/job/matching")
                    .method(http::Method::POST)
                    .header("Content-Type", "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&json!({
                            "cmd": "blarg",
                            "env":"PATH=/usr/bin",
                            "cwd":"/workspace",
                            "stdin":"",
                            "is_atty": false,
                            "hidden_info":"",
                            "visible_files": []
                        }))
                        .unwrap(),
                    ))
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::OK);

        let body = hyper::body::to_bytes(res).await.unwrap();
        let body: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(
            body,
            json!({
                "type": "Match",
                "output_dirs": [],
                "output_symlinks": [],
                "output_files":[],
                "stdout":[116, 104, 105, 115, 32, 105, 115, 32, 97, 32, 116, 101, 115, 116],
                "stderr":[116, 104, 105, 115, 32, 105, 115, 32, 97, 32, 118, 101, 114, 121, 32, 108, 111, 110, 103, 32, 115, 116, 114, 105, 110, 103, 32, 102, 111, 114, 32, 97, 32, 116, 101, 115, 116],
                "status": 0,
                "runtime":1.0,
                "cputime":1.0,
                "memory": 1000,
                "ibytes":100000,
                "obytes":1000
            })
        );
    }

    #[tokio::test]
    async fn ttl_eviction() {
        let db = make_db().await.unwrap();
        let state = Arc::new(db);

        let hash: [u8; 32] = [
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0,
        ];

        // Create a job that is 5 days old
        let insert_job = entity::job::ActiveModel {
            id: NotSet,
            created_at: Set((Utc::now() - Duration::days(5)).naive_utc()),
            hash: Set(hash.into()),
            cmd: Set("blarg".into()),
            env: Set("PATH=/usr/bin".as_bytes().into()),
            cwd: Set("/workspace".into()),
            stdin: Set("".into()),
            is_atty: Set(false),
            hidden_info: Set("".into()),
            stdout: Set("This is a test".into()),
            stderr: Set("This is a very long string for a test".into()),
            status: Set(0),
            runtime: Set(1.0),
            cputime: Set(1.0),
            memory: Set(1000),
            i_bytes: Set(100000),
            o_bytes: Set(1000),
        };

        insert_job.save(state.clone().as_ref()).await.unwrap();

        let hash: [u8; 32] = [
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 1,
        ];

        // Create a job that is 1 day old
        let insert_job = entity::job::ActiveModel {
            id: NotSet,
            created_at: Set((Utc::now() - Duration::days(1)).naive_utc()),
            hash: Set(hash.into()),
            cmd: Set("blarg2".into()),
            env: Set("PATH=/usr/bin".as_bytes().into()),
            cwd: Set("/workspace".into()),
            stdin: Set("".into()),
            is_atty: Set(false),
            hidden_info: Set("".into()),
            stdout: Set("This is a test".into()),
            stderr: Set("This is a very long string for a test".into()),
            status: Set(0),
            runtime: Set(1.0),
            cputime: Set(1.0),
            memory: Set(1000),
            i_bytes: Set(100000),
            o_bytes: Set(1000),
        };

        insert_job.save(state.clone().as_ref()).await.unwrap();

        let count = entity::job::Entity::find()
            .count(state.clone().as_ref())
            .await
            .unwrap();
        assert_eq!(count, 2);

        // Setup eviction for jobs older than 3 days ticking every second
        launch_eviction(state.clone(), 1, 60 * 60 * 24 * 3);
        tokio::time::sleep(tokio::time::Duration::from_millis(2000)).await;

        let count = entity::job::Entity::find()
            .count(state.clone().as_ref())
            .await
            .unwrap();
        assert_eq!(count, 1);
    }
}
