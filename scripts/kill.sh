#! /usr/bin/env bash
bin=`dirname "$0"`
bin=`cd "$bin"; pwd`
SRC_HOME=$bin/../test_cc_highpara
slaves=$bin/slaves
log_file=$bin/log
master_ip=fastswap_client_01
master_port=1231

kill(){
	while IFS= read -r line; do
		ip=$(echo "$line" | cut -d ' ' -f1)
    	port=$(echo "$line" | cut -d ' ' -f2)
    	echo "$line $ip $port"
    	ssh "$ip" "killall benchmark" &
	done < "$slaves"
	wait
}

kill

