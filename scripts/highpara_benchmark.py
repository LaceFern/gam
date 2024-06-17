# please warning don't use sudo to run this scripts

import paramiko
import threading
import time
import sys

base = "/home/zxy/nfs/DSM_prj/gam_cxz"

user = "zxy"

# whether rebuild from source code
init_build = True # True False

# attention!
# centralized dir: master_ip/machine = home_ip/machine, remote ratio = 0
# distributed dir: remote ratio = 88

# 8 machines
# master_machine = "192.168.189.7"
# master_ip = "10.0.0.1"

# requester_machine = "192.168.189.9"
# requester_ip = "10.0.0.3"

# cache_machine = "192.168.189.10"
# cache_ip = "10.0.0.4"

# home_machine = "192.168.189.11"
# home_ip = "10.0.0.5"

# other_machine = ["192.168.189.8", "192.168.189.12", "192.168.189.13", "192.168.189.14"]
# other_ip = ["10.0.0.2", "10.0.0.6", "10.0.0.7", "10.0.0.8"]

# 4 machines
master_machine = "192.168.189.10"
master_ip = "10.0.0.4"

requester_machine = "192.168.189.11"
requester_ip = "10.0.0.5"

cache_machine = "192.168.189.12"
cache_ip = "10.0.0.6"

home_machine = "192.168.189.13"
home_ip = "10.0.0.7"
other_machine = []
other_ip = []

output_directory = "/home/zxy/gam_result_cxz_7"

program_name = "highpara_benchmark"

# bench_thread = [24]
# sys_thread = [1, 2, 4, 8]
bench_thread = [1]
sys_thread = [1]

# RLock is 0, WLock is 1, READ_P2P is 4
request_type = 1

# Attention: if need to test latency under low throughput, replace Run_request() with Run_request_only() in Benchmark(); change breakdown_times from 1024 to 204800


def make_and_clean(ssh, extra_flag=""):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0} && cd build &&  cmake .. {1} && make -j".format(
            base,
            extra_flag))
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)
    sys.stdout.flush()


def create_dir(ssh, filename):
    stdin, stdout, stderr = ssh.exec_command(
        "mkdir {0}".format(filename)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)

def master_home_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    print("cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 1 --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 1 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, master_ip, output_dir, bench_thread, node_num))
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 1 --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 1 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, master_ip, output_dir, bench_thread, node_num)
    )
    # str1 = stdout.read().decode('utf-8')
    # str2 = stderr.read().decode('utf-8')
    # print("output for master")
    # print(str1)
    # print(str2)
    # sys.stdout.flush()

    while not stdout.channel.exit_status_ready():
        result = stdout.readline()
        print(result)
        if stdout.channel.exit_status_ready():
            a = stdout.readlines()
            print(a)
            break

def master_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    print("cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 0 --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 1 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, master_ip, output_dir, bench_thread, node_num))
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 0 --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 1 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, master_ip, output_dir, bench_thread, node_num)
    )
    # str1 = stdout.read().decode('utf-8')
    # str2 = stderr.read().decode('utf-8')
    # print("output for master")
    # print(str1)
    # print(str2)
    # sys.stdout.flush()

    while not stdout.channel.exit_status_ready():
        result = stdout.readline()
        print(result)
        if stdout.channel.exit_status_ready():
            a = stdout.readlines()
            print(a)
            break


def requester_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    print("cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 0 --is_cache 0 --cache_rw 0 --is_request 1 --request_rw {8} --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, requester_ip, output_dir, bench_thread, node_num, request_type))
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 0 --is_cache 0 --cache_rw 0 --is_request 1 --request_rw {8} --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, requester_ip, output_dir, bench_thread, node_num, request_type)
    )
    # str1 = stdout.read().decode('utf-8')
    # str2 = stderr.read().decode('utf-8')
    # print("output for requester")
    # print(str1)
    # print(str2)
    # sys.stdout.flush()

    while not stdout.channel.exit_status_ready():
        result = stdout.readline()
        print(result)
        if stdout.channel.exit_status_ready():
            a = stdout.readlines()
            print(a)
            break

def cache_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    print("cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 0 --is_cache 1 --cache_rw 1 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, cache_ip, output_dir, bench_thread, node_num))
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 0 --is_cache 1 --cache_rw 1 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, cache_ip, output_dir, bench_thread, node_num)
    )
    # str1 = stdout.read().decode('utf-8')
    # str2 = stderr.read().decode('utf-8')
    # print("output for cache")
    # print(str1)
    # print(str2)
    # sys.stdout.flush()

    while not stdout.channel.exit_status_ready():
        result = stdout.readline()
        print(result)
        if stdout.channel.exit_status_ready():
            a = stdout.readlines()
            print(a)
            break


def home_run(ssh, program, bench_thread, sys_thread, output_dir, node_num):
    print("cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 1 --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, home_ip, output_dir, bench_thread, node_num))
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_home 1 --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, home_ip, output_dir, bench_thread, node_num)
    )
    # str1 = stdout.read().decode('utf-8')
    # str2 = stderr.read().decode('utf-8')
    # print("output for cache")
    # print(str1)
    # print(str2)
    # sys.stdout.flush()

    while not stdout.channel.exit_status_ready():
        result = stdout.readline()
        print(result)
        if stdout.channel.exit_status_ready():
            a = stdout.readlines()
            print(a)
            break


def other_run(ssh, program, bench_thread, sys_thread, output_dir, node_num, self_ip):
    print("cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, self_ip, output_dir, bench_thread, node_num))
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node {7} --port_worker 1235 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 88 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, self_ip, output_dir, bench_thread, node_num)
    )
    # str1 = stdout.read().decode('utf-8')
    # str2 = stderr.read().decode('utf-8')
    # # print(str1)
    # # print(str2)           

    while not stdout.channel.exit_status_ready():
        result = stdout.readline()
        print(result)
        if stdout.channel.exit_status_ready():
            a = stdout.readlines()
            print(a)
            break

def ssh_connect(ip, user):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ip, 22, user, None, key_filename="/home/zxy/.ssh/id_rsa")
    return ssh


if __name__ == '__main__':
    ssh_master = ssh_connect(master_machine, user)
    ssh_requester = ssh_connect(requester_machine, user)
    ssh_cache = ssh_connect(cache_machine, user)
    if (master_ip != home_ip):
        ssh_home = ssh_connect(home_machine, user)

    ssh_others = [ssh_connect(other_machine[i], user) for i in range(len(other_machine))]

    create_dir(ssh_master, base + "/build")

    if init_build:
        t0 = threading.Thread(target=make_and_clean,
                                args=(ssh_master,))
        t0.start()
        t0.join()
    
    time.sleep(5)

    for b_i in bench_thread:
        for s_i in sys_thread:
            if (master_ip == home_ip):
                print("master_ip == home_ip")
                t1 = threading.Thread(target=master_home_run,
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

            else:
                print("master_ip != home_ip")
                t1 = threading.Thread(target=master_run,
                                        args=(ssh_master, program_name, b_i, s_i, output_directory, 4+len(ssh_others)))

                t2 = threading.Thread(target=requester_run,
                                        args=(ssh_requester, program_name, b_i, s_i, output_directory, 4+len(ssh_others)))

                t3 = threading.Thread(target=cache_run,
                                        args=(ssh_cache, program_name, b_i, s_i, output_directory, 4+len(ssh_others)))

                t4 = threading.Thread(target=home_run,
                                        args=(ssh_home, program_name, b_i, s_i, output_directory, 4+len(ssh_others)))

                tother_list = [threading.Thread(target=other_run,
                                        args=(ssh_others[i], program_name, b_i, s_i, output_directory, 4+len(ssh_others), other_ip[i]))  for i in range(len(ssh_others))]
                
                # t1.start()
                # time.sleep(2)
                # t2.start()
                # time.sleep(2)
                # t4.start()
                # time.sleep(2)
                # t3.start()
                # time.sleep(2)
                
                
                t1.start()
                time.sleep(1)
                t2.start()
                # time.sleep(2)
                t3.start()
                # time.sleep(2)
                t4.start()
                # time.sleep(2)
                for i in range(len(ssh_others)):
                    tother_list[i].start()

                t1.join()
                t2.join()
                t3.join()
                t4.join()
                for i in range(len(ssh_others)):
                    tother_list[i].join()

    print("finish: bench_thread {} sys_thread {}\n".format(b_i, s_i))
    time.sleep(10)
