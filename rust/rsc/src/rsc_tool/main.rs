use clap::{Parser, Subcommand};
use inquire::Confirm;
use is_terminal::IsTerminal;
use migration::{DbErr, Migrator, MigratorTrait};
use sea_orm::{prelude::Uuid, DatabaseConnection};
use std::io::{Error, ErrorKind};
use tracing;

mod table;

#[path = "../common/config.rs"]
mod config;
#[path = "../common/database.rs"]
mod database;

async fn add_api_key(
    opts: AddApiKeyOpts,
    db: &DatabaseConnection,
) -> Result<(), Box<dyn std::error::Error>> {
    let key = database::create_api_key(db, opts.key, opts.desc).await?;
    println!("Created api key: {}", key.id);
    Ok(())
}

async fn list_api_keys(db: &DatabaseConnection) -> Result<(), DbErr> {
    let mut keys: Vec<_> = database::read_api_keys(db)
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
    keys.insert(0, headers);

    table::print_table(keys);

    Ok(())
}

async fn remove_api_key(
    id: &String,
    db: &DatabaseConnection,
) -> Result<(), Box<dyn std::error::Error>> {
    let uuid = Uuid::parse_str(id)?;
    let Some(key) = database::read_api_key(db, uuid).await? else {
        println!("{} is not a valid key", id);
        std::process::exit(2);
    };

    // We only want to prompt the user if the user can type into the terminal
    if std::io::stdin().is_terminal() {
        let should_delete = Confirm::new("Are you sure you want to delete this key?")
            .with_default(false)
            .with_help_message(format!("key = {}, desc = {:?}", key.key, key.desc).as_str())
            .prompt()?;

        if !should_delete {
            println!("Aborting removal");
            return Ok(());
        }
    }

    // Ok now that we're really sure we want to delete this key
    database::delete_api_key(db, key).await?;

    println!("Key {} was successfully removed", id);
    Ok(())
}

async fn add_local_blob_store(
    opts: AddLocalBlobStoreOpts,
    db: &DatabaseConnection,
) -> Result<(), Box<dyn std::error::Error>> {
    tokio::fs::create_dir_all(opts.root.clone()).await?;
    let store = database::create_local_blob_store(db, opts.root).await?;
    println!("Created local blob store: {}", store.id);
    Ok(())
}

async fn list_local_blob_stores(db: &DatabaseConnection) -> Result<(), DbErr> {
    let mut stores: Vec<_> = database::read_local_blob_stores(db)
        .await?
        .into_iter()
        .map(|x| vec![format!("{}", x.id), x.root])
        .collect();

    let headers = vec!["Id".into(), "Root".into()];
    stores.insert(0, headers);

    table::print_table(stores);

    Ok(())
}

async fn remove_local_blob_store(
    id: &String,
    db: &DatabaseConnection,
) -> Result<(), Box<dyn std::error::Error>> {
    let uuid = Uuid::parse_str(id)?;
    let Some(store) = database::read_local_blob_store(db, uuid).await? else {
        println!("{} is not a valid local blob store", id);
        std::process::exit(2);
    };

    // We only want to prompt the user if the user can type into the terminal
    if std::io::stdin().is_terminal() {
        let should_delete = Confirm::new("Are you sure you want to delete this local blob store?")
            .with_default(false)
            .with_help_message(format!("id = {}, root = {}", store.id, store.root).as_str())
            .prompt()?;

        if !should_delete {
            println!("Aborting removal");
            return Ok(());
        }
    }

    // Try to delete the backing store and fail if it isn't empty
    let Ok(()) = tokio::fs::remove_dir(store.root.clone()).await else {
        println!("Local blob store contains blobs so it is not safe to delete it!");
        std::process::exit(2);
    };

    // Ok now that we're really sure we want to delete this key
    database::delete_local_blob_store(db, store).await?;

    println!("Local Blob Store {} was successfully removed", id);
    Ok(())
}

async fn remove_all_jobs(db: &DatabaseConnection) -> Result<(), Box<dyn std::error::Error>> {
    // Only let a human perform this action
    if !std::io::stdin().is_terminal() {
        println!("Remove all jobs may only be triggered manually by a human.");
        std::process::exit(2);
    }

    let mut should_delete = Confirm::new("Are you REALLY sure you want to delete ALL jobs?")
        .with_default(false)
        .prompt()?;

    if !should_delete {
        println!("Aborting removal");
        return Ok(());
    }

    should_delete = Confirm::new("Last chance: Are you REALLY sure you want to delete ALL jobs?")
        .with_default(false)
        .prompt()?;

    if !should_delete {
        println!("Aborting removal");
        return Ok(());
    }

    // Ok now that we're really sure we want to delete this key
    database::delete_all_jobs(db).await?;

    println!("All jobs successfully removed");
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

    #[command(subcommand)]
    db_command: Option<DBCommand>,
}

#[derive(Debug, Subcommand)]
enum DBCommand {
    /// List all models in the database
    List(ListOpts),

    /// Add a model to the database
    Add(AddOpts),

    /// Remove a model from the database
    Remove(RemoveOpts),
}

#[derive(Debug, Parser)]
struct ListOpts {
    #[command(subcommand)]
    db_command: DBModelList,
}

#[derive(Debug, Parser)]
struct AddOpts {
    #[command(subcommand)]
    db_command: DBModelAdd,
}

#[derive(Debug, Parser)]
struct RemoveOpts {
    #[command(subcommand)]
    db_command: DBModelRemove,
}

#[derive(Debug, Subcommand)]
enum DBModelList {
    /// List all api keys
    ApiKey(NullOpts),

    /// List all local blob stores
    LocalBlobStore(NullOpts),
}

#[derive(Debug, Subcommand)]
enum DBModelAdd {
    /// Add an api key
    ApiKey(AddApiKeyOpts),

    /// Add a local blob store
    LocalBlobStore(AddLocalBlobStoreOpts),
}

#[derive(Debug, Subcommand)]
enum DBModelRemove {
    /// Remove an api key
    ApiKey(RemoveByIdOpts),

    /// Remove a local blob store
    LocalBlobStore(RemoveByIdOpts),

    /// DANGER Remove all cached jobs
    DangerJobsAll(NullOpts),
}

#[derive(Debug, Parser)]
struct NullOpts {}

#[derive(Debug, Parser)]
struct RemoveByIdOpts {
    #[arg(
        required = true,
        help = "The id of the model you want to remove",
        value_name = "ID"
    )]
    id: String,
}

#[derive(Debug, Parser)]
struct AddApiKeyOpts {
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
struct AddLocalBlobStoreOpts {
    #[arg(
        required = true,
        help = "The root directory for the store. Blobs will be saved to this location",
        value_name = "ROOT",
        long
    )]
    root: String,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Parse our arguments
    let args = TopLevel::parse();

    // Gather our config
    let config = config::RSCConfig::new(config::RSCConfigOverride {
        config_override: args.config_override,
        database_url: args.database_url,
        ..Default::default()
    })?;

    if args.show_config {
        println!("{}", serde_json::to_string(&config).unwrap());
        return Ok(());
    }

    // connect to our db
    let db = sea_orm::Database::connect(config.database_url).await?;
    let pending_migrations = Migrator::get_pending_migrations(&db).await?;
    if pending_migrations.len() != 0 {
        let err = Error::new(
            ErrorKind::Other,
            format!(
                "This rsc-tool version expects {:?} additional migrations to be applied",
                pending_migrations.len()
            ),
        );
        tracing::error! {%err, "unperformed migrations, please apply these migrations before using rsc-tool"};
        Err(err)?;
    }

    let Some(db_command) = args.db_command else {
        return Ok(());
    };

    match db_command {
        // List Commands
        DBCommand::List(ListOpts {
            db_command: DBModelList::ApiKey(_),
        }) => list_api_keys(&db).await?,
        DBCommand::List(ListOpts {
            db_command: DBModelList::LocalBlobStore(_),
        }) => list_local_blob_stores(&db).await?,

        // Add Commands
        DBCommand::Add(AddOpts {
            db_command: DBModelAdd::ApiKey(args),
        }) => add_api_key(args, &db).await?,
        DBCommand::Add(AddOpts {
            db_command: DBModelAdd::LocalBlobStore(args),
        }) => add_local_blob_store(args, &db).await?,

        // Remove Commands
        DBCommand::Remove(RemoveOpts {
            db_command: DBModelRemove::ApiKey(args),
        }) => remove_api_key(&args.id, &db).await?,
        DBCommand::Remove(RemoveOpts {
            db_command: DBModelRemove::LocalBlobStore(args),
        }) => remove_local_blob_store(&args.id, &db).await?,
        DBCommand::Remove(RemoveOpts {
            db_command: DBModelRemove::DangerJobsAll(_),
        }) => remove_all_jobs(&db).await?,
    }

    Ok(())
}
