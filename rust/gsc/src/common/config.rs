use config::{Config, ConfigError, Environment, File};
use serde::Deserialize;

#[derive(Debug, Deserialize, Default)]
pub struct GSCConfigOverride {
    pub config_override: Option<String>,
    pub database_url: Option<String>,
    pub server_addr: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct GSCConfig {
    pub database_url: String,
    // TODO: We should allow setting a domain as well
    pub server_addr: String,
}

impl GSCConfig {
    pub fn new(overrides: GSCConfigOverride) -> Result<GSCConfig, ConfigError> {
        // Gather the config
        let config = Config::builder()
            .add_source(Environment::with_prefix("WAKE_GSC"))
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
            .build()?;

        config.try_deserialize()
    }
}
