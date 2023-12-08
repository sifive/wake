use crate::config::RSCConfig;
use crate::types::GetUploadUrlResponse;
use axum::{body::Bytes, extract::Multipart, http::StatusCode, BoxError, Json};
use sea_orm::DatabaseConnection;
use std::sync::Arc;
use tracing;

use futures::{Stream, TryStreamExt};
use tokio::fs::File;
use tokio::io::BufWriter;
use tokio_util::io::StreamReader;

// to prevent directory traversal attacks we ensure the path consists of exactly one normal
// component
fn path_is_valid(path: &str) -> bool {
    let path = std::path::Path::new(path);
    let mut components = path.components().peekable();

    if let Some(first) = components.peek() {
        if !matches!(first, std::path::Component::Normal(_)) {
            return false;
        }
    }

    components.count() == 1
}

async fn stream_to_file<S, E>(
    parent: &str,
    path: &str,
    stream: S,
) -> Result<(), (StatusCode, String)>
where
    S: Stream<Item = Result<Bytes, E>>,
    E: Into<BoxError>,
{
    if !path_is_valid(path) {
        return Err((StatusCode::BAD_REQUEST, "Invalid path".to_owned()));
    }

    async {
        // Convert the stream into an `AsyncRead`.
        let body_with_io_error =
            stream.map_err(|err| std::io::Error::new(std::io::ErrorKind::Other, err));
        let body_reader = StreamReader::new(body_with_io_error);
        futures::pin_mut!(body_reader);

        // Create the file. `File` implements `AsyncWrite`.
        let path = std::path::Path::new(parent).join(path);
        let mut file = BufWriter::new(File::create(path).await?);

        // Copy the body into the file.
        tokio::io::copy(&mut body_reader, &mut file).await?;

        Ok::<_, std::io::Error>(())
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
        let file_name = if let Some(file_name) = field.name() {
            file_name.to_owned()
        } else {
            tracing::warn!("Skipping part upload due to missing key.");
            continue;
        };

        let Some(ref local_store) = config.local_store else {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                "Invalid configuration".into(),
            );
        };

        // TODO: instead of writing the user provided field name to disk we should
        // generate a random temporary name.
        if let Err(inner) = stream_to_file(local_store, &file_name, field).await {
            return inner;
        }
    }

    (StatusCode::OK, "ok".into())
}
