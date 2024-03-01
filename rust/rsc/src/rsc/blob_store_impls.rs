use crate::blob::*;
use async_trait::async_trait;
use data_encoding::BASE64URL;
use futures::stream::BoxStream;
use rand_core::{OsRng, RngCore};
use sea_orm::prelude::Uuid;
use tokio::fs::File;
use tokio::io::BufWriter;
use tokio_util::bytes::Bytes;
use tokio_util::io::StreamReader;

fn create_temp_filename() -> String {
    let mut key = [0u8; 16];
    OsRng.fill_bytes(&mut key);
    // URL must be used as files can't contain /
    BASE64URL.encode(&key)
}

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

        let key = create_temp_filename();
        let path = std::path::Path::new(&self.root).join(key.clone());
        let mut file = BufWriter::new(File::create(path).await?);

        tokio::io::copy(&mut reader, &mut file).await?;

        Ok(key)
    }

    async fn download_url(&self, key: String) -> String {
        return format!("file://{0}/{key}", self.root);
    }

    async fn delete_key(&self, key: String) -> Result<(), std::io::Error> {
        let path = std::path::Path::new(&self.root).join(key);
        tokio::fs::remove_file(path).await
    }
}

impl DebugBlobStore for LocalBlobStore {}

#[derive(Debug, Clone)]
pub struct TestBlobStore {
    pub id: Uuid,
}

#[async_trait]
impl BlobStore for TestBlobStore {
    async fn stream<'a>(
        &self,
        _stream: BoxStream<'a, Result<Bytes, std::io::Error>>,
    ) -> Result<String, std::io::Error> {
        Ok(create_temp_filename())
    }

    async fn download_url(&self, key: String) -> String {
        return format!("test://{0}/{key}", self.id);
    }

    async fn delete_key(&self, _key: String) -> Result<(), std::io::Error> {
        Ok(())
    }
}

impl DebugBlobStore for TestBlobStore {}

#[derive(Debug, Clone)]
pub struct DbOnlyBlobStore {
    pub id: Uuid,
}

#[async_trait]
impl BlobStore for DbOnlyBlobStore {
    async fn stream<'a>(
        &self,
        _stream: BoxStream<'a, Result<Bytes, std::io::Error>>,
    ) -> Result<String, std::io::Error> {
        panic!("DbOnly Blobs must not be created at runtime");
    }

    async fn download_url(&self, key: String) -> String {
        return format!("dbonly://{key}");
    }

    async fn delete_key(&self, _key: String) -> Result<(), std::io::Error> {
        Ok(())
    }
}

impl DebugBlobStore for DbOnlyBlobStore {}
