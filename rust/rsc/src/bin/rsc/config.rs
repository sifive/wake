use config::{Config, ConfigError, Environment, File};
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCTTLConfig {
    // How often to run the eviction check in seconds
    pub tick_rate: u64,
    // How long an object is allowed to live
    pub ttl: u64,
    // Maximum number of objects to delete at a time. Must be 1 >= x <= 16000
    pub chunk_size: u32,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCLRUConfig {
    // The max size in bytes that the cache may reach before eviction
    pub high_mark: u64,
    // The end size in bytes that the cache should reach after eviction
    pub low_mark: u64,
    // Maximum number of objects to delete at a time. Must be 1 >= x <= 16000
    pub chunk_size: u32,
}

#[derive(Debug, Deserialize, Serialize)]
pub enum RSCJobEvictionConfig {
    // Time to live eviction strategy
    #[serde(rename = "ttl")]
    TTL(RSCTTLConfig),
    // Least recently used eviction strategy
    #[serde(rename = "lru")]
    LRU(RSCLRUConfig),
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCConfig {
    // The url used to connect to the postgres database
    pub database_url: String,
    // The address the that server should bind to
    pub server_address: String,
    // The amount of time a query should wait for a connection before timing out in seconds
    pub connection_pool_timeout: u32,
    // The blob store that new blobs should be written into
    pub active_store: String,
    // The directory that server logs should be written to. If None logs are written to stdout
    pub log_directory: Option<String>,
    // The config to control blob eviction
    pub blob_eviction: RSCTTLConfig,
    // The config to control job eviction
    pub job_eviction: RSCJobEvictionConfig,
}

impl RSCConfig {
    pub fn new() -> Result<RSCConfig, ConfigError> {
        // Gather the config
        let config = Config::builder()
            .add_source(Environment::with_prefix("WAKE_RSC_CONFIG"))
            .add_source(File::with_name(".config"))
            .build()?;

        config.try_deserialize()
    }
}
