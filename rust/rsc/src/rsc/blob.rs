use crate::config::RSCConfig;
use crate::types::GetUploadUrlResponse;
use axum::{extract::Multipart, Json};
use sea_orm::DatabaseConnection;
use std::sync::Arc;
use tracing;

#[tracing::instrument]
pub async fn get_upload_url(
    _conn: Arc<DatabaseConnection>,
    config: Arc<RSCConfig>,
) -> Json<GetUploadUrlResponse> {
    let url = config.server_addr.clone() + "/blob";
    Json(GetUploadUrlResponse { url })
}

pub async fn create_blob(mut multipart: Multipart, config: Arc<RSCConfig>) -> hyper::StatusCode {
    let mut hash: Option<String> = None;
    let mut contents: Option<axum::body::Bytes> = None;

    let exhausted_without_err = loop {
        let Ok(field) = multipart.next_field().await else {
            break false;
        };

        // if field is None, then we have exhausted the input.
        let Some(field) = field else {
            break true;
        };

        if field.name().is_none() {
            break false;
        }
        let name = field.name().unwrap().to_string();
        let Ok(data) = field.bytes().await else {
            break false;
        };

        if name == "hash" {
            let key = String::from_utf8(data.to_ascii_lowercase()).unwrap();
            hash = Some(key);
        }

        if name == "file" {
            contents = Some(data);
        }
    };

    if !exhausted_without_err {
        return hyper::StatusCode::BAD_REQUEST;
    }

    let Some(hash) = hash else {
        return hyper::StatusCode::BAD_REQUEST;
    };

    let Some(contents) = contents else {
        return hyper::StatusCode::BAD_REQUEST;
    };

    let Some(local_store) = config.local_store.clone() else {
        return hyper::StatusCode::INTERNAL_SERVER_ERROR;
    };

    let dest = local_store + &hash;
    let Ok(_) = std::fs::write(dest, contents) else {
        return hyper::StatusCode::INTERNAL_SERVER_ERROR;
    };

    hyper::StatusCode::OK
}
