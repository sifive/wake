use crate::types::AddJobPayload;
use axum::{http::StatusCode, Json};
use entity::prelude::{OutputDir, OutputFile, OutputSymlink, VisibleFile};
use entity::{job, output_dir, output_file, output_symlink, visible_file};
use sea_orm::{
    ActiveModelTrait, ActiveValue::*, DatabaseConnection, DbErr, EntityTrait, TransactionTrait,
};
use std::sync::Arc;
use tracing;

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
        id: NotSet,
        hash: Set(hash.clone().into()),
        cmd: Set(payload.cmd),
        env: Set(payload.env.as_bytes().into()),
        cwd: Set(payload.cwd),
        stdin: Set(payload.stdin),
        is_atty: Set(payload.is_atty),
        hidden_info: Set(payload.hidden_info),
        stdout: Set(payload.stdout),
        stderr: Set(payload.stderr),
        status: Set(payload.status),
        runtime: Set(payload.runtime),
        cputime: Set(payload.cputime),
        memory: Set(payload.memory as i64),
        i_bytes: Set(payload.ibytes as i64),
        o_bytes: Set(payload.obytes as i64),
    };

    // Now perform the insert as a single transaction
    let insert_result = conn
        .as_ref()
        .transaction::<_, (), DbErr>(|txn| {
            Box::pin(async move {
                let job = insert_job.save(txn).await?;
                let job_id = job.id.unwrap();

                let visible_files = vis.into_iter().map(|vis_file| visible_file::ActiveModel {
                    id: NotSet,
                    path: Set(vis_file.path),
                    hash: Set(vis_file.hash.into()),
                    job_id: Set(job_id),
                });

                VisibleFile::insert_many(visible_files)
                    .on_empty_do_nothing()
                    .exec(txn)
                    .await?;

                let out_files = output_files
                    .into_iter()
                    .map(|out_file| output_file::ActiveModel {
                        id: NotSet,
                        path: Set(out_file.path),
                        hash: Set(out_file.hash.into()),
                        mode: Set(out_file.mode),
                        job_id: Set(job_id),
                    });

                OutputFile::insert_many(out_files)
                    .on_empty_do_nothing()
                    .exec(txn)
                    .await?;

                let out_symlinks =
                    output_symlinks
                        .into_iter()
                        .map(|out_symlink| output_symlink::ActiveModel {
                            id: NotSet,
                            path: Set(out_symlink.path),
                            content: Set(out_symlink.content),
                            job_id: Set(job_id),
                        });

                OutputSymlink::insert_many(out_symlinks)
                    .on_empty_do_nothing()
                    .exec(txn)
                    .await?;

                let dirs = output_dirs.into_iter().map(|dir| output_dir::ActiveModel {
                    id: NotSet,
                    path: Set(dir.path),
                    mode: Set(dir.mode),
                    job_id: Set(job_id),
                });

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
