#!/bin/bash

WAKE="${1:+$1/wake}"

lsp_in=/tmp/lsp_in_pipe
lsp_out=/tmp/lsp_out_pipe

# Create the pipes
mkfifo $lsp_in
mkfifo $lsp_out

# Cleanup on exit
function cleanup ()  { rm -f $lsp_in; rm -f $lsp_out; }
trap cleanup EXIT

# Launch wakelsp and pass through in/out pipes
$WAKE --lsp < $lsp_in > $lsp_out &    # send data into wakelsp

cat ../msg/initialize.json > $lsp_in
cat ../msg/initialized.json > $lsp_in
cat ../msg/cancel.json > $lsp_in
cat ../msg/shutdown.json > $lsp_in
cat ../msg/exit.json > $lsp_in

cat - < $lsp_out
