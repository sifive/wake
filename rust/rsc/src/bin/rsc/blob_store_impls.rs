use crate::blob::*;
use async_trait::async_trait;
use blake3;
use futures::stream::BoxStream;
use futures::StreamExt;
use rand::{thread_rng, RngCore};
use sea_orm::prelude::Uuid;
use std::fmt::Write;
use std::io::Read;
use tokio::fs::File;
use tokio::io::AsyncReadExt;
use tokio::io::BufWriter;
use tokio_util::bytes::Bytes;
use tokio_util::io::StreamReader;

fn create_temporary_blob_path() -> std::path::PathBuf {
    // 2 deep @ 8 bytes wide
    let mut parts = [0u8; 10];
    thread_rng().fill_bytes(&mut parts);

    let mut buf = std::path::PathBuf::from("tmp");

    // First 2 bytes represent the containing directories
    for i in 0..2 {
        let mut s = String::new();
        write!(&mut s, "{:02X}", parts[i]).unwrap();
        buf.push(s);
    }

    // Next 8 bytes represent the file name
    let mut s = String::new();
    for i in 2..10 {
        write!(&mut s, "{:02X}", parts[i]).unwrap();
    }
    buf.push(s);

    return buf;
}

fn create_path_from_hash(hash: &blake3::Hash) -> std::path::PathBuf {
    // bc 1b b8 50 a5 b6 1e 20 6f e3 32 48 fa 31 53 38 4e 49 53 46 65 4b 0c 48 7e 21 a1 c9 5f ca c2 19
    // There are 32 bytes in the file hash. The first two are used to specify the directory
    // while the remaining 30 specify the file name.

    let parts: &[u8; 32] = hash.as_bytes();

    let mut buf = std::path::PathBuf::from("");

    // First 2 bytes represent the containing directories
    for i in 0..2 {
        let mut s = String::new();
        write!(&mut s, "{:02X}", parts[i]).unwrap();
        buf.push(s);
    }

    // Next 30 bytes represent the file name
    let mut s = String::new();
    for i in 2..32 {
        write!(&mut s, "{:02X}", parts[i]).unwrap();
    }
    buf.push(s);

    return buf;
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

    // hash the bytes
    // hash the size
    // if file exists
    //   spawn task to delete temp file
    //   return hash as key
    // else
    //   move file to correct location (has to be done before return)
    //   return hash as key
    async fn stream<'a>(
        &self,
        stream: BoxStream<'a, Result<Bytes, std::io::Error>>,
    ) -> Result<(String, i64), std::io::Error> {
        let mut hasher = blake3::Hasher::new();

        let stream = stream.inspect(|x| {
            if let Ok(bytes) = x {
                let mut vec: Vec<u8> = Vec::with_capacity(bytes.len());
                for byte in bytes.bytes() {
                    vec.push(byte.unwrap());
                }
                hasher.update(&vec);
            };
        });
        let reader = StreamReader::new(stream);
        futures::pin_mut!(reader);

        let tmp_key_path = create_temporary_blob_path();
        let tmp_full_path = std::path::Path::new(&self.root).join(tmp_key_path.clone());
        tokio::fs::create_dir_all(tmp_full_path.parent().unwrap()).await?;

        let mut file = BufWriter::new(File::create(tmp_full_path.clone()).await?);
        let written = tokio::io::copy(&mut reader, &mut file).await?;

        let size = match i64::try_from(written) {
            Err(err) => {
                tracing::error!(%err, %written, "Size overflows i64, setting to i64::MAX instead");
                i64::MAX
            }
            Ok(size) => size,
        };

        hasher.update(&size.to_le_bytes());

        let final_key_path = create_path_from_hash(&hasher.finalize());
        let final_full_path = std::path::Path::new(&self.root).join(final_key_path.clone());
        tokio::fs::create_dir_all(final_full_path.parent().unwrap()).await?;
        tokio::fs::rename(tmp_full_path, final_full_path).await?;

        let key = match final_key_path.into_os_string().into_string() {
            Err(path) => {
                tracing::error!("Cannot convert path to string, returning lossy path instead");
                path.to_string_lossy().to_string()
            }
            Ok(s) => s,
        };

        Ok((key, size))
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
    ) -> Result<(String, i64), std::io::Error> {
        Ok(("TestTestTest".to_string(), 0xDEADBEEF))
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
    ) -> Result<(String, i64), std::io::Error> {
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

        // Safe to unwrap here since count is stopped at 101
        let size: i64 = count.try_into().unwrap();

        Ok((
            buffer[0..count]
                .iter()
                .map(|x| format!("%{:02X}", x))
                .collect::<Vec<_>>()
                .concat(),
            size,
        ))
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
