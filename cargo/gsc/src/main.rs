use axum::{
    // http::StatusCode,
    routing::post,
    Json,
    Router,
};
use entity::job;
use migration::{Migrator, MigratorTrait};
use sea_orm::{ActiveModelTrait, ActiveValue, DatabaseConnection};
use serde::Deserialize;
use std::sync::Arc;

#[derive(Deserialize)]
struct AddJobPayload {
    cmd: String,
    env: String,
}

async fn add_job(Json(payload): Json<AddJobPayload>, conn: Arc<DatabaseConnection>) {
    let insert_job = job::ActiveModel {
        id: ActiveValue::NotSet,
        cmd: ActiveValue::Set(payload.cmd),
        env: ActiveValue::Set(payload.env),
    };
    insert_job.insert(conn.as_ref()).await.unwrap();
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
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
    axum::Server::bind(&"127.0.0.1:3000".parse().unwrap())
        .serve(app.into_make_service())
        .await
        .unwrap();
    Ok(())
}
