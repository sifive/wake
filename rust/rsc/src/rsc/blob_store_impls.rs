use crate::blob::*;
use async_trait::async_trait;
use data_encoding::BASE64URL;
use futures::stream::BoxStream;
use rand_core::{OsRng, RngCore};
use sea_orm::prelude::Uuid;
use tokio::fs::File;
use tokio::io::AsyncReadExt;
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
    pub id: Uuid,
    pub root: String,
}

#[async_trait]
impl BlobStore for LocalBlobStore {
    fn id(&self) -> Uuid {
        self.id
    }

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
    fn id(&self) -> Uuid {
        self.id
    }

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
    fn id(&self) -> Uuid {
        self.id
    }

    async fn stream<'a>(
        &self,
        stream: BoxStream<'a, Result<Bytes, std::io::Error>>,
    ) -> Result<String, std::io::Error> {
        let reader = StreamReader::new(stream);
        futures::pin_mut!(reader);
        let mut buffer = [0u8; 101];
        let mut count = 0;

        loop {
            // If we have filled the buffer then the request has exceeded the max.
            if count == 101 {
                panic!("DbOnlyBlobStore only supports up to 100 byte blobs");
            }

            let n = reader.read(&mut buffer[count..]).await?;
            if n == 0 {
                break;
            }

            count += n;
        }

        Ok(buffer[0..count]
            .iter()
            .map(|x| format!("%{:02X}", x))
            .collect::<Vec<_>>()
            .concat())
    }

    async fn download_url(&self, key: String) -> String {
        return format!("db://{key}");
    }

    async fn delete_key(&self, _key: String) -> Result<(), std::io::Error> {
        // Nothing outside of the db to delete
        Ok(())
    }
}

impl DebugBlobStore for DbOnlyBlobStore {}
