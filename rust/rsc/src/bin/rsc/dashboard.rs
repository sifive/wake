use crate::types::{DashboardStatsMostReusedJob, DashboardStatsOldestJob, DashboardStatsResponse};
use axum::Json;
use rsc::database;
use sea_orm::DatabaseConnection;
use std::sync::Arc;

#[tracing::instrument(skip_all)]
pub async fn stats(db: Arc<DatabaseConnection>) -> Json<DashboardStatsResponse> {
    let empty = DashboardStatsResponse {
        job_count: 0,
        blob_count: 0,
        size: 0,
        savings: 0,
        oldest_jobs: Vec::new(),
        most_reused_jobs: Vec::new(),
    };

    let job_count = match database::count_jobs(db.as_ref()).await {
        Ok(res) => res,
        Err(err) => {
            tracing::error! {%err, "Failed to lookup job count"};
            return Json(empty);
        }
    };

    let blob_count = match database::count_blobs(db.as_ref()).await {
        Ok(res) => res,
        Err(err) => {
            tracing::error! {%err, "Failed to lookup blob count"};
            return Json(empty);
        }
    };

    let size = match database::total_blob_size(db.as_ref()).await {
        Ok(Some(res)) => res.size,
        Ok(None) => {
            tracing::error! {"Failed to lookup total blob size"};
            return Json(empty);
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup total blob size"};
            return Json(empty);
        }
    };

    let savings = match database::time_saved(db.as_ref()).await {
        Ok(Some(res)) => res.savings,
        Ok(None) => {
            tracing::error! {"Failed to lookup cache savings"};
            return Json(empty);
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup cache savings"};
            return Json(empty);
        }
    };

    let oldest_jobs = match database::oldest_jobs(db.as_ref()).await {
        Ok(items) => {
            let mut out = Vec::new();
            for item in items {
                out.push(DashboardStatsOldestJob {
                    label: item.label,
                    created_at: item.created_at,
                    reuses: item.reuses,
                    savings: item.savings,
                });
            }
            out
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup oldest jobs"};
            return Json(empty);
        }
    };

    let most_reused_jobs = match database::most_reused_jobs(db.as_ref()).await {
        Ok(items) => {
            let mut out = Vec::new();
            for item in items {
                out.push(DashboardStatsMostReusedJob {
                    label: item.label,
                    reuses: item.reuses,
                    savings: item.savings,
                });
            }
            out
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup most reused jobs"};
            return Json(empty);
        }
    };

    Json(DashboardStatsResponse {
        job_count,
        blob_count,
        size,
        savings,
        oldest_jobs,
        most_reused_jobs,
    })
}
