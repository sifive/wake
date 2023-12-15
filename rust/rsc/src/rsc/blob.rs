use crate::types::GetUploadUrlResponse;
use async_trait::async_trait;
use axum::{extract::Multipart, http::StatusCode, Json};
use data_encoding::BASE64URL;
use futures::TryStreamExt;
use rand_core::{OsRng, RngCore};
use sea_orm::DatabaseConnection;
use std::sync::Arc;
use tokio::fs::File;
use tokio::io::BufWriter;
use tokio_util::io::StreamReader;
use tracing;

// TODO: Update this trait to return the url for a given key
#[async_trait]
pub trait BlobStore {
    async fn stream<A, B>(&self, reader: StreamReader<A, B>) -> Result<String, std::io::Error>
    where
        StreamReader<A, B>: tokio::io::AsyncRead + std::marker::Send;
}

pub trait DebugBlobStore: BlobStore + std::fmt::Debug {}

#[derive(Debug, Clone)]
pub struct LocalBlobStore {
    pub root: String,
}

#[async_trait]
impl BlobStore for LocalBlobStore {
    async fn stream<A, B>(&self, reader: StreamReader<A, B>) -> Result<String, std::io::Error>
    where
        StreamReader<A, B>: tokio::io::AsyncRead + std::marker::Send,
    {
        futures::pin_mut!(reader);

        let name = create_temp_filename();
        let path = std::path::Path::new(&self.root).join(name.clone());
        let mut file = BufWriter::new(File::create(path).await?);

        tokio::io::copy(&mut reader, &mut file).await?;

        Ok(name)
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
    _conn: Arc<DatabaseConnection>,
    store: impl DebugBlobStore,
) -> (StatusCode, String) {
    while let Ok(Some(field)) = multipart.next_field().await {
        let body = field.map_err(|err| std::io::Error::new(std::io::ErrorKind::Other, err));
        match store.stream(StreamReader::new(body)).await {
            // TODO: The blob should be inserted into the db instead of just printing the key
            Ok(key) => println!("{:?}", key),
            Err(msg) => return (StatusCode::INTERNAL_SERVER_ERROR, msg.to_string()),
        }
    }

    (StatusCode::OK, "ok".into())
}