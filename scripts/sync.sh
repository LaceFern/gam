cd ~/DSM_prj/gam_20240222/src
make clean
make -j
cd ~/DSM_prj/gam_20240222/test_cc_highpara
make clean
make -j
scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.13:/home/zxy/DSM_prj/
scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.12:/home/zxy/DSM_prj/
scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.11:/home/zxy/DSM_prj/
scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.10:/home/zxy/DSM_prj/
scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.9:/home/zxy/DSM_prj/
scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.8:/home/zxy/DSM_prj/
scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.7:/home/zxy/DSM_prj/

# sudo scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.12:/home/zxy/DSM_prj/