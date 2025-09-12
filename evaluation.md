###个人理解
1. 为了让 GAM 跟 Concordia 在选取相同 sharing ratio 时地址空间范围一致，GAM 的 STEPS 应该设置为应用实际总共享空间（不包括仅共本地访问的空间）除以 cline size；这使得sharing ratio=100时，单线程读写操作理想情况下可以覆盖整个共享空间

2. GAM中conf.size表示的是单机slab分配器需要开出的空间，在当前总的gmem有8GB用于共享8GB用于非共享的情况下，每机至少需要开((long) BLOCK_SIZE) * STEPS * 1.0 / no_node * 2，还需要考虑cache空间，可以直接开个16GB

3. 如果遇到如下RDMA连接报错，通过命令行指令 show_gids 查看当前gid是否连续，不连续则出故障（可能是因为上一次运行没被kill就开始了新的运行导致的），可以通过重启解决
[562573] 08 Sep 13:51:30.200 - [/home/zxy/nfs/DSM_prj/gam_cxz/src/rdma.cc:603-SetRemoteConnParam()] Unable to modify qp to RTR (61:No data available)
[562573] 08 Sep 13:51:30.200 - [/home/zxy/nfs/DSM_prj/gam_cxz/src/rdma.cc:835-Rdma()] ibv_post_send failed (61:No data available)

4. 应该在什么位置加 mfence 才能保证 sequential consistency？在所有写操作后面都加mfence

5. 目标链路测试得用WLock，不然没法搜集等待时间；以实验名命名文件夹

6. 为了跟Concordia保持一致，缓存率=单节点cache空间/总gmem空间，单节点单线程访问的地址空间数量=允许被共享的gmem空间/cline大小。

7. 应该把线程都绑在numa0上，以体现线程间的串扰和激烈竞争;此外，main函数本身还占用一个线程，等待队列本身还占用一个线程，异步驱逐还占了一个线程，因而在appt=13，syst=8时，逻辑核就已经被用完；为了避免在appt小于13时就因appt跟main所在线程同物理核导致性能下降，appt的优先从numa0的lcore1开始绑(算了算了，对于GAM还是把appt绑到numa1上，对于concordia可以都绑到numa0上)

8. 重要-GAM的随机访问存在两次随机地址不可位于同一个data block的限制，这虽然可以减小cline的本地竞争，也可能加剧cline miss，需要具体参数具体分析

9. 为了减少测试所需的开辟的gmem空间，限制了非共享区域的地址数量

10. 为什么write操作gam的req节点没有统计信息？

11. 把 MAX_SYS_THREAD 宏从12增加到了24