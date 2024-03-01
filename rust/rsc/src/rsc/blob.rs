use crate::types::{GetUploadUrlResponse, PostBlobResponse, PostBlobResponsePart};
use async_trait::async_trait;
use axum::{extract::Multipart, http::StatusCode, Json};
use entity::blob;
use futures::stream::BoxStream;
use futures::TryStreamExt;
use sea_orm::prelude::Uuid;
use sea_orm::{ActiveModelTrait, ActiveValue::*, DatabaseConnection};
use std::sync::Arc;
use tokio_util::bytes::Bytes;
use tracing;

#[async_trait]
pub trait BlobStore {
    async fn stream<'a>(
        &self,
        stream: BoxStream<'a, Result<Bytes, std::io::Error>>,
    ) -> Result<String, std::io::Error>;

    async fn download_url(&self, key: String) -> String;
    async fn delete_key(&self, key: String) -> Result<(), std::io::Error>;
}

pub trait DebugBlobStore: BlobStore + std::fmt::Debug {}

#[tracing::instrument]
pub async fn get_upload_url(server_addr: String) -> Json<GetUploadUrlResponse> {
    let url = server_addr + "/blob";
    Json(GetUploadUrlResponse { url })
}

#[tracing::instrument]
pub async fn create_blob(
    mut multipart: Multipart,
    db: Arc<DatabaseConnection>,
    store_id: Uuid,
    store: Arc<dyn DebugBlobStore + Send + Sync>,
) -> (StatusCode, Json<PostBlobResponse>) {
    let mut parts: Vec<PostBlobResponsePart> = Vec::new();

    while let Ok(Some(field)) = multipart.next_field().await {
        let name = match field.name() {
            Some(x) => x.to_string(),
            None => {
                return (
                    StatusCode::BAD_REQUEST,
                    Json(PostBlobResponse::Error {
                        message: "Multipart field must be named".into(),
                    }),
                )
            }
        };

        let result = store
            .stream(Box::pin(field.map_err(|err| {
                std::io::Error::new(std::io::ErrorKind::Other, err)
            })))
            .await;

        if let Err(msg) = result {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(PostBlobResponse::Error {
                    message: msg.to_string(),
                }),
            );
        }

        let active_blob = blob::ActiveModel {
            id: NotSet,
            created_at: NotSet,
            key: Set(result.unwrap()),
            store_id: Set(store_id),
        };

        match active_blob.insert(db.as_ref()).await {
            Err(msg) => {
                return (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    Json(PostBlobResponse::Error {
                        message: msg.to_string(),
                    }),
                )
            }
            Ok(blob) => parts.push(PostBlobResponsePart { id: blob.id, name }),
        }
    }

    (StatusCode::OK, Json(PostBlobResponse::Ok { blobs: parts }))
}
