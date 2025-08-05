#!/bin/bash
set -o nounset

# specify your hosts_file here 
# hosts_file specify a list of host names and port numbers, with the host names in the first column
hosts_file="../tpcc/config.txt"
# specify your directory for log files
output_dir="/home/tzr/logs"
# create the output directory if it does not exist
mkdir -p ${output_dir}

# working environment
proj_dir="/home/tzr/nfs/GAM"
bin_dir="${proj_dir}/build"
script_dir="${proj_dir}/database/scripts"
ssh_opts="-o StrictHostKeyChecking=no"
bin_file=hash_index_test

hosts_list=`./get_servers.sh ${hosts_file} | tr "\\n" " "`
hosts=(`echo ${hosts_list}`)
master_host=${hosts[0]}

USER_ARGS="$@"
echo "input Arguments: ${USER_ARGS}"
echo "launch..."

# run_test () {
#   output_file="${output_dir}/${bin_file}-c4.log"
#   script="cd ${bin_dir} && ./${bin_file} ${USER_ARGS} > ${output_file} 2>&1"
  
#   echo "start master: ssh ${ssh_opts} ${master_host} "$script" &"
#   ssh ${ssh_opts} ${master_host} "$script" &
#   sleep 3
#   for ((i=1;i<${#hosts[@]};i++)); do
#     host=${hosts[$i]}
#     echo "start worker: ssh ${ssh_opts} ${host} "$script" &"
#     ssh ${ssh_opts} ${host} "$script" &
#     sleep 1
#   done
#   wait
# }

run_test () {
  local d_value=$1

  local USER_ARGS="-p11111 -sf64 -sf10 -c4 -t200000 -d${d_value} -f../database/tpcc/config.txt"
  
  output_dir=${output_dir:-.}
  output_file="${output_dir}/${bin_file}-c12-d${d_value}-r30.log"
  
  script="cd ${bin_dir} && ./${bin_file} ${USER_ARGS} > ${output_file} 2>&1"
  
  for ((i=1;i<${#hosts[@]};i++)); do
    host=${hosts[$i]}
    echo "start worker: ssh ${ssh_opts} ${host} \"$script\" &"
    ssh ${ssh_opts} ${host} "$script" &
    sleep 1
  done

  echo "start master: ssh ${ssh_opts} ${master_host} \"$script\" &"
  ssh ${ssh_opts} ${master_host} "$script" 
  
  wait
  echo "--- Run with -d ${d_value} finished. Log: ${output_file} ---"
}

arp (){
  for ((i=1;i<${#hosts[@]};i++)); do
    host=${hosts[$i]}
    arp_script="cd ${proj_dir} && sudo bash ./arp-${host}.sh"
    echo "start worker: ssh ${ssh_opts} ${host} "$arp_script" &"
    ssh ${ssh_opts} ${host} "$arp_script" &
    sleep 1
  done
}

auto_fill_params () {
  # so that users don't need to specify parameters for themselves
  USER_ARGS="-p11111 -sf32 -sf10 -c4 -t200000 -f../database/tpcc/config.txt"
}

# auto_fill_params
arp
# bin_file=hash_index_test
# run_test
bin_file=tpcc
for d_val in 0; do
    
    run_test "${d_val}" 
    
    # (可选) 在两次不同的-d测试之间短暂暂停
    sleep 2
    ${script_dir}/kill_servers.sh
    sleep 2
done
