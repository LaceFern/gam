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


run() {
    # echo "run for result_file=$result_file, 
    # node=$node, thread=$thread, 
    # remote_ratio=$remote_ratio, shared_ratio=$shared_ratio,
    # read_ratio=$read_ratio, op_type=$op_type,
    # space_locality=$space_locality, time_locality=$time_locality"
 
    # i=0
	# k=0
    # while IFS= read -r line; do
	# 	slave=$(echo "$line")
	# 	ip=$(echo "$line" | cut -d ' ' -f1)
    # 	port=$(echo "$line" | cut -d ' ' -f2)
	# it seems that uses while IFS= read -r line; will stack ssh returns  

    echo "run for result_file=$result_file, 
    node=$node, thread=$thread, 
    remote_ratio=$remote_ratio, shared_ratio=$shared_ratio,
    read_ratio=$read_ratio, op_type=$op_type,
    space_locality=$space_locality, time_locality=$time_locality"
 
    old_IFS=$IFS
    IFS=$'\n'
    i=0
	k=0
    for slave in `cat "$slaves"`
    do
    	ip=`echo $slave | cut -d ' ' -f1`
    	port=`echo $slave | cut -d ' ' -f2`

		is_cache="0"
		cache_rw="0"
		is_request="0"
		request_rw="0"

		echo "$target_CCtxn"
		
		IFS=' '
		read -r -a target_CCtxn_plt <<< "$target_CCtxn"
		# echo "${target_CCtxn_plt[0]}"
		# echo "${target_CCtxn_plt[1]}"
		# echo "${target_CCtxn_plt[2]}"
		# echo "${target_CCtxn_plt[3]}"

    	if [ $i = 0 ]; then
    		is_master=1
            master_ip=$ip

			if [ "${target_CCtxn_plt[0]}" = "0" ]; then
				is_request="1"
				request_rw="${target_CCtxn_plt[1]}"
			fi
    	else
    		is_master=0
			if [ "$k" = "0" ] && [ "${target_CCtxn_plt[0]}" = "1" ]; then
				is_request="1"
				request_rw="${target_CCtxn_plt[1]}"
				k=$((k+1))
			else
				if [ "$k" = "1" ] && [ "${target_CCtxn_plt[2]}" = "1" ]; then
					is_cache="1"
					cache_rw="${target_CCtxn_plt[3]}"
					k=$((k+1))
				fi
			fi
    	fi

    	if [ $port == $ip ]; then
    		port=1234
    	fi
    	echo ""
    	echo "slave = $slave, ip = $ip, port = $port"
    	echo "$SRC_HOME/benchmark --no_sys_thread $no_sys_thread --is_cache $is_cache --cache_rw $cache_rw --is_request $is_request --request_rw $request_rw --is_master $is_master --ip_master $master_ip --ip_worker $ip --no_node $node --port_worker $port --port_master $master_port --cache_th $cache_th --result_file $result_file --no_thread $thread --remote_ratio $remote_ratio --shared_ratio $shared_ratio --read_ratio $read_ratio --space_locality $space_locality --time_locality $time_locality --op_type $op_type"
		ssh $ip	"$SRC_HOME/benchmark --no_sys_thread $no_sys_thread --is_cache $is_cache --cache_rw $cache_rw --is_request $is_request --request_rw $request_rw --is_master $is_master --ip_master $master_ip --ip_worker $ip --no_node $node --port_worker $port --port_master $master_port --cache_th $cache_th --result_file $result_file --no_thread $thread --remote_ratio $remote_ratio --shared_ratio $shared_ratio --read_ratio $read_ratio --space_locality $space_locality --time_locality $time_locality --op_type $op_type" & 
    	sleep 1
    	i=$((i+1))
    	if [ "$i" = "$node" ]; then
    		break
    	fi
    done
	wait
	j=0
	for slave in `cat $slaves`
	do
		ip=`echo $slave | cut -d ' ' -f1`
		ssh $ip killall benchmark > /dev/null 2>&1
		j=$((j+1))
		if [ $j = $node ]; then
			break;
		fi
	done
	IFS="$old_IFS"
}



run_my_test() {
# read ratio test
echo "**************************run sharing ratio test****************************"
result_file=$bin/results_20240311

#if request doesn't equal master, request_rw; if cache exists, cache_rw (0r,1w)
# (1 0 1 2)
target_CCtxns=("1 0 1 1")

# sys_thread_range="1 4 8 16"
sys_thread_range="4"

node_range="8"
# thread_range="4 8 16"
thread_range="8 16 32"
remote_range="0"
shared_range="100"
read_range="50"
space_range="0"
time_range="0"
op_range="2"
cache_th=2


for target_CCtxn in "${target_CCtxns[@]}"
do
for no_sys_thread in $sys_thread_range
do
for remote_ratio in $remote_range
do
for op_type in $op_range
do
for space_locality in $space_range
do
for time_locality in $time_range
do
for node in $node_range
do
for thread in $thread_range
do
for shared_ratio in $shared_range
do
for read_ratio in $read_range
do
	if [[ $remote_ratio -gt 0 && $node = 1 ]]; then
		continue;
	fi
    run
done
done
done
done
done
done
done
done
done
done
}


kill
run_my_test