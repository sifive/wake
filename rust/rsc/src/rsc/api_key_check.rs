use axum::{
    http::{header::AUTHORIZATION, Request, StatusCode},
    middleware::Next,
    response::{IntoResponse, Response},
};
use entity::api_key;
use sea_orm::{ColumnTrait, DatabaseConnection, EntityTrait, QueryFilter};
use std::sync::Arc;

fn unauth() -> Response {
    StatusCode::UNAUTHORIZED.into_response()
}

// NOTE: This is not secure, its just to prevent otherwise trusted
//       users from polluting the cache.
pub async fn api_key_check_middleware<B>(
    request: Request<B>,
    next: Next<B>,
    conn: Arc<DatabaseConnection>,
) -> Response {
    let headers = request.headers();

    // First get the key, if the user doesn't provide
    // a key give up on them.
    let Some(key) = headers.get(AUTHORIZATION) else {
        return unauth();
    };

    // Convert the key to unicode if possible
    let Ok(key) = key.to_str() else {
        return unauth();
    };

    // Next check the database for a key
    let Ok(Some(_)) = api_key::Entity::find()
        .filter(api_key::Column::Key.eq(key))
        .one(conn.as_ref())
        .await
    else {
        return unauth();
    };

    // Ok they're all good, run their request
    next.run(request).await
}
