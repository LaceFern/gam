cd ~/DSM_prj/gam_20240222/src
make clean
make -j
cd ~/DSM_prj/gam_20240222/test_cc_highpara
make clean
make -j
sudo scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.13:/home/zxy/DSM_prj/
sudo scp -r /home/zxy/DSM_prj/gam_20240222  zxy@192.168.189.8:/home/zxy/DSM_prj/