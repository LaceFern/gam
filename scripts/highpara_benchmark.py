# please warning don't use sudo to run this scripts

import paramiko
import threading
import time
import sys

base = "/home/cxz/nfs/gam"

user = "cxz"

# whether rebuild from source code
init_build = True


master_machine = "192.168.189.7"
master_ip = "10.0.0.1"

requester_machine = "192.168.189.13"
requester_ip = "10.0.0.7"

cache_machine = "192.168.189.14"
cache_ip = "10.0.0.8"

# other_machine = []
# other_ip = []
other_machine = ["192.168.189.8", "192.168.189.9", "192.168.189.10", "192.168.189.11", "192.168.189.12"]
other_ip = ["10.0.0.2", "10.0.0.3", "10.0.0.4", "10.0.0.5", "10.0.0.6"]
# other_machine = ["192.168.189.8", "192.168.189.9", "192.168.189.10"]
# other_ip = ["10.0.0.2", "10.0.0.3", "10.0.0.4"]



output_directory = "/home/cxz/gam_result_tmp_8"

program_name = "highpara_benchmark"

bench_thread = [8]
sys_thread = [1,2,4,8]

# RLock is 0, READ_P2P is 4
request_type = 0

def make_and_clean(ssh, extra_flag=""):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}  && rm -rf ./build && mkdir build && cd build &&  cmake .. {1} && make -j".format(
            base,
            extra_flag))
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)
    sys.stdout.flush()


def create_dir(ssh, filename):
    stdin, stdout, stderr = ssh.exec_command(
        "mkdir -p $(dirname {0})".format(filename)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def master_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 1 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1234 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 0 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, master_ip, output_dir, bench_thread, node_num)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print("output for master")
    print(str1)
    print(str2)
    sys.stdout.flush()


def requester_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 0 --cache_rw 0 --is_request 1 --request_rw {8} --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1234 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 0 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, requester_ip, output_dir, bench_thread, node_num, request_type)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print("output for requester")
    print(str1)
    print(str2)
    sys.stdout.flush()

def cache_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 1 --cache_rw 1 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1234 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 0 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, cache_ip, output_dir, bench_thread, node_num)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print("output for cache")
    print(str1)
    print(str2)
    sys.stdout.flush()


def other_run(ssh, program, bench_thread, sys_thread, output_dir, node_num, self_ip):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1234 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 0 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, self_ip, output_dir, bench_thread, node_num)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    # print(str1)
    # print(str2)           


def ssh_connect(ip, user):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ip, 22, user, None, key_filename="/home/cxz/.ssh/id_ed25519")
    return ssh


if __name__ == '__main__':
    ssh_master = ssh_connect(master_machine, user)
    ssh_requester = ssh_connect(requester_machine, user)
    ssh_cache = ssh_connect(cache_machine, user)

    ssh_others = [ssh_connect(other_machine[i], user) for i in range(len(other_machine))]

    if init_build:
        t0 = threading.Thread(target=make_and_clean,
                                args=(ssh_master,))
        t0.start()
        t0.join()
    
    time.sleep(5)

    for b_i in bench_thread:
        for s_i in sys_thread:
            t1 = threading.Thread(target=master_run,
                                    args=(ssh_master, program_name, b_i, s_i, output_directory, 3+len(ssh_others)))

            t2 = threading.Thread(target=requester_run,
                                    args=(ssh_requester, program_name, b_i, s_i, output_directory, 3+len(ssh_others)))
            
            t3 = threading.Thread(target=cache_run,
                                    args=(ssh_cache, program_name, b_i, s_i, output_directory, 3+len(ssh_others)))

            tother_list = [threading.Thread(target=other_run,
                                    args=(ssh_others[i], program_name, b_i, s_i, output_directory, 3+len(ssh_others), other_ip[i]))  for i in range(len(ssh_others))]
            t1.start()
            time.sleep(2)
            t2.start()
            time.sleep(1)
            t3.start()
            for i in range(len(ssh_others)):
                tother_list[i].start()

            t1.join()
            t2.join()
            t3.join()
            for i in range(len(ssh_others)):
                tother_list[i].join()

            print("finish: bench_thread {} sys_thread {}\n".format(b_i, s_i))
            time.sleep(3)
