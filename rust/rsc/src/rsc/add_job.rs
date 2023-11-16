use axum::{http::StatusCode, Json};
use entity::prelude::{OutputDir, OutputFile, OutputSymlink, VisibleFile};
use entity::{job, output_dir, output_file, output_symlink, visible_file};
use sea_orm::{
    ActiveModelTrait, ActiveValue, DatabaseConnection, DbErr, EntityTrait, TransactionTrait,
};
use std::sync::Arc;
use tracing;

use crate::types::AddJobPayload;

#[tracing::instrument]
pub async fn add_job(
    Json(payload): Json<AddJobPayload>,
    conn: Arc<DatabaseConnection>,
) -> StatusCode {
    // First construct all the job details as an ActiveModel for insert
    let hash = payload.hash();
    let vis = payload.visible_files;
    let output_files = payload.output_files;
    let output_symlinks = payload.output_symlinks;
    let output_dirs = payload.output_dirs;
    let insert_job = job::ActiveModel {
        id: ActiveValue::NotSet,
        hash: ActiveValue::Set(hash.clone().into()),
        cmd: ActiveValue::Set(payload.cmd),
        env: ActiveValue::Set(payload.env.as_bytes().into()),
        cwd: ActiveValue::Set(payload.cwd),
        stdin: ActiveValue::Set(payload.stdin),
        is_atty: ActiveValue::Set(payload.is_atty),
        hidden_info: ActiveValue::Set(payload.hidden_info),
        stdout: ActiveValue::Set(payload.stdout),
        stderr: ActiveValue::Set(payload.stderr),
        status: ActiveValue::Set(payload.status),
        runtime: ActiveValue::Set(payload.runtime),
        cputime: ActiveValue::Set(payload.cputime),
        memory: ActiveValue::Set(payload.memory as i64),
        i_bytes: ActiveValue::Set(payload.ibytes as i64),
        o_bytes: ActiveValue::Set(payload.obytes as i64),
    };

    // Now perform the insert as a single transaction
    let insert_result = conn
        .as_ref()
        .transaction::<_, (), DbErr>(|txn| {
            Box::pin(async move {
                let job = insert_job.save(txn).await?;
                let job_id = job.id.unwrap();

                let mut visible_files = Vec::new();
                for vis_file in vis {
                    visible_files.push(visible_file::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(vis_file.path),
                        hash: ActiveValue::Set(vis_file.hash.into()),
                        job_id: ActiveValue::Set(job_id),
                    });
                }

                VisibleFile::insert_many(visible_files)
                    .on_empty_do_nothing()
                    .exec(txn)
                    .await?;

                let mut out_files = Vec::new();
                for out_file in output_files {
                    out_files.push(output_file::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(out_file.path),
                        hash: ActiveValue::Set(out_file.hash.into()),
                        mode: ActiveValue::Set(out_file.mode),
                        job_id: ActiveValue::Set(job_id),
                    });
                }

                OutputFile::insert_many(out_files)
                    .on_empty_do_nothing()
                    .exec(txn)
                    .await?;

                let mut out_symlinks = Vec::new();
                for out_symlink in output_symlinks {
                    out_symlinks.push(output_symlink::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(out_symlink.path),
                        content: ActiveValue::Set(out_symlink.content),
                        job_id: ActiveValue::Set(job_id),
                    });
                }

                OutputSymlink::insert_many(out_symlinks)
                    .on_empty_do_nothing()
                    .exec(txn)
                    .await?;

                let mut dirs = Vec::new();
                for dir in output_dirs {
                    dirs.push(output_dir::ActiveModel {
                        id: ActiveValue::NotSet,
                        path: ActiveValue::Set(dir.path),
                        mode: ActiveValue::Set(dir.mode),
                        job_id: ActiveValue::Set(job_id),
                    });
                }

                OutputDir::insert_many(dirs)
                    .on_empty_do_nothing()
                    .exec(txn)
                    .await?;

                Ok(())
            })
        })
        .await;
    match insert_result {
        Ok(_) => StatusCode::OK,
        // TODO: We should returnn 500 on some errors
        //       but 4** when the user trys to add an invalid job
        Err(cause) => {
            tracing::error! {
              %cause,
              "failed to add job"
            };
            StatusCode::BAD_REQUEST
        }
    }
}
