use axum::{
    extract::{DefaultBodyLimit, Multipart},
    routing::{get, post},
    Router,
};
use chrono::Utc;
use clap::Parser;
use data_encoding::HEXLOWER;
use migration::{Migrator, MigratorTrait};
use rand_core::{OsRng, RngCore};
use rlimit::Resource;
use rsc::{config, database};
use sea_orm::{
    prelude::Uuid, ActiveModelTrait, ActiveValue::*, ConnectOptions, ConnectionTrait, Database,
    DatabaseConnection,
};
use std::collections::HashMap;
use std::io::{Error, ErrorKind};
use std::sync::Arc;
use std::time::Duration;
use tracing;

mod add_job;
mod api_key_check;
mod blob;
mod blob_store_impls;
mod read_job;
mod types;

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
}

// Maps databse store uuid -> dyn blob::DebugBlobStore
// that represent said store.
async fn activate_stores(
    conn: Arc<DatabaseConnection>,
) -> HashMap<Uuid, Arc<dyn blob::DebugBlobStore + Sync + Send>> {
    let mut active_stores: HashMap<Uuid, Arc<dyn blob::DebugBlobStore + Sync + Send>> =
        HashMap::new();

    // --- Activate Test Blob Stores  ---
    let test_stores = match database::read_test_blob_stores(conn.as_ref()).await {
        Ok(stores) => stores,
        Err(err) => {
            tracing::warn!(%err, "Failed to read test stores from database");
            Vec::new()
        }
    };

    for store in test_stores.into_iter() {
        active_stores.insert(
            store.id,
            Arc::new(blob_store_impls::TestBlobStore { id: store.id }),
        );
    }

    // --- Activate Local Blob Stores ---
    let stores = match database::read_local_blob_stores(conn.as_ref()).await {
        Ok(stores) => stores,
        Err(err) => {
            tracing::warn!(%err, "Failed to read local stores from database");
            Vec::new()
        }
    };

    for store in stores.into_iter() {
        active_stores.insert(
            store.id,
            Arc::new(blob_store_impls::LocalBlobStore {
                id: store.id,
                root: store.root,
            }),
        );
    }

    // ---    Activate DBOnly Store   ---
    let dbonly_store = match database::read_dbonly_blob_store(conn.as_ref()).await {
        Ok(Some(store)) => store,
        Ok(None) => {
            panic!("Database is not configured with a DbOnly store. Please bootstrap the db")
        }
        Err(_) => panic!("Failed to load DbOnly store from database. Unable to continue"),
    };
    active_stores.insert(
        dbonly_store.id,
        Arc::new(blob_store_impls::DbOnlyBlobStore {
            id: dbonly_store.id,
        }),
    );

    return active_stores;
}

#[derive(serde::Deserialize)]
struct VersionCheck {
    version: String,
}

async fn check_version(check: axum::extract::Query<VersionCheck>) -> axum::http::StatusCode {
    // During development, declare all version as compatible
    if !check.version.starts_with("sifive/wake/") {
        return axum::http::StatusCode::FORBIDDEN;
    }

    return axum::http::StatusCode::OK;
}

fn create_router(
    conn: Arc<DatabaseConnection>,
    config: Arc<config::RSCConfig>,
    blob_stores: &HashMap<Uuid, Arc<dyn blob::DebugBlobStore + Sync + Send>>,
) -> Router {
    // If we can't create a store, just exit. The config is wrong and must be rectified.
    let Some(active_store_uuid) = config.active_store.clone() else {
        panic!("Active store uuid not set in configuration");
    };

    let Ok(active_store_uuid) = Uuid::parse_str(&active_store_uuid) else {
        panic!("Failed to parse provided active store into uuid");
    };

    let Some(active_store) = blob_stores.get(&active_store_uuid).clone() else {
        panic!("UUID for active store not in database");
    };

    let dbonly_uuid = database::read_dbonly_blob_store_id();
    let Some(dbonly_store) = blob_stores.get(&dbonly_uuid).clone() else {
        panic!("UUID for db store not in database");
    };

    Router::new()
        // Authorized Routes
        .route(
            "/job",
            post({
                let conn = conn.clone();
                move |body| add_job::add_job(body, conn)
            })
            .layer(DefaultBodyLimit::disable())
            .layer(axum::middleware::from_fn({
                let conn = conn.clone();
                move |req, next| api_key_check::api_key_check_middleware(req, next, conn.clone())
            })),
        )
        .route(
            "/blob",
            post({
                let conn = conn.clone();
                let active = active_store.clone();
                let dbonly = dbonly_store.clone();

                move |multipart: Multipart| blob::create_blob(multipart, conn, active, dbonly)
            })
            .layer(DefaultBodyLimit::disable())
            .layer(axum::middleware::from_fn({
                let conn = conn.clone();
                move |req, next| api_key_check::api_key_check_middleware(req, next, conn.clone())
            })),
        )
        .route(
            "/auth/check",
            post(axum::http::StatusCode::OK).layer(axum::middleware::from_fn({
                let conn = conn.clone();
                move |req, next| api_key_check::api_key_check_middleware(req, next, conn.clone())
            })),
        )
        // Unauthorized Routes
        .route(
            "/job/matching",
            post({
                let conn = conn.clone();
                let blob_stores = blob_stores.clone();
                move |body| read_job::read_job(body, conn, blob_stores)
            })
            .layer(DefaultBodyLimit::disable()),
        )
        .route(
            "/blob",
            get({
                let config = config.clone();
                move || blob::get_upload_url(config.server_addr.clone())
            }),
        )
        .route("/version/check", get(check_version))
}

async fn create_standalone_db() -> Result<DatabaseConnection, sea_orm::DbErr> {
    let shim_db = Database::connect("postgres://127.0.0.1/shim").await?;
    let mut buf = [0u8; 24];
    OsRng.fill_bytes(&mut buf);
    let rng_str = HEXLOWER.encode(&buf);
    let db = format!("db_{}", rng_str);
    shim_db
        .execute_unprepared(&format!("CREATE DATABASE {}", db))
        .await?;
    drop(shim_db);
    let db = Database::connect(format!("postgres://127.0.0.1/{}", db)).await?;
    Migrator::up(&db, None).await?;
    Ok(db)
}

async fn create_remote_db(
    config: &config::RSCConfig,
) -> Result<DatabaseConnection, Box<dyn std::error::Error>> {
    let mut opt = ConnectOptions::new(&config.database_url);
    opt.sqlx_logging_level(tracing::log::LevelFilter::Debug);
    let connection = Database::connect(opt).await?;
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

fn launch_job_eviction(conn: Arc<DatabaseConnection>, tick_interval: u64, ttl: u64) {
    tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_secs(tick_interval));
        loop {
            interval.tick().await;
            tracing::info!("Job TTL eviction tick");

            let ttl = (Utc::now() - Duration::from_secs(ttl)).naive_utc();

            match database::evict_jobs_ttl(conn.clone(), ttl).await {
                Ok(res) => tracing::info!(%res, "Deleted jobs from database"),
                Err(err) => tracing::error!(%err, "Failed to delete jobs for eviction"),
            };
        }
    });
}

fn launch_blob_eviction(
    conn: Arc<DatabaseConnection>,
    tick_interval: u64,
    setup_ttl: u64,
    blob_stores: HashMap<Uuid, Arc<dyn blob::DebugBlobStore + Sync + Send>>,
) {
    // TODO: This should probably be a transaction so that a job can't add a new reference to a
    // blob as we are deleting it.

    tokio::spawn(async move {
        let mut interval = tokio::time::interval(Duration::from_secs(tick_interval));
        loop {
            interval.tick().await;
            tracing::info!("Blob TTL eviction tick");

            // Blobs must be at least this old to be considered for eviction.
            // This gives clients time to reference a blob before it gets evicted.
            let setup_ttl = (Utc::now() - Duration::from_secs(setup_ttl)).naive_utc();

            let blobs = match database::read_unreferenced_blobs(conn.as_ref(), setup_ttl).await {
                Ok(b) => b,
                Err(err) => {
                    tracing::error!(%err, "Failed to fetch blobs for eviction");
                    continue; // Try again on the next tick
                }
            };

            let blob_ids: Vec<Uuid> = blobs.iter().map(|blob| blob.id).collect();
            let eligible = blob_ids.len();

            tracing::info!(%eligible, "At least N blobs eligible for eviction");

            // Delete blobs from database
            match database::delete_blobs_by_ids(conn.as_ref(), blob_ids).await {
                Ok(deleted) => tracing::info!(%deleted, "Deleted blobs from database"),
                Err(err) => {
                    tracing::error!(%err, "Failed to delete blobs from db for eviction");
                    continue; // Try again on the next tick
                }
            };

            tracing::info!("Spawning blob deletion from stores");

            // Delete blobs from blob store
            for blob in blobs {
                let store = match blob_stores.get(&blob.store_id) {
                    Some(s) => s.clone(),
                    None => {
                        let blob = blob.clone();
                        tracing::info!(%blob.id, %blob.store_id, %blob.key, "Blob has been orphaned!");
                        tracing::error!(%blob.store_id, "Blob's store id missing from activated stores");
                        continue;
                    }
                };

                tokio::spawn(async move {
                    store.delete_key(blob.key.clone()).await.unwrap_or_else(|err| {
                        let blob = blob.clone();
                        tracing::info!(%blob.id, %blob.store_id, %blob.key, "Blob has been orphaned!");
                        tracing::error!(%err, "Failed to delete blob from store for eviction. See above for blob info");
                    });
                });
            }
        }
    });
}

fn request_max_fileno_limit() {
    let Ok((current, max)) = Resource::NOFILE.get() else {
        tracing::warn!("Unable to discover fileno limits. Using default");
        return;
    };

    tracing::info!(%current, %max, "Discovered fileno limits");

    let Ok(new_limit) = rlimit::increase_nofile_limit(max) else {
        tracing::warn!("Unable to increase fileno limits. Using default");
        return;
    };

    tracing::info!(%new_limit, "Increased fileno limit");
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
        active_store: None,
    })?;
    let config = Arc::new(config);

    if args.show_config {
        println!("{}", serde_json::to_string(&config).unwrap());
        return Ok(());
    }

    // Increase the number of allowed open files the the max
    request_max_fileno_limit();

    // Connect to the db
    let connection = connect_to_database(&config).await?;
    let connection = Arc::new(connection);

    // Activate blob stores
    let stores = activate_stores(connection.clone()).await;

    // Launch evictions threads
    let one_min_in_seconds = 60 * 1;
    let ten_mins_in_seconds = one_min_in_seconds * 10;
    let one_hour_in_seconds = one_min_in_seconds * 60;
    let one_week_in_seconds = one_hour_in_seconds * 24 * 7;
    launch_job_eviction(connection.clone(), ten_mins_in_seconds, one_week_in_seconds);
    launch_blob_eviction(
        connection.clone(),
        one_min_in_seconds,
        one_hour_in_seconds,
        stores.clone(),
    );

    // Launch the server
    let router = create_router(connection.clone(), config.clone(), &stores);
    axum::Server::bind(&config.server_addr.parse()?)
        .serve(router.into_make_service())
        .await?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use entity::blob_store;
    use sea_orm::{prelude::Uuid, ActiveModelTrait, EntityTrait, PaginatorTrait};
    use std::sync::Arc;

    use axum::{
        body::Body,
        http::{self, header::*, Request, StatusCode},
    };
    use chrono::Duration;
    use mime::BOUNDARY;
    use serde_json::{json, Value};
    use std::io::Write;
    use tower::Service;

    async fn create_test_store(
        db: &DatabaseConnection,
    ) -> Result<Uuid, Box<dyn std::error::Error>> {
        database::create_dbonly_blob_store(db).await?;

        let test_store = blob_store::ActiveModel {
            id: NotSet,
            r#type: Set("TestBlobStore".into()),
        };
        let inserted = test_store.insert(db).await?;

        Ok(inserted.id)
    }

    fn create_config(store_id: Uuid) -> Result<config::RSCConfig, Box<dyn std::error::Error>> {
        Ok(config::RSCConfig::new(config::RSCConfigOverride {
            config_override: Some("".into()),
            server_addr: Some("test:0000".into()),
            database_url: Some("".into()),
            standalone: Some(true),
            active_store: Some(store_id.to_string()),
        })?)
    }

    async fn create_fake_blob(
        db: &DatabaseConnection,
        store_id: Uuid,
    ) -> Result<Uuid, Box<dyn std::error::Error>> {
        let active_key = entity::blob::ActiveModel {
            id: NotSet,
            created_at: Set((Utc::now() - Duration::days(5)).naive_utc()),
            updated_at: Set((Utc::now() - Duration::days(5)).naive_utc()),
            key: Set("InsecureKey".into()),
            size: Set(11),
            store_id: Set(store_id),
        };

        let inserted_key = active_key.insert(db).await?;

        Ok(inserted_key.id)
    }

    #[tokio::test]
    async fn nominal() {
        let db = create_standalone_db().await.unwrap();
        let store_id = create_test_store(&db).await.unwrap();
        let api_key = create_insecure_api_key(&db).await.unwrap();
        let blob_id = create_fake_blob(&db, store_id.clone()).await.unwrap();
        let config = create_config(store_id.clone()).unwrap();
        let db = Arc::new(db);
        let stores = activate_stores(db.clone()).await;
        let mut router = create_router(db.clone(), Arc::new(config), &stores);

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
                    .header(CONTENT_TYPE, "application/json")
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
                    .header(CONTENT_TYPE, "application/json")
                    .header(AUTHORIZATION, api_key.clone())
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::BAD_REQUEST);

        // Authorization check with invalid auth should 401
        let res = router
            .call(
                Request::builder()
                    .uri("/auth/check")
                    .method(http::Method::POST)
                    .header("Authorization", "badauth")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::UNAUTHORIZED);

        // Authorization check with valid auth should 200
        let res = router
            .call(
                Request::builder()
                    .uri("/auth/check")
                    .method(http::Method::POST)
                    .header("Authorization", api_key.clone())
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::OK);

        // Correctly inserting a job should 200
        let res = router
            .call(
                Request::builder()
                    .uri("/job")
                    .method(http::Method::POST)
                    .header(CONTENT_TYPE, "application/json")
                    .header(AUTHORIZATION, api_key.clone())
                    .body(Body::from(
                        serde_json::to_vec(&json!({
                            "cmd": b"blarg",
                            "env": b"PATH=/usr/bin",
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

        // Non-matching job should 404 with expected body
        let res = router
            .call(
                Request::builder()
                    .uri("/job/matching")
                    .method(http::Method::POST)
                    .header(CONTENT_TYPE, "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&json!({
                            "cmd": b"blrg",
                            "env": b"PATH=/usr/bin",
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

        assert_eq!(res.status(), StatusCode::NOT_FOUND);

        let body = hyper::body::to_bytes(res).await.unwrap();
        let body: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(body, json!({ "type": "NoMatch" }));

        // Matching job should 200 with expected body
        let res = router
            .call(
                Request::builder()
                    .uri("/job/matching")
                    .method(http::Method::POST)
                    .header(CONTENT_TYPE, "application/json")
                    .body(Body::from(
                        serde_json::to_vec(&json!({
                            "cmd": b"blarg",
                            "env": b"PATH=/usr/bin",
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
                "stdout_blob": {
                    "id": blob_id,
                    "url": format!("test://{0}/InsecureKey", store_id),
                },
                "stderr_blob": {
                    "id": blob_id,
                    "url": format!("test://{0}/InsecureKey", store_id),
                },
                "status": 0,
                "runtime":1.0,
                "cputime":1.0,
                "memory": 1000,
                "ibytes":100000,
                "obytes":1000
            })
        );

        // Protected post blob route without auth should 401
        let body_data: Vec<u8> = Vec::new();
        let res = router
            .call(
                Request::builder()
                    .uri("/blob")
                    .method(http::Method::POST)
                    .header(
                        CONTENT_TYPE,
                        format!("multipart/form-data; boundary={}", BOUNDARY),
                    )
                    .body(body_data.into())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::UNAUTHORIZED);

        // Protected post blob route with auth should 200
        let mut body_data: Vec<u8> = Vec::new();
        write!(body_data, "--{}\r\n", BOUNDARY).unwrap();
        write!(
            body_data,
            "Content-Disposition: form-data; name=\"file\";\r\n"
        )
        .unwrap();
        write!(body_data, "\r\n").unwrap();
        write!(body_data, "contents").unwrap();
        write!(body_data, "\r\n").unwrap();
        write!(body_data, "--{}--\r\n", BOUNDARY).unwrap();

        let res = router
            .call(
                Request::builder()
                    .uri("/blob")
                    .method(http::Method::POST)
                    .header(
                        CONTENT_TYPE,
                        format!("multipart/form-data; boundary={}", BOUNDARY),
                    )
                    .header(AUTHORIZATION, api_key)
                    .body(body_data.into())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::OK);

        // Allowed version should should 200
        let res = router
            .call(
                Request::builder()
                    .uri("/version/check?version=sifive/wake/1.2.3")
                    .method(http::Method::GET)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::OK);

        // Disallowed version should should 403
        let res = router
            .call(
                Request::builder()
                    .uri("/version/check?version=sifive/foo/1.2.3")
                    .method(http::Method::GET)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(res.status(), StatusCode::FORBIDDEN);
    }

    #[tokio::test]
    async fn ttl_eviction() {
        let db = create_standalone_db().await.unwrap();
        let store_id = create_test_store(&db).await.unwrap();
        let blob_id = create_fake_blob(&db, store_id).await.unwrap();
        let conn = Arc::new(db);

        // Create a job that is 5 days old
        let insert_job = entity::job::ActiveModel {
            id: NotSet,
            created_at: Set((Utc::now() - Duration::days(5)).naive_utc()),
            hash: Set("00000000".to_string()),
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
            label: Set("".to_string()),
        };

        insert_job.save(conn.clone().as_ref()).await.unwrap();

        // Create a job that is 1 day old
        let insert_job = entity::job::ActiveModel {
            id: NotSet,
            created_at: Set((Utc::now() - Duration::days(1)).naive_utc()),
            hash: Set("00000001".to_string()),
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
            label: Set("".to_string()),
        };

        insert_job.save(conn.clone().as_ref()).await.unwrap();

        let count = entity::job::Entity::find()
            .count(conn.clone().as_ref())
            .await
            .unwrap();
        assert_eq!(count, 2);

        // Setup eviction for jobs older than 3 days ticking every second
        launch_job_eviction(conn.clone(), 1, 60 * 60 * 24 * 3);
        tokio::time::sleep(tokio::time::Duration::from_millis(2000)).await;

        let count = entity::job::Entity::find()
            .count(conn.clone().as_ref())
            .await
            .unwrap();
        assert_eq!(count, 1);
    }
}
