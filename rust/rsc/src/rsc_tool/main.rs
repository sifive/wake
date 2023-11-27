use clap::{Parser, Subcommand};
use data_encoding::BASE64;
use entity::api_key;
use inquire::Confirm;
use is_terminal::IsTerminal;
use migration::{DbErr, Migrator, MigratorTrait};
use rand_core::{OsRng, RngCore};
use sea_orm::{
    ActiveModelTrait, ActiveValue::*, ColumnTrait, DatabaseConnection, EntityTrait, QueryFilter,
};
use std::io::{Error, ErrorKind};
use tracing;

mod table;

#[path = "../common/config.rs"]
mod config;

#[tracing::instrument]
async fn add_api_key(
    opts: AddKeyOpts,
    conn: &DatabaseConnection,
) -> Result<(), Box<dyn std::error::Error>> {
    // If the user hasn't specified a key generate one. This is
    // the expected way for this tool to be used.
    let key = match &opts.key {
        None => {
            let mut buf = [0u8; 24];
            OsRng.fill_bytes(&mut buf);
            BASE64.encode(&buf)
        }
        Some(key) => key.clone(),
    };

    // Go ahead and insert the key
    let insert_key = api_key::ActiveModel {
        id: NotSet,
        key: Set(key.clone()),
        desc: Set(opts.desc.clone()),
    };
    tracing::info!("Adding key = {} as valid API key", &key);
    insert_key.insert(conn).await?;

    // If everything was a success go ahead and tell the user the
    // API key.
    tracing::info!("Key {} Added", &key);
    println!("\nSuccessfully added {} as an API key", key);
    Ok(())
}

#[tracing::instrument]
async fn list_api_keys(_: ListKeysOpts, conn: &DatabaseConnection) -> Result<(), DbErr> {
    let mut keys: Vec<_> = api_key::Entity::find()
        .all(conn)
        .await?
        .into_iter()
        .map(|x| {
            vec![
                format!("{}", x.id),
                x.key,
                textwrap::wrap(&x.desc, 60).join("\n"),
            ]
        })
        .collect();

    let headers = vec!["Id".into(), "Key".into(), "Desc".into()];
    keys.insert(0, headers.clone());

    table::print_table(keys);

    Ok(())
}

#[tracing::instrument]
async fn remove_api_key(
    key: String,
    conn: &DatabaseConnection,
) -> Result<(), Box<dyn std::error::Error>> {
    // First find the key, because its a very strange user expierence to see
    // "key X was successfully removed" when it never existed in the first place.
    // It means that you can never be quite sure you acomplished what you thought
    // you did and have to use list-keys to check. This also lets us output
    // the description before commiting.
    let Some(result) = api_key::Entity::find()
        .filter(api_key::Column::Key.eq(&key))
        .one(conn)
        .await?
    else {
        println!("{} is not a valid key", key);
        std::process::exit(2);
    };

    // We only want to prompt the user if the user can type into the terminal
    if std::io::stdin().is_terminal() {
        let should_delete = Confirm::new("Are you sure you want to delete this key?")
            .with_default(false)
            .with_help_message(format!("key = {}, desc = {:?}", key, result.desc).as_str())
            .prompt()?;

        if !should_delete {
            println!("Aborting key removal");
            return Ok(());
        }
    }

    // Ok now that we're really sure we want to delete this key
    tracing::info!("Deleting key {}", &key);
    api_key::Entity::delete_many()
        .filter(api_key::Column::Key.eq(&key))
        .exec(conn)
        .await?;

    // If we haven't returned already then log it and tell the user
    tracing::info!("Key {} deleted", &key);
    println!("Key {} was successfully removed", key);
    Ok(())
}

// Define all of our top level commands
#[derive(Debug, Parser)]
#[command(author, version, about, long_about = None)]
struct TopLevel {
    #[arg(
        help = "Specify a config override file",
        value_name = "CONFIG",
        short,
        long
    )]
    config_override: Option<String>,

    #[arg(
        help = "Specify and override for the database url",
        value_name = "DATABASE_URL",
        long
    )]
    database_url: Option<String>,

    #[arg(help = "Show's the config and then exits", long)]
    show_config: bool,

    // The `command` option will delegate option parsing to the command type,
    // starting at the first free argument.
    #[command(subcommand)]
    api_key_command: Option<ApiKey>,
}

#[derive(Debug, Subcommand)]
enum ApiKey {
    //#[options(help = "list all api keys in the database")]
    List(ListKeysOpts),

    //#[options(help = "add an api key to the database")]
    AddKey(AddKeyOpts),

    //#[options(help = "remove an api key from the database")]
    RemoveKey(RemoveKeyOpts),
}

#[derive(Debug, Parser)]
struct AddKeyOpts {
    #[arg(
        help = "If specified this is the key that will be used, otherwise one will be generated",
        value_name = "KEY",
        long
    )]
    key: Option<String>,

    #[arg(
        required = true,
        help = "The description of the key. This might tell you where the key is meant to be used",
        value_name = "DESC",
        long
    )]
    desc: String,
}

#[derive(Debug, Parser)]
struct ListKeysOpts {}

#[derive(Debug, Parser)]
struct RemoveKeyOpts {
    #[arg(
        required = true,
        help = "The key you want to remove",
        value_name = "KEY"
    )]
    key: String,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Parse our arguments
    let args = TopLevel::parse();

    // Gather our config
    let config = config::GSCConfig::new(config::GSCConfigOverride {
        config_override: args.config_override,
        database_url: args.database_url,
        ..Default::default()
    })?;

    if args.show_config {
        println!("{}", serde_json::to_string(&config).unwrap());
        return Ok(());
    }

    // connect to our db
    let connection = sea_orm::Database::connect(config.database_url).await?;
    let pending_migrations = Migrator::get_pending_migrations(&connection).await?;
    if pending_migrations.len() != 0 {
        let err = Error::new(
            ErrorKind::Other,
            format!(
                "This gsc-tool version expects {:?} additional migrations to be applied",
                pending_migrations.len()
            ),
        );
        tracing::error! {%err, "unperformed migrations, please apply these migrations before using gsc-tool"};
        Err(err)?;
    }

    let Some(api_key_args) = args.api_key_command else {
        return Ok(());
    };

    match api_key_args {
        ApiKey::List(args) => list_api_keys(args, &connection).await?,
        ApiKey::AddKey(args) => add_api_key(args, &connection).await?,
        ApiKey::RemoveKey(args) => remove_api_key(args.key, &connection).await?,
    }

    Ok(())
}
