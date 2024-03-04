#! /usr/bin/env bash
bin=`dirname "$0"`
bin=`cd "$bin"; pwd`
SRC_HOME=$bin/../test_cc_highpara
slaves=$bin/slaves
log_file=$bin/log
master_ip=fastswap_client_01
master_port=1231

kill(){
    for slave in `cat "$slaves"`
    do
		ip=`echo $slave | cut -d ' ' -f1`
		echo "ssh $ip	killall benchmark"
		ssh $ip	killall benchmark
    done # for slave
}


run() {
    echo "run for result_file=$result_file, 
    node=$node, thread=$thread, 
    remote_ratio=$remote_ratio, shared_ratio=$shared_ratio,
    read_ratio=$read_ratio, op_type=$op_type,
    space_locality=$space_locality, time_locality=$time_locality"
 
    old_IFS=$IFS
    IFS=$'\n'
    i=0
    for slave in `cat "$slaves"`
    do
    	ip=`echo $slave | cut -d ' ' -f1`
    	port=`echo $slave | cut -d ' ' -f2`
    	if [ $i = 0 ]; then
    		is_master=1
            master_ip=$ip
    	else
    		is_master=0
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
    done # for slave
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


# /home/zxy/DSM_prj/gam_20240222/scripts/../test_cc_highpara/benchmark --no_sys_thread 1 --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 1 --ip_master 10.0.0.7 --ip_worker 10.0.0.7 --no_node 3 --port_worker 1234 --port_master 1231 --cache_th 2 --result_file /home/zxy/DSM_prj/gam_20240103/scripts/results/read_ratio --no_thread 8 --remote_ratio 0 --shared_ratio 100 --read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2

run_my_test() {
# read ratio test
echo "**************************run sharing ratio test****************************"
result_file=$bin/results
# sys_thread_range="1 4 8 16"
sys_thread_range="8"
is_cache="0"
cache_rw="0"
is_request="0"
request_rw="0"


node_range="6"
# thread_range="4 8 16"
thread_range="8"
remote_range="0"
shared_range="100"
read_range="50"
space_range="0"
time_range="0"
op_range="2"
cache_th=2

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
}


kill
run_my_test

