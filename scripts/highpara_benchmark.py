# please warning don't use sudo to run this scripts

import paramiko
import threading
import time

base = "/home/cxz/nfs/gam"

user = "cxz"

# whether rebuild from source code
init_build = True


master_machine = "192.168.189.8"
master_ip = "10.0.0.2"

requester_machine = "192.168.189.13"
requester_ip = "10.0.0.7"

cache_machine = "192.168.189.14"
cache_ip = "10.0.0.8"

output_directory = "/home/cxz/gam_result"

program_name = "highpara_benchmark"

payload = [16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072]
bench_thread = [8]
sys_thread = [1,2,4,6,8,11]


def make_and_clean(ssh, extra_flag=""):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}  && rm -rf ./build && mkdir build && cd build &&  cmake .. {1} && make -j".format(
            base,
            extra_flag))
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def create_dir(ssh, filename):
    stdin, stdout, stderr = ssh.exec_command(
        "mkdir -p $(dirname {0})".format(filename)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)


def master_run(ssh, program, bench_thread, sys_thread, output_dir):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 0 --cache_rw 0 --is_request 0 --request_rw 0 --is_master 1 " 
        "--ip_master {3} --ip_worker {4} --no_node 3 --port_worker 1234 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 0 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, master_ip, output_dir, bench_thread)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)

def requester_run(ssh, program, bench_thread, sys_thread, output_dir):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 0 --cache_rw 0 --is_request 1 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node 3 --port_worker 1234 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 0 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, requester_ip, output_dir, bench_thread)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)

def cache_run(ssh, program, bench_thread, sys_thread, output_dir):
    stdin, stdout, stderr = ssh.exec_command(
        "cd {0}/build && ./{1} "
        "--no_sys_thread {2} --is_cache 1 --cache_rw 1 --is_request 0 --request_rw 0 --is_master 0 " 
        "--ip_master {3} --ip_worker {4} --no_node 3 --port_worker 1234 --port_master 1231 "
        "--cache_th 2 --result_dir {5} --no_thread {6} --remote_ratio 0 --shared_ratio 100 "
        "--read_ratio 50 --space_locality 0 --time_locality 0 --op_type 2".format(base, program, sys_thread, master_ip, cache_ip, output_dir, bench_thread)
    )
    str1 = stdout.read().decode('utf-8')
    str2 = stderr.read().decode('utf-8')
    print(str1)
    print(str2)    


def ssh_connect(ip, user):
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(ip, 22, user, None, key_filename="/home/cxz/.ssh/id_ed25519")
    return ssh


if __name__ == '__main__':
    ssh_master = ssh_connect(master_ip, user)
    ssh_requester = ssh_connect(requester_ip, user)
    ssh_cache = ssh_connect(cache_ip, user)

    if init_build:
        t0 = threading.Thread(target=make_and_clean,
                                args=(ssh_master,))
        t0.start()
        t0.join()

    for b_i in bench_thread:
        for s_i in sys_thread:
            t1 = threading.Thread(target=master_run,
                                    args=(ssh_master, program_name, b_i, s_i, output_directory))

            t2 = threading.Thread(target=requester_run,
                                    args=(ssh_requester, program_name, b_i, s_i, output_directory))
            
            t3 = threading.Thread(target=cache_run,
                                    args=(ssh_cache, program_name, b_i, s_i, output_directory))

            t1.start()
            time.sleep(3)
            t2.start()
            t3.start()

            t3.join()
            t2.join()
            t1.join()

            print("finish: bench_thread {} sys_thread {}\n".format(b_i, s_i))
            time.sleep(3)
