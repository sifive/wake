use crate::blob_store_service::fetch_local_blob_store;
use crate::types::{GetUploadUrlResponse, PostBlobResponse, PostBlobResponsePart};
use async_trait::async_trait;
use axum::{extract::Multipart, http::StatusCode, Json};
use data_encoding::BASE64URL;
use entity::blob;
use futures::stream::BoxStream;
use futures::TryStreamExt;
use rand_core::{OsRng, RngCore};
use sea_orm::{ActiveModelTrait, ActiveValue::*, DatabaseConnection};
use std::sync::Arc;
use tokio::fs::File;
use tokio::io::BufWriter;
use tokio_util::bytes::Bytes;
use tokio_util::io::StreamReader;
use tracing;

// TODO: Update this trait to return the url for a given key
#[async_trait]
pub trait BlobStore {
    async fn stream<'a>(
        &self,
        stream: BoxStream<'a, Result<Bytes, std::io::Error>>,
    ) -> Result<String, std::io::Error>;

    async fn download_url(&self, key: String) -> String;
}

pub trait DebugBlobStore: BlobStore + std::fmt::Debug {}

#[derive(Debug, Clone)]
pub struct LocalBlobStore {
    pub root: String,
}

#[async_trait]
impl BlobStore for LocalBlobStore {
    async fn stream<'a>(
        &self,
        stream: BoxStream<'a, Result<Bytes, std::io::Error>>,
    ) -> Result<String, std::io::Error> {
        let reader = StreamReader::new(stream);
        futures::pin_mut!(reader);

        let name = create_temp_filename();
        let path = std::path::Path::new(&self.root).join(name.clone());
        let mut file = BufWriter::new(File::create(path).await?);

        tokio::io::copy(&mut reader, &mut file).await?;

        Ok(name)
    }

    async fn download_url(&self, key: String) -> String {
        return format!("file://{0}/{key}", self.root);
    }
}

impl DebugBlobStore for LocalBlobStore {}

fn create_temp_filename() -> String {
    let mut key = [0u8; 16];
    OsRng.fill_bytes(&mut key);
    // URL must be used as files can't contain /
    BASE64URL.encode(&key)
}

#[tracing::instrument]
pub async fn get_upload_url(server_addr: String) -> Json<GetUploadUrlResponse> {
    let url = server_addr + "/blob";
    Json(GetUploadUrlResponse { url })
}

#[tracing::instrument]
pub async fn create_blob(
    mut multipart: Multipart,
    db: Arc<DatabaseConnection>,
    store: Arc<dyn DebugBlobStore + Send + Sync>,
) -> (StatusCode, Json<PostBlobResponse>) {
    let mut parts: Vec<PostBlobResponsePart> = Vec::new();

    let store_id = match fetch_local_blob_store(&db).await {
        Ok(id) => id,
        Err(msg) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(PostBlobResponse::Error {
                    message: msg.to_string(),
                }),
            )
        }
    };

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
