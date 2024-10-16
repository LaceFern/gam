// Copyright (c) 2018 The GAM Authors 

#ifndef SERVER_H_
#define SERVER_H_ 

#include <unordered_map>

#include "client.h"
#include "settings.h"
#include "rdma.h"
#include "workrequest.h"
#include "structure.h"
#include "ae.h"
#include "hashtable.h"
#include "locked_unordered_map.h"
#include "map.h"
#include "agent_stat.h"
#define CDF_BUCKET_NUM 512
class ServerFactory;
class Server;
class Client;

static int latency_to_bkt(unsigned long lat_in_us) {
  if (lat_in_us < 100)
    return (int)lat_in_us;
  else if (lat_in_us < 1000)
    return 100 + ((lat_in_us - 100) / 10);
  else if (lat_in_us < 10000)
    return 190 + ((lat_in_us - 1000) / 100);
  else if (lat_in_us < 100000)
    return 280 + ((lat_in_us - 10000) / 1000);
  else if (lat_in_us < 1000000)
    return 370 + ((lat_in_us - 100000) / 10000);
  return CDF_BUCKET_NUM - 1;	// over 1 sec
}

class Server {
private:
  //unordered_map<uint32_t, Client*> qpCliMap; /* rdma clients */
  //unordered_map<int, Client*> widCliMap; //map from worker id to region
  HashTable<uint32_t, Client *> qpCliMap{ "qpCliMap" };  //thread-safe as it is dynamic
  HashTable<int, Client *> widCliMap{ "widCliMap" };  //store all the wid -> Client map
  UnorderedMap<int, Client *> widCliMapWorker{ "widCliMapWorker" };  //only store the wid -> Client map excluding ClientServer
  HashTable<uint64_t, long> networkLatencyMap{ "networkLatencyMap" };
  RdmaResource *resource;
  aeEventLoop *el;
  int sockfd;
  const Conf *conf;

  friend class ServerFactory;
  friend class Master;
  friend class Worker;
  friend class Cache;

public:
  atomic<unsigned long> cdf_cnt_network[CDF_BUCKET_NUM];
  Client *NewClient(bool isMaster, const char *rdmaConn = nullptr);
  Client *NewClient(const char *);
  Client *NewClient();

  virtual bool IsMaster() = 0;
  virtual int GetWorkerId() = 0;

  void RmClient(Client *);

  Client *FindClient(uint32_t qpn);
  void UpdateWidMap(Client *cli);
  Client *FindClientWid(int wid);
  inline int GetClusterSize() {
    return widCliMap.size();
  }

  void ProcessRdmaRequest();
  MULTI_SYS_THREAD_OP ProcessRdmaRequest(ibv_wc &wc);
  virtual int PostAcceptWorker(int, void *) {
    return 0;
  }
  virtual int PostConnectMaster(int, void *) {
    return 0;
  }
  virtual void ProcessRequest(Client *client, WorkRequest *wr) = 0;
  virtual void ProcessRequest(Client *client, unsigned int id) {}
  virtual MULTI_SYS_THREAD_OP ProcessRequestWithOpRes(Client *client, unsigned int id) {
    return MULTI_SYS_THREAD_OP::NONE;
  }

  virtual void CompletionCheck(unsigned int id) {}

  const string &GetIP() const {
    return conf->worker_ip;
  }

  int GetPort() const {
    return conf->worker_port;
  }

  inline std::vector<int> get_all_client_pending_msg_number() {
    std::vector<int> res;
    for (auto &item : widCliMapWorker) {
      res.push_back(item.second->get_pending_msg_number());
    }
    return res;
  }

  virtual ~Server() {
    aeDeleteEventLoop(el);
  }
};
#endif
