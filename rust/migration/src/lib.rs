pub use sea_orm_migration::prelude::*;

mod m20220101_000001_create_table;
mod m20230706_104843_api_keys;
mod m20230707_144317_record_uses;
mod m20231117_162713_add_created_at;

pub struct Migrator;

#[async_trait::async_trait]
impl MigratorTrait for Migrator {
    fn migrations() -> Vec<Box<dyn MigrationTrait>> {
        vec![
            Box::new(m20220101_000001_create_table::Migration),
            Box::new(m20230706_104843_api_keys::Migration),
            Box::new(m20230707_144317_record_uses::Migration),
            Box::new(m20231117_162713_add_created_at::Migration),
        ]
    }
}
