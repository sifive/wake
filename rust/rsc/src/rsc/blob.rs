use crate::config::RSCConfig;
use crate::types::GetUploadUrlResponse;
use axum::{body::Bytes, extract::Multipart, http::StatusCode, BoxError, Json};
use data_encoding::BASE64URL;
use futures::{Stream, TryStreamExt};
use rand_core::{OsRng, RngCore};
use sea_orm::DatabaseConnection;
use std::sync::Arc;
use tokio::fs::File;
use tokio::io::BufWriter;
use tokio_util::io::StreamReader;
use tracing;

fn create_temp_filename() -> String {
    let mut key = [0u8; 16];
    OsRng.fill_bytes(&mut key);
    // URL must be used as files can't contain /
    BASE64URL.encode(&key)
}

async fn stream_to_file<S, E>(parent: &str, stream: S) -> Result<String, (StatusCode, String)>
where
    S: Stream<Item = Result<Bytes, E>>,
    E: Into<BoxError>,
{
    async {
        // Convert the stream into an `AsyncRead`.
        let body_with_io_error =
            stream.map_err(|err| std::io::Error::new(std::io::ErrorKind::Other, err));
        let body_reader = StreamReader::new(body_with_io_error);
        futures::pin_mut!(body_reader);

        // Create the file. `File` implements `AsyncWrite`.
        let name = create_temp_filename();
        let path = std::path::Path::new(parent).join(name.clone());
        let mut file = BufWriter::new(File::create(path).await?);

        // Copy the body into the file.
        tokio::io::copy(&mut body_reader, &mut file).await?;

        Ok::<String, std::io::Error>(name)
    }
    .await
    .map_err(|err| (StatusCode::INTERNAL_SERVER_ERROR, err.to_string()))
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
    config: Arc<RSCConfig>,
) -> (StatusCode, String) {
    while let Ok(Some(field)) = multipart.next_field().await {
        let Some(ref local_store) = config.local_store else {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                "Invalid configuration".into(),
            );
        };

        if let Err(inner) = stream_to_file(local_store, field).await {
            return inner;
        }
    }

    (StatusCode::OK, "ok".into())
}
