#! /usr/bin/env bash
bin=`dirname "$0"`
bin=`cd "$bin"; pwd`
SRC_HOME=$bin/../test
slaves=$bin/slaves
log_file=$bin/log
master_ip=fastswap_client_01
master_port=1231

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
    	echo "$SRC_HOME/benchmark --cache_th $cache_th --op_type $op_type --no_node $node --no_thread $thread --remote_ratio $remote_ratio --shared_ratio $shared_ratio --read_ratio $read_ratio --space_locality $space_locality --time_locality $time_locality --result_file $result_file --ip_master $master_ip --ip_worker $ip --port_worker $port --is_master $is_master --port_master $master_port" | tee -a "$log_file".$ip
    	# ssh $ip	"$SRC_HOME/benchmark --cache_th $cache_th --op_type $op_type --no_node $node --no_thread $thread --remote_ratio $remote_ratio --shared_ratio $shared_ratio --read_ratio $read_ratio --space_locality $space_locality --time_locality $time_locality --result_file "$result_file" --ip_master $master_ip --ip_worker $ip --port_worker $port --is_master $is_master --port_master $master_port | tee -a '$log_file'.$ip" &
		ssh $ip	"$SRC_HOME/benchmark --cache_th $cache_th --op_type $op_type --no_node $node --no_thread $thread --remote_ratio $remote_ratio --shared_ratio $shared_ratio --read_ratio $read_ratio --space_locality $space_locality --time_locality $time_locality --result_file "$result_file" --ip_master $master_ip --ip_worker $ip --port_worker $port --is_master $is_master --port_master $master_port" &
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




run_my_test() {
# read ratio test
echo "**************************run sharing ratio test****************************"
result_file=$bin/results/read_ratio
node_range="2"
thread_range="4"
remote_range="0"
shared_range="10 20 30 40 50 60 70 80 90 100"
read_range="50"
space_range="0"
time_range="0"
op_range="2"
cache_th=0.5

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
}



run_my_test
