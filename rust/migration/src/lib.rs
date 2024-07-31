pub use sea_orm_migration::prelude::*;

mod m20220101_000001_create_blob_tables;
mod m20220101_000002_create_table;
mod m20230706_104843_api_keys;
mod m20230707_144317_record_uses;
mod m20231117_162713_add_created_at;
mod m20231127_232833_drop_use_time;
mod m20231128_000751_normalize_uses_table;
mod m20240509_163905_add_label_to_job;
mod m20240517_195757_add_updated_at_to_blob;
mod m20240522_185420_create_job_history;
mod m20240731_152842_create_job_size_proc;

pub struct Migrator;

#[async_trait::async_trait]
impl MigratorTrait for Migrator {
    fn migrations() -> Vec<Box<dyn MigrationTrait>> {
        vec![
            Box::new(m20220101_000001_create_blob_tables::Migration),
            Box::new(m20220101_000002_create_table::Migration),
            Box::new(m20230706_104843_api_keys::Migration),
            Box::new(m20230707_144317_record_uses::Migration),
            Box::new(m20231117_162713_add_created_at::Migration),
            Box::new(m20231127_232833_drop_use_time::Migration),
            Box::new(m20231128_000751_normalize_uses_table::Migration),
            Box::new(m20240509_163905_add_label_to_job::Migration),
            Box::new(m20240517_195757_add_updated_at_to_blob::Migration),
            Box::new(m20240522_185420_create_job_history::Migration),
            Box::new(m20240731_152842_create_job_size_proc::Migration),
        ]
    }
}
