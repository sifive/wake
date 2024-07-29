use config::{Config, ConfigError, Environment, File};
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Default)]
pub struct RSCConfigOverride {
    pub active_store: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCConfig {
    pub database_url: String,
    pub server_addr: String,
    pub active_store: String,
}

impl RSCConfig {
    pub fn new(overrides: RSCConfigOverride) -> Result<RSCConfig, ConfigError> {
        // Gather the config
        let config = Config::builder()
            .add_source(Environment::with_prefix("WAKE_RSC_CONFIG"))
            .add_source(File::with_name(".config").required(false))
            .set_override_option("active_store", overrides.active_store)?
            .build()?;

        config.try_deserialize()
    }
}
