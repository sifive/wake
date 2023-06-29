use tower_lsp::lsp_types::*;

use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader, Lines};
use tokio::process::{ChildStdin, ChildStdout, Command};

use tower_lsp::lsp_types::notification::{
    DidChangeTextDocument, DidOpenTextDocument, Exit, Initialized, PublishDiagnostics,
};
use tower_lsp::lsp_types::request::{HoverRequest, Initialize, Shutdown};

use std::process::Stdio;

fn jrpc_serialize(req: tower_lsp::jsonrpc::Request) -> std::string::String {
    let str = req.to_string();
    let length = str.len();

    format!("Content-Length: {}\r\n\r\n{}", length, str)
}

fn make_request<R>(id: tower_lsp::jsonrpc::Id, params: R::Params) -> tower_lsp::jsonrpc::Request
where
    R: tower_lsp::lsp_types::request::Request,
    R::Params: serde::Serialize,
    R::Result: serde::de::DeserializeOwned,
{
    tower_lsp::jsonrpc::Request::build(R::METHOD)
        .id(id)
        .params(serde_json::to_value(params).expect(""))
        .finish()
}

async fn write_request<R>(
    stdin: &mut ChildStdin,
    id: tower_lsp::jsonrpc::Id,
    params: R::Params,
) -> Result<usize, std::io::Error>
where
    R: tower_lsp::lsp_types::request::Request,
    R::Params: serde::Serialize,
    R::Result: serde::de::DeserializeOwned,
{
    let req = make_request::<R>(id, params);
    stdin.write(jrpc_serialize(req).as_bytes()).await
}

fn make_notification<R>(params: R::Params) -> tower_lsp::jsonrpc::Request
where
    R: tower_lsp::lsp_types::notification::Notification,
    R::Params: serde::Serialize,
{
    tower_lsp::jsonrpc::Request::build(R::METHOD)
        .params(serde_json::to_value(params).expect(""))
        .finish()
}

async fn write_notification<R>(
    stdin: &mut ChildStdin,
    params: R::Params,
) -> Result<usize, std::io::Error>
where
    R: tower_lsp::lsp_types::notification::Notification,
    R::Params: serde::Serialize,
{
    let req = make_notification::<R>(params);
    stdin.write(jrpc_serialize(req).as_bytes()).await
}

async fn read_notification<R>(
    reader: &mut Lines<BufReader<ChildStdout>>,
) -> std::io::Result<R::Params>
where
    R: tower_lsp::lsp_types::notification::Notification,
    R::Params: serde::Serialize,
{
    let Some(_length) = reader.next_line().await? else {
        todo!();
    };
    let Some(_end_header) = reader.next_line().await? else {
        todo!();
    };
    let Some(json) = reader.next_line().await? else {
        todo!();
    };

    let res: tower_lsp::jsonrpc::Request = serde_json::from_str(&json).unwrap();
    let value = res.params().unwrap();
    let inner: R::Params = serde_json::from_value(value.clone()).unwrap();
    return Ok(inner);
}

async fn read_response<R>(reader: &mut Lines<BufReader<ChildStdout>>) -> std::io::Result<R::Result>
where
    R: tower_lsp::lsp_types::request::Request,
    R::Params: serde::Serialize,
    R::Result: serde::de::DeserializeOwned,
{
    let _length = reader.next_line().await?;
    let _end_header = reader.next_line().await?;
    let Some(json) = reader.next_line().await? else {
        todo!();
    };

    let res: tower_lsp::jsonrpc::Response = serde_json::from_str(&json).unwrap();
    let value = res.result().unwrap();
    let inner: R::Result = serde_json::from_value(value.clone()).unwrap();
    return Ok(inner);
}

async fn exchange_request_response<R>(
    stdin: &mut ChildStdin,
    reader: &mut Lines<BufReader<ChildStdout>>,
    id: tower_lsp::jsonrpc::Id,
    params: R::Params,
) -> std::io::Result<R::Result>
where
    R: tower_lsp::lsp_types::request::Request,
    R::Params: serde::Serialize,
    R::Result: serde::de::DeserializeOwned,
{
    write_request::<R>(stdin, id, params).await?;
    read_response::<R>(reader).await
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut child = Command::new("../../lib/wake/lsp-wake")
        .stdout(Stdio::piped())
        .stdin(Stdio::piped())
        .spawn()?;

    let mut stdin = child
        .stdin
        .take()
        .expect("failed to take child stdin handle");

    let stdout = child
        .stdout
        .take()
        .expect("failed to take child stdout handle");

    let mut reader = BufReader::new(stdout).lines();

    let Ok(blah) = std::env::current_dir() else {
        todo!();
    };
    let Some(cwd) = blah.to_str() else {
        todo!();
    };

    let res = exchange_request_response::<Initialize>(
        &mut stdin,
        &mut reader,
        tower_lsp::jsonrpc::Id::Number(5),
        InitializeParams {
            process_id: Some(5),
            root_path: None,
            root_uri: None,
            initialization_options: None,
            capabilities: ClientCapabilities {
                workspace: None,
                text_document: None,
                window: None,
                general: None,
                experimental: None,
            },
            trace: None,
            workspace_folders: Some(
                [WorkspaceFolder {
                    uri: Url::parse(format!("file://{}", cwd).as_str())?,
                    name: "wake".to_string(),
                }]
                .to_vec(),
            ),
            client_info: Some(ClientInfo {
                name: "Neovim".to_string(),
                version: Some("0.8.0".to_string()),
            }),
            locale: None,
        },
    )
    .await?;

    println!("{:?}", res);

    write_notification::<Initialized>(&mut stdin, InitializedParams {}).await?;

    for n in 1..28 {
        let res = read_notification::<PublishDiagnostics>(&mut reader).await?;
        println!("{}: {:?}", n, res);
    }

    let test_wake = Url::parse(format!("file://{}/test.wake", cwd).as_str())?;

    write_notification::<DidOpenTextDocument>(
        &mut stdin,
        DidOpenTextDocumentParams {
            text_document: TextDocumentItem {
                uri: test_wake.clone(),
                language_id: "wake".to_string(),
                version: 1,
                text: "# comment\n# comment \n\ndef unused = 5".to_string(),
            },
        },
    )
    .await?;

    write_notification::<DidChangeTextDocument>(
        &mut stdin,
        DidChangeTextDocumentParams {
            text_document: VersionedTextDocumentIdentifier {
                uri: test_wake.clone(),
                version: 1,
            },
            content_changes: [TextDocumentContentChangeEvent {
                range: None,
                range_length: None,
                text: "# comment\n# comment \n\ndef y = 6".to_string(),
            }]
            .to_vec(),
        },
    )
    .await?;

    let hover_params = HoverParams {
        text_document_position_params: TextDocumentPositionParams {
            text_document: TextDocumentIdentifier {
                uri: test_wake.clone(),
            },
            position: Position {
                line: 0,
                character: 0,
            },
        },
        work_done_progress_params: WorkDoneProgressParams {
            work_done_token: None,
        },
    };

    let res = exchange_request_response::<HoverRequest>(
        &mut stdin,
        &mut reader,
        tower_lsp::jsonrpc::Id::Number(6),
        hover_params.clone(),
    )
    .await?;
    println!("{:?}", res);

    let res = exchange_request_response::<HoverRequest>(
        &mut stdin,
        &mut reader,
        tower_lsp::jsonrpc::Id::Number(7),
        hover_params.clone(),
    )
    .await?;
    println!("{:?}", res);

    write_request::<HoverRequest>(
        &mut stdin,
        tower_lsp::jsonrpc::Id::Number(8),
        hover_params.clone(),
    )
    .await?;

    for n in 1..28 {
        let res = read_notification::<PublishDiagnostics>(&mut reader).await?;
        println!("{}: {:?}", n, res);
    }

    let res = read_response::<HoverRequest>(&mut reader).await?;
    println!("{:?}", res);

    let res = exchange_request_response::<Shutdown>(
        &mut stdin,
        &mut reader,
        tower_lsp::jsonrpc::Id::Number(9),
        (),
    )
    .await?;
    println!("{:?}", res);

    write_notification::<Exit>(&mut stdin, ()).await?;

    let status = child.wait().await?;
    println!("child exit: {}", status);

    Ok(())
}
