use config::{Config, ConfigError, Environment, File};
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCCronLoopConfig {
    // How often to run the loop in seconds
    pub tick_rate: u64,
    // Maximum number of objects to procss per tick. Must be 1 >= x <= 16000
    pub chunk_size: i32,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCTTLConfig {
    // How often to run the eviction check in seconds
    pub tick_rate: u64,
    // How long an object is allowed to live
    pub ttl: u64,
    // Maximum number of objects to delete from the db at a time. Must be 1 >= x <= 16000
    pub chunk_size: u32,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCBlobTTLConfig {
    // How often to run the eviction check in seconds
    pub tick_rate: u64,
    // How long an object is allowed to live
    pub ttl: u64,
    // Maximum number of objects to delete from the db at a time. Must be 1 >= x <= 16000
    pub chunk_size: u32,
    // Maximum number of files to delete from the disk per task
    pub file_chunk_size: usize,
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
pub struct RSCLoadShedConfig {
    // How often to refresh the system load
    pub tick_rate: u64,
    // Load value after which load should be statistically shed
    pub target: f64,
    // The minimum amount of time a job must take to complete in order to be cached
    pub min_runtime: f64,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCConfig {
    // The url used to connect to the postgres database
    pub database_url: String,
    // The address the that server should bind to
    pub server_address: String,
    // The amount of time a query should wait for a connection before timing out in seconds
    pub connection_pool_timeout: u64,
    // The blob store that new blobs should be written into
    pub active_store: String,
    // The directory that server logs should be written to. If None logs are written to stdout
    pub log_directory: Option<String>,
    // The config to control blob eviction
    pub blob_eviction: RSCBlobTTLConfig,
    // The config to control job eviction
    pub job_eviction: RSCJobEvictionConfig,
    // The config to control job size calculation
    pub job_size_calculate: RSCCronLoopConfig,
    // The config to control load shed
    pub load_shed: RSCLoadShedConfig,
}

impl RSCConfig {
    pub fn new() -> Result<RSCConfig, ConfigError> {
        // Gather the config
        let config = Config::builder()
            .add_source(Environment::with_prefix("WAKE_RSC_CONFIG"))
            .add_source(File::with_name(".config"))
            .build()?;

        let config: RSCConfig = config.try_deserialize()?;

        if config.load_shed.target == 0.0 {
            return Err(ConfigError::Message(
                "Load shed target must not be zero".to_string(),
            ));
        }

        Ok(config)
    }
}
