use axum::{
    extract::{DefaultBodyLimit, Multipart},
    routing::{get, post},
    Router,
};
use clap::Parser;
use migration::{Migrator, MigratorTrait};
use std::io::{Error, ErrorKind};
use std::sync::Arc;
use tracing;

use sea_orm::{
    ActiveModelTrait, ActiveValue::*, ColumnTrait, Database, DatabaseConnection, DeleteResult,
    EntityTrait, QueryFilter,
};

use chrono::{Duration, Utc};

mod add_job;
mod api_key_check;
mod blob;
mod read_job;
mod types;

#[path = "../common/config.rs"]
mod config;

#[derive(Debug, Parser)]
struct ServerOptions {
    #[arg(help = "Specify a config override file", value_name = "CONFIG", long)]
    config_override: Option<String>,

    #[arg(help = "Shows the config and then exits", long)]
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

    #[arg(help = "Launches the cache without an external database", long)]
    standalone: bool,

    #[arg(
        help = "Specify an absolute path for a local directory based store",
        value_name = "LOCAL_STORE",
        long
    )]
    local_store: Option<String>,
}

fn create_router(conn: Arc<DatabaseConnection>, config: Arc<config::RSCConfig>) -> Router {
    // If we can't create a store, just exit. The config is wrong and must be rectified.
    let root = config.local_store.clone().unwrap();
    let store = blob::LocalBlobStore { root };
    Router::new()
        .route(
            "/job",
            post({
                let conn = conn.clone();
                move |body| add_job::add_job(body, conn)
            })
            .layer(axum::middleware::from_fn({
                let conn = conn.clone();
                move |req, next| api_key_check::api_key_check_middleware(req, next, conn.clone())
            })),
        )
        .route(
            "/job/matching",
            post({
                let conn = conn.clone();
                move |body| read_job::read_job(body, conn)
            }),
        )
        .route(
            "/blob",
            get({
                let config = config.clone();
                move || blob::get_upload_url(config.server_addr.clone())
            }),
        )
        .route(
            "/blob",
            post({
                let conn = conn.clone();
                let store = store.clone();
                move |multipart: Multipart| blob::create_blob(multipart, conn, store)
            })
            .layer(DefaultBodyLimit::disable()),
        )
}

async fn create_standalone_db() -> Result<DatabaseConnection, sea_orm::DbErr> {
    let db = Database::connect("sqlite::memory:").await?;
    Migrator::up(&db, None).await?;
    Ok(db)
}

async fn create_remote_db(
    config: &config::RSCConfig,
) -> Result<DatabaseConnection, Box<dyn std::error::Error>> {
    let connection = Database::connect(&config.database_url).await?;
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
        Err(err)?
    }

    Ok(connection)
}

async fn create_insecure_api_key(
    db: &DatabaseConnection,
) -> Result<String, Box<dyn std::error::Error>> {
    let active_key = entity::api_key::ActiveModel {
        id: NotSet,
        created_at: NotSet,
        key: Set("InsecureKey".into()),
        desc: Set("Generated Insecure Key".into()),
    };

    let inserted_key = active_key.insert(db).await?;

    Ok(inserted_key.key)
}

async fn connect_to_database(
    config: &config::RSCConfig,
) -> Result<DatabaseConnection, Box<dyn std::error::Error>> {
    if config.standalone {
        tracing::warn!("Launching rsc in standalone mode, data will not persist.");
        let db = create_standalone_db().await?;
        let key = create_insecure_api_key(&db).await?;
        tracing::info!(key, "Created insecure api key.");

        return Ok(db);
    }

    create_remote_db(config).await
}

fn launch_eviction(conn: Arc<DatabaseConnection>, tick_interval: u64, deadline: i64) {
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(std::time::Duration::from_secs(tick_interval));
        loop {
            interval.tick().await;

            let deadline = (Utc::now() - Duration::seconds(deadline)).naive_utc();

            let res: DeleteResult = entity::job::Entity::delete_many()
                .filter(entity::job::Column::CreatedAt.lte(deadline))
                .exec(conn.as_ref())
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
        standalone: if args.standalone {
            Some(args.standalone)
        } else {
            None
        },
        local_store: args.local_store,
    })?;
    let config = Arc::new(config);

    if args.show_config {
        println!("{}", serde_json::to_string(&config).unwrap());
        return Ok(());
    }

    // connect to the db
    let connection = connect_to_database(&config).await?;
    let connection = Arc::new(connection);

    // Launch the eviction thread
    launch_eviction(connection.clone(), 60 * 10, 60 * 60 * 24 * 7);

    // Launch the server
    let router = create_router(connection.clone(), config.clone());
    axum::Server::bind(&config.server_addr.parse()?)
        .serve(router.into_make_service())
        .await?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use sea_orm::{ActiveModelTrait, PaginatorTrait};
    use std::sync::Arc;

    use axum::{
        body::Body,
        http::{self, Request, StatusCode},
    };
    use serde_json::{json, Value};
    use tower::Service;

    fn create_config() -> Result<config::RSCConfig, Box<dyn std::error::Error>> {
        Ok(config::RSCConfig::new(config::RSCConfigOverride {
            config_override: Some("".into()),
            server_addr: Some("test:0000".into()),
            database_url: Some("".into()),
            standalone: Some(true),
            local_store: Some("".into()),
        })?)
    }

    async fn create_fake_blob(db: &DatabaseConnection) -> Result<i32, Box<dyn std::error::Error>> {
        let active_key = entity::blob::ActiveModel {
            id: NotSet,
            created_at: Set((Utc::now() - Duration::days(5)).naive_utc()),
            key: Set("InsecureKey".into()),
            store_id: Set(1),
        };

        let inserted_key = active_key.insert(db).await?;

        Ok(inserted_key.id)
    }

    #[tokio::test]
    async fn nominal() {
        let db = create_standalone_db().await.unwrap();
        let api_key = create_insecure_api_key(&db).await.unwrap();
        let blob_id = create_fake_blob(&db).await.unwrap();
        let config = create_config().unwrap();
        let mut router = create_router(Arc::new(db), Arc::new(config));

        // Non-existant route should 404
        let res = router
            .call(Request::builder().uri("/").body(Body::empty()).unwrap())
            .await
            .unwrap();
        assert_eq!(res.status(), StatusCode::NOT_FOUND);

        // Protected route without auth should 401
        let res = router
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
        let res = router
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
        let res = router
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
                            "stdout_blob_id": blob_id,
                            "stderr_blob_id": blob_id,
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
        let res = router
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
        let res = router
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
                "stdout_blob_id": blob_id,
                "stderr_blob_id": blob_id,
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
        let db = create_standalone_db().await.unwrap();
        let blob_id = create_fake_blob(&db).await.unwrap();
        let conn = Arc::new(db);

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
            stdout_blob_id: Set(blob_id),
            stderr_blob_id: Set(blob_id),
            status: Set(0),
            runtime: Set(1.0),
            cputime: Set(1.0),
            memory: Set(1000),
            i_bytes: Set(100000),
            o_bytes: Set(1000),
        };

        insert_job.save(conn.clone().as_ref()).await.unwrap();

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
            stdout_blob_id: Set(blob_id),
            stderr_blob_id: Set(blob_id),
            status: Set(0),
            runtime: Set(1.0),
            cputime: Set(1.0),
            memory: Set(1000),
            i_bytes: Set(100000),
            o_bytes: Set(1000),
        };

        insert_job.save(conn.clone().as_ref()).await.unwrap();

        let count = entity::job::Entity::find()
            .count(conn.clone().as_ref())
            .await
            .unwrap();
        assert_eq!(count, 2);

        // Setup eviction for jobs older than 3 days ticking every second
        launch_eviction(conn.clone(), 1, 60 * 60 * 24 * 3);
        tokio::time::sleep(tokio::time::Duration::from_millis(2000)).await;

        let count = entity::job::Entity::find()
            .count(conn.clone().as_ref())
            .await
            .unwrap();
        assert_eq!(count, 1);
    }
}
