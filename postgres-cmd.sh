set -ex
echo $SHELL
echo $USER
mkdir $DATA_DIR
$PG_DIR/bin/initdb -D $DATA_DIR
$PG_DIR/bin/postgres -D $DATA_DIR &
sleep 2s
$PG_DIR/bin/createuser -h localhost root
$PG_DIR/bin/createdb -h localhost nominal_test
$PG_DIR/bin/createdb -h localhost ttl_eviction
cd rust/rsc
cargo test
STATUS=$?
killall postgres
echo $STATUS