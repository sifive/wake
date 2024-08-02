use crate::types::{
    DashboardStatsBlobUseByStore, DashboardStatsLostOpportunityJob, DashboardStatsMostReusedJob,
    DashboardStatsOldestJob, DashboardStatsResponse, DashboardStatsSizeRuntimeValueJob,
};
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
        most_time_saved_jobs: Vec::new(),
        lost_opportunity_jobs: Vec::new(),
        most_space_efficient_jobs: Vec::new(),
        most_space_use_jobs: Vec::new(),
        blob_use_by_store: Vec::new(),
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

    let most_time_saved_jobs = match database::most_time_saved_jobs(db.as_ref()).await {
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
            tracing::error! {%err, "Failed to lookup most time saved jobs"};
            return Json(empty);
        }
    };

    let lost_opportunity_jobs = match database::lost_opportuinty_jobs(db.as_ref()).await {
        Ok(items) => {
            let mut out = Vec::new();
            for item in items {
                out.push(DashboardStatsLostOpportunityJob {
                    label: item.label,
                    reuses: item.reuses,
                    misses: item.misses,
                    real_savings: item.real_savings,
                    lost_savings: item.lost_savings,
                    potential_savings: item.potential_savings,
                });
            }
            out
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup lost opportunity jobs"};
            return Json(empty);
        }
    };

    let most_space_efficient_jobs = match database::most_space_efficient_jobs(db.as_ref()).await {
        Ok(items) => {
            let mut out = Vec::new();
            for item in items {
                out.push(DashboardStatsSizeRuntimeValueJob {
                    label: item.label,
                    runtime: item.runtime,
                    disk_usage: item.disk_usage,
                    ns_saved_per_byte: item.ns_saved_per_byte,
                });
            }
            out
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup most space efficient jobs"};
            return Json(empty);
        }
    };

    let most_space_use_jobs = match database::most_space_use_jobs(db.as_ref()).await {
        Ok(items) => {
            let mut out = Vec::new();
            for item in items {
                out.push(DashboardStatsSizeRuntimeValueJob {
                    label: item.label,
                    runtime: item.runtime,
                    disk_usage: item.disk_usage,
                    ns_saved_per_byte: item.ns_saved_per_byte,
                });
            }
            out
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup most space use jobs"};
            return Json(empty);
        }
    };

    let blob_use_by_store = match database::blob_use_by_store(db.as_ref()).await {
        Ok(items) => {
            let mut out = Vec::new();
            for item in items {
                out.push(DashboardStatsBlobUseByStore {
                    store_id: item.store_id.to_string(),
                    store_type: item.store_type,
                    refs: item.refs,
                    blob_count: item.blob_count,
                });
            }
            out
        }
        Err(err) => {
            tracing::error! {%err, "Failed to lookup blob use by store"};
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
        most_time_saved_jobs,
        lost_opportunity_jobs,
        most_space_efficient_jobs,
        most_space_use_jobs,
        blob_use_by_store,
    })
}
