use axum::{http::StatusCode, routing::post, Json, Router};
use entity::job;
use migration::{Migrator, MigratorTrait};
use sea_orm::{ActiveModelTrait, ActiveValue, DatabaseConnection};
use serde::Deserialize;
use std::sync::Arc;
use tracing;

#[derive(Debug, Deserialize)]
struct AddJobPayload {
    cmd: String,
    env: String,
}

#[tracing::instrument]
async fn add_job(Json(payload): Json<AddJobPayload>, conn: Arc<DatabaseConnection>) -> StatusCode {
    let insert_job = job::ActiveModel {
        id: ActiveValue::NotSet,
        cmd: ActiveValue::Set(payload.cmd),
        env: ActiveValue::Set(payload.env),
    };
    let insert_result = insert_job.insert(conn.as_ref()).await;
    match insert_result {
        Ok(_) => StatusCode::OK,
        Err(cause) => {
            tracing::error! {
              %cause,
              "failed to add job"
            };
            StatusCode::BAD_REQUEST
        }
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // setup a subscriber so that we always have logging
    let subscriber = tracing_subscriber::FmtSubscriber::new();
    tracing::subscriber::set_global_default(subscriber)?;

    // connect to our db
    let connection = sea_orm::Database::connect("postgres://127.0.0.1/test").await?;
    Migrator::up(&connection, None).await?;
    let state = Arc::new(connection);

    // build our application with a single route
    let app = Router::new().route(
        "/job",
        post({
            let shared_state = state.clone();
            move |body| add_job(body, shared_state)
        }),
    );

    // run it with hyper on localhost:3000
    axum::Server::bind(&"127.0.0.1:3000".parse()?)
        .serve(app.into_make_service())
        .await?;
    Ok(())
}
