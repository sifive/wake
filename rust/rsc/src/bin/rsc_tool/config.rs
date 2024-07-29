use config::{Config, ConfigError, Environment, File};
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Default)]
pub struct RSCToolConfigOverride {
    pub database_url: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCToolConfig {
    pub database_url: String,
}

impl RSCToolConfig {
    pub fn new(overrides: RSCToolConfigOverride) -> Result<RSCToolConfig, ConfigError> {
        // Gather the config
        let config = Config::builder()
            .add_source(Environment::with_prefix("WAKE_RSC_CONFIG"))
            .add_source(File::with_name(".config").required(false))
            .set_override_option("database_url", overrides.database_url)?
            .build()?;

        config.try_deserialize()
    }
}
