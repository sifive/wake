use crate::types::AddJobPayload;
use axum::{http::StatusCode, Json};
use entity::prelude::{OutputDir, OutputFile, OutputSymlink, VisibleFile};
use entity::{job, output_dir, output_file, output_symlink, visible_file};
use itertools::Itertools;
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
        created_at: NotSet,
        hash: Set(hash.clone().into()),
        cmd: Set(payload.cmd),
        env: Set(payload.env),
        cwd: Set(payload.cwd),
        stdin: Set(payload.stdin),
        is_atty: Set(payload.is_atty),
        hidden_info: Set(payload.hidden_info),
        stdout_blob_id: Set(payload.stdout_blob_id),
        stderr_blob_id: Set(payload.stderr_blob_id),
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

                // sqlx only allows 65635 sql parameters per query and each visible file accounts
                // for 5 parameters so we can only insert 65635/5 visible files per query
                let chunked_visible_files: Vec<Vec<visible_file::ActiveModel>> = vis
                    .into_iter()
                    .map(|vis_file| visible_file::ActiveModel {
                        id: NotSet,
                        created_at: NotSet,
                        path: Set(vis_file.path),
                        hash: Set(vis_file.hash),
                        job_id: Set(job_id),
                    })
                    .chunks(65635 / 5)
                    .into_iter()
                    .map(|chunk| chunk.collect())
                    .collect();

                for chunk in chunked_visible_files {
                    VisibleFile::insert_many(chunk)
                        .on_empty_do_nothing()
                        .exec(txn)
                        .await?;
                }

                // sqlx only allows 65635 sql parameters per query and each output file accounts
                // for 6 parameters so we can only insert 65635/6 visible files per query
                let chunked_output_files: Vec<Vec<output_file::ActiveModel>> = output_files
                    .into_iter()
                    .map(|out_file| output_file::ActiveModel {
                        id: NotSet,
                        created_at: NotSet,
                        path: Set(out_file.path),
                        mode: Set(out_file.mode),
                        job_id: Set(job_id),
                        blob_id: Set(out_file.blob_id),
                    })
                    .chunks(65635 / 6)
                    .into_iter()
                    .map(|chunk| chunk.collect())
                    .collect();

                for chunk in chunked_output_files {
                    OutputFile::insert_many(chunk)
                        .on_empty_do_nothing()
                        .exec(txn)
                        .await?;
                }

                // sqlx only allows 65635 sql parameters per query and each output symlink accounts
                // for 5 parameters so we can only insert 65635/5 visible files per query
                let chunked_output_symlinks: Vec<Vec<output_symlink::ActiveModel>> =
                    output_symlinks
                        .into_iter()
                        .map(|out_symlink| output_symlink::ActiveModel {
                            id: NotSet,
                            created_at: NotSet,
                            path: Set(out_symlink.path),
                            link: Set(out_symlink.link),
                            job_id: Set(job_id),
                        })
                        .chunks(65635 / 5)
                        .into_iter()
                        .map(|chunk| chunk.collect())
                        .collect();

                for chunk in chunked_output_symlinks {
                    OutputSymlink::insert_many(chunk)
                        .on_empty_do_nothing()
                        .exec(txn)
                        .await?;
                }

                // sqlx only allows 65635 sql parameters per query and each output dir accounts
                // for 5 parameters so we can only insert 65635/5 visible files per query
                let chunked_output_dirs: Vec<Vec<output_dir::ActiveModel>> = output_dirs
                    .into_iter()
                    .map(|dir| output_dir::ActiveModel {
                        id: NotSet,
                        created_at: NotSet,
                        path: Set(dir.path),
                        mode: Set(dir.mode),
                        job_id: Set(job_id),
                    })
                    .chunks(65635 / 5)
                    .into_iter()
                    .map(|chunk| chunk.collect())
                    .collect();

                for chunk in chunked_output_dirs {
                    OutputDir::insert_many(chunk)
                        .on_empty_do_nothing()
                        .exec(txn)
                        .await?;
                }

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
