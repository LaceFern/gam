// Copyright (c) 2018 The GAM Authors 

#include <cstring>
#include <arpa/inet.h>
#include "server.h"
#include "log.h"
#include "kernel.h"
#include "util.h"

Client *Server::NewClient() {
  Client *c = NewClient(IsMaster());
  return c;
}

Client *Server::NewClient(const char *rdmaConn) {
  return NewClient(IsMaster(), rdmaConn);
}

Client *Server::NewClient(bool isMaster, const char *rdmaConn) {
  try {
    Client *c = new Client(resource, isMaster, rdmaConn);
    uint32_t qp = c->GetQP();
    //LOCK_MICRO(qpCliMap, 0);
    qpCliMap[qp] = c;
    //UNLOCK_MICRO(qpCliMap, 0);
    return c;
  }
  catch (int err) {
    epicLog(LOG_WARNING, "Unable to create new client\n");
    return NULL;
  }
}

MULTI_SYS_THREAD_OP Server::ProcessRdmaRequest(ibv_wc &wc, uint64_t sys_thread_id) {
  void *ctx;
  Client *cli;
  uint32_t immdata, id;
  MULTI_SYS_THREAD_OP res_op = MULTI_SYS_THREAD_OP::NONE;

  cli = FindClient(wc.qp_num);
  if (unlikely(!cli)) {
    epicLog(LOG_WARNING, "cannot find the corresponding client for qp %d\n",
      wc.qp_num);
    return res_op;
  }

  epicLog(LOG_DEBUG, "THe client is: %d %d %d\n", cli->GetWorkerId(), cli->GetFreeMem(), cli->GetTotalMem());
  if (wc.status != IBV_WC_SUCCESS) {
    epicLog(LOG_WARNING, "Completion with error, wr_id = %d (%d:%s:%d:%d)", wc.wr_id,
      wc.status, ibv_wc_status_str(wc.status), wc.vendor_err, wc.qp_num);
    epicAssert(false);
    return res_op;
  }
  epicLog(LOG_DEBUG, "Completion without error, wr_id = %d op = %d (%d:%s:%d:%d)", wc.opcode, wc.wr_id,
    wc.status, ibv_wc_status_str(wc.status), wc.vendor_err, wc.qp_num);

  epicLog(LOG_DEBUG, "transferred %d (qp_num %d, src_qp %d)", wc.byte_len,
    wc.qp_num, wc.src_qp);

  switch (wc.opcode) {
  case IBV_WC_SEND:
    epicLog(LOG_DEBUG, "get send completion event");
#ifdef COLLECT_NETWORK_LATENCY
    try {
      long start_time = networkLatencyMap.at(wc.wr_id);
      cdf_cnt_network[latency_to_bkt((get_time() - start_time) / 1000)]++;
      networkLatencyMap.erase(wc.wr_id);
    }
    catch (const std::out_of_range &oor) {
      // epicLog(LOG_WARNING, "cannot find the client for qpn %d (%s)", qpn,
      //         oor.what());
    }
#endif
    id = cli->SendComp(wc);
    //send check initiated locally
    CompletionCheck(id);
    //TODO: send out the waiting request
    break;
  case IBV_WC_RDMA_WRITE:
    //update pending_msg
    epicLog(LOG_DEBUG, "get write completion event");
    id = cli->WriteComp(wc);
    //write check initiated locally
    CompletionCheck(id);
    //TODO: send out the waiting request
    break;
  case IBV_WC_RECV: {

    epicLog(LOG_DEBUG, "Get recv completion event");
    char *data = cli->RecvComp(wc);
    data[wc.byte_len] = '\0';

#ifdef MERGE_RDMA_REQUESTS
    //we cannot use split by string since GET/PUT is using binary protocol!!!
    //TODO: revise the protocol to binary protocol!!!
    epicLog(LOG_DEBUG, "received %s", data);
    int consumed_len = 0, len = 0;
    while (consumed_len < wc.byte_len) {
      WorkRequest *wr = new WorkRequest();
      if (wr->Deser(data + consumed_len, len)) {
        epicLog(LOG_WARNING, "de-serialize the work request failed\n");
      } else {
        epicLog(LOG_DEBUG, "After deserialize this should be processed, %lld, %lld", wr->size, wr->free);
        // need check before ProcessRequest, because ProcessRequest will delete wr!
        if (agent_stats_inst.is_valid_gaddr(wr->addr)) {
          if (wr->op == READ_FORWARD || wr->op == FETCH_AND_SHARED || wr->op == INVALIDATE || wr->op == FETCH_AND_INVALIDATE
            || wr->op == WRITE_FORWARD || wr->op == INVALIDATE_FORWARD || wr->op == WRITE_PERMISSION_ONLY_FORWARD) {
            res_op = MULTI_SYS_THREAD_OP::PROCESS_IN_CACHE_NODE;
          } else {
            res_op = MULTI_SYS_THREAD_OP::PROCESS_IN_HOME_NODE;
          }
        }
        ProcessRequest(cli, wr);
      }
      consumed_len += len;
      if (consumed_len < wc.byte_len) {
        if (data[consumed_len] != '\0') {
          epicLog(
            LOG_WARNING,
            "received: pos = %d, len = %d, consumed_len = %d, byte_len = %u, str = %s",
            data[consumed_len], len, consumed_len, wc.byte_len, data);
          epicAssert(false);
        }
      }
      consumed_len++;
    }
#else
    WorkRequest *wr = new WorkRequest();
    int len = wc.byte_len;
    if (wr->Deser(data, len)) {
      epicLog(LOG_WARNING, "de-serialize the work request failed\n");
    } else {
      if (len != wc.byte_len) {
        epicLog(LOG_WARNING, "len = %d, wc.byte_len = %d, data = %s", len, wc.byte_len, data);
        epicAssert(false);
      }
      // need check before ProcessRequest, because ProcessRequest will delete wr!
      if (agent_stats_inst.is_valid_gaddr(wr->addr)) {
        if (wr->op == READ_P2P){
          // just a hard code !!!
          res_op = MULTI_SYS_THREAD_OP::PROCESS_IN_HOME_NODE;
        }
        else if (wr->op == READ_FORWARD || wr->op == FETCH_AND_SHARED || wr->op == INVALIDATE || wr->op == FETCH_AND_INVALIDATE
          || wr->op == WRITE_FORWARD || wr->op == INVALIDATE_FORWARD || wr->op == WRITE_PERMISSION_ONLY_FORWARD) {
          res_op = MULTI_SYS_THREAD_OP::PROCESS_IN_CACHE_NODE;
        } else {
          res_op = MULTI_SYS_THREAD_OP::PROCESS_IN_HOME_NODE;
          // epicLog(LOG_WARNING, "Home node: op = %d\n", wr->op);
        }
      }

      wr->glb_thread_id = sys_thread_id;
      ProcessRequest(cli, wr);
    }
#endif
    //resource->ClearSlot(wc.wr_id);
    int n = resource->PostRecvSlot(wc.wr_id);
    //epicAssert(n == 1);
    break;
  }
  case IBV_WC_RECV_RDMA_WITH_IMM: {
    epicLog(LOG_DEBUG, "Get recv with IMM completion event");
    char *data = cli->RecvComp(wc);

    epicAssert(wc.wc_flags & IBV_WC_WITH_IMM);
    res_op = ProcessRequestWithOpRes(cli, ntohl(wc.imm_data), sys_thread_id);
    //resource->ClearSlot(wc.wr_id);
    int n = resource->PostRecvSlot(wc.wr_id);
    //epicAssert(n == 1);
    break;
  }
  default:
    epicLog(LOG_WARNING, "unknown opcode received %d\n", wc.opcode);
    break;
  }
  return res_op;
}

void Server::ProcessRdmaRequest() {
#ifdef RDMA_POLL
  epicAssert(IsMaster());
#endif
  int ne;
  ibv_wc wc[MAX_CQ_EVENTS];
  ibv_cq *cq = resource->GetCompQueue();

  epicLog(LOG_DEBUG, "received RDMA event\n");
  /*
   * to get notified in the event-loop,
   * we need ibv_req_notify_cq -> ibv_get_cq_event -> ibv_ack_cq_events seq -> ibv_req_notify_cq!!
   */
  if (likely(resource->GetCompEvent())) {
    do {
      ne = ibv_poll_cq(cq, MAX_CQ_EVENTS, wc);
      if (unlikely(ne < 0)) {
        epicLog(LOG_FATAL, "Unable to poll cq\n");
        return;
      }

      for (int i = 0; i < ne; ++i) {
        ProcessRdmaRequest(wc[i]);
      }
    } while (ne == MAX_CQ_EVENTS);
  }
}

Client *Server::FindClient(uint32_t qpn) {
  Client *cli = nullptr;
  //LOCK_MICRO(qpCliMap, 0);
  try {
    cli = qpCliMap.at(qpn);
  }
  catch (const std::out_of_range &oor) {
    epicLog(LOG_WARNING, "cannot find the client for qpn %d (%s)", qpn,
      oor.what());
  }
  //UNLOCK_MICRO(qpCliMap, 0);
  return cli;
}

void Server::UpdateWidMap(Client *cli) {
  widCliMap[cli->GetWorkerId()] = cli;
}

Client *Server::FindClientWid(int wid) {
  //	UpdateWidMap();
    //epicAssert(widCliMap.size() == qpCliMap.size());
  Client *cli = nullptr;
  //LOCK_MICRO(widCliMap, 0);
  try {
    cli = widCliMap.at(wid);
  }
  catch (const std::out_of_range &oor) {
    epicLog(LOG_WARNING, "cannot find the client for worker %d (%s)", wid,
      oor.what());
  }
  //UNLOCK_MICRO(widCliMap, 0);
  return cli;
}

void Server::RmClient(Client *c) {
  //TODO: currently we do not support remove client
  //in a thread-safe manner
  epicLog(LOG_WARNING, "WARNING: remove client");
  //LOCK_MICRO(qpCliMap, 0);
  qpCliMap.erase(c->GetQP());
  widCliMap.erase(c->GetWorkerId());
  epicAssert(widCliMapWorker.count(c->GetWorkerId()) == 0);  //FIXME: assume no worker will be deleted for now
  //UNLOCK_MICRO(qpCliMap, 0);
}

