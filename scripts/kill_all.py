import paramiko
import threading

program_name = "highpara_benchmark"

target_machine = ["192.168.189.7", "192.168.189.8", "192.168.189.9", "192.168.189.10", 
                  "192.168.189.11", "192.168.189.12", "192.168.189.13", "192.168.189.14"]

def kill_process(ssh, program):
    stdin, stdout, stderr = ssh.exec_command(
        "pkill -9 -f {0}".format(program)
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
    for machine in target_machine:
        ssh = ssh_connect(machine, "cxz")
        kill_process(ssh, program_name)
        ssh.close()