        set -ex
        echo HOME: $HOME
        echo USER: $USER
        unshare -U
		set -ex
        export HOME=/root
        echo HOME: $HOME
        echo USER: $USER
        mkdir /tmp/pg_data
        /scratch/jakee/pg-server2/bin/initdb -D /tmp/pg_data
        /scratch/jakee/pg-server2/bin/postgres -D /tmp/pg_data &
        trap "kill $!" 0
        sleep 2s
        /scratch/jakee/pg-server2/bin/createuser -h localhost root
        /scratch/jakee/pg-server2/bin/createdb -h localhost shim
        cd rust/rsc
        /sifive/tools/rust/stable/1.72.0/bin/cargo test