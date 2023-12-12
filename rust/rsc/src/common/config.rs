use config::{Config, ConfigError, Environment, File};
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Default)]
pub struct RSCConfigOverride {
    pub config_override: Option<String>,
    pub database_url: Option<String>,
    pub server_addr: Option<String>,
    pub standalone: Option<bool>,
    // TODO: the backing store should be configurable via URI
    pub local_store: Option<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct RSCConfig {
    pub database_url: String,
    // TODO: We should allow setting a domain as well
    pub server_addr: String,
    pub standalone: bool,
    pub local_store: Option<String>,
}

impl RSCConfig {
    pub fn new(overrides: RSCConfigOverride) -> Result<RSCConfig, ConfigError> {
        // Gather the config
        let config = Config::builder()
            .add_source(Environment::with_prefix("WAKE_RSC"))
            .add_source(
                File::with_name(
                    &overrides
                        .config_override
                        .as_ref()
                        .map(|x| x.as_str())
                        .unwrap_or(".config"),
                )
                .required(false),
            )
            .set_override_option("database_url", overrides.database_url)?
            .set_override_option("server_addr", overrides.server_addr)?
            .set_override_option("standalone", overrides.standalone)?
            .set_override_option("local_store", overrides.local_store)?
            .build()?;

        config.try_deserialize()
    }
}
