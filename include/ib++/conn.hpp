#ifndef IB_CONN_HPP_
#define IB_CONN_HPP_

#include <thread>
#include <future>
#include <functional>
#include <map>
#include <atomic>
#include <ib++/verbs.hpp>
#include <ib++/utils.hpp>
#include <ib++/conn_role.hpp>
#include <ib++/cm_tcp.hpp>
#include <ib++/cm_msg.hpp>

namespace ib {

template<typename CM = typename cm::tcp::Conn>
struct Conn {
    Conn(std::function<void(bool)> cb, ConnRole role=LISTENER,
            std::string connect_str_in="0.0.0.0:0",
            int nth_device=0, int port=0, int pkey_index=0):
        cb_(cb), role_(role),
        devices(get_devices()), ctx(make_ctx(devices, nth_device)),
        pd(make_pd(ctx)), cc(make_cc(ctx)), scq(make_cq(ctx, cc)), rcq(make_cq(ctx)),
        qp(make_qp(pd, scq, rcq)),
        cm_conn(role, connect_str_in),
        connect_str(cm_conn.connect_str),
        psn_(GenRnd<uint32_t>(0, 0xffffff))
    {
        enterInit(port, pkey_index);

        if(role == LISTENER) {
            std::thread([this]{
                cm_conn.accept();
                establishConnection();
            }).detach();
        }
        else {
            std::thread([this]{
                cm_conn.connect();
                establishConnection();
            }).detach();
        }
        std::thread([this]{
            handleEvents();
        }).detach();
    }

    std::future<bool> Read(MrPtr mr, uint64_t remote_addr, uint32_t remote_key, uint64_t size) {
        ibv_sge sge;
        sge.addr = reinterpret_cast<uintptr_t>(mr->addr);
        sge.length = mr->length;
        sge.lkey = mr->lkey;

        ibv_send_wr wr;
        wr.wr_id = wr_id_++;
        wr.next = nullptr;
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_READ;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = remote_addr;
        wr.wr.rdma.rkey = remote_key;

        ibv_send_wr *bad_wr;
        if(0 != ibv_post_send(qp.get(), &wr, &bad_wr)) {
            throw std::runtime_error("cannot post rdma read");
        }
        return promises_[wr.wr_id].get_future();
    }

    std::function<void(bool)> cb;
    DevicesPtr devices;
    CtxPtr ctx;
    PdPtr pd;
    CcPtr cc;
    CqPtr scq;
    CqPtr rcq;
    QpPtr qp;
    CM cm_conn;
    std::string connect_str;

private:
    void enterInit(int port, int pkey_index) {
        ibv_qp_attr qp_attr;
        memset(&qp_attr, 0, sizeof(qp_attr));
        qp_attr.qp_state = IBV_QPS_INIT;
        qp_attr.port_num = port+1;
        qp_attr.pkey_index = pkey_index;
        qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_ATOMIC;
        if(0 != ibv_modify_qp(qp.get(), &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
            IBV_QP_PORT | IBV_QP_ACCESS_FLAGS)) {
            throw std::runtime_error("cannot init qp");
        }
    }

    uint16_t getLid() {
        ibv_port_attr attr;
        if(0 != ibv_query_port(ctx.get(), 1, &attr)) {
            throw std::runtime_error("cannot get lid");
        }
        return attr.lid;
    }

    void enterRtr(cm::ConnInfo info) {
        ibv_qp_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RTR;
        attr.path_mtu = IBV_MTU_2048;
        attr.dest_qp_num = info.qpn;
        attr.rq_psn = info.psn;
        attr.max_dest_rd_atomic = 4;
        attr.min_rnr_timer = 12;
        attr.ah_attr.is_global = 0;
        attr.ah_attr.dlid = info.lid;
        attr.ah_attr.sl = 0;
        attr.ah_attr.src_path_bits = 0;
        attr.ah_attr.port_num = 1;
        if(0 != ibv_modify_qp(qp.get(), &attr, IBV_QP_STATE |
            IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_AV | IBV_QP_PATH_MTU |
            IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)) {
            throw std::runtime_error("cannot enter RTR");
        }
    }

    void enterRts() {
        ibv_qp_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RTS;
        attr.sq_psn = psn_;
        attr.max_rd_atomic = 1;
        attr.timeout = 10;
        attr.retry_cnt = 10;
        attr.rnr_retry = 10;
        if(-1 == ibv_modify_qp(qp.get(), &attr, IBV_QP_STATE | IBV_QP_SQ_PSN |
            IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
            IBV_QP_MAX_QP_RD_ATOMIC)) {
            throw std::runtime_error("cannot enter RTS");
        }
    }

    void establishConnection() {
        try {
            cm::ConnInfo local_info{getLid(), qp->qp_num, psn_};
            auto remote_info = cm_conn.XchgInfo(local_info);
            enterRtr(remote_info);
            enterRts();
        }
        catch(...) {
            this->cb_(false);
            return;
        }
        this->cb_(true);
    }

    void handleEvents() {
        if(0 != ibv_req_notify_cq(scq.get(), 0)) {
            throw std::runtime_error("cannot request cq notification");
        }
        while(true) {
            ibv_cq *cq;
            void *cq_ctx;
            if(0 != ibv_get_cq_event(cc.get(), &cq, &cq_ctx)) {
                throw std::runtime_error("cannot get cq event");
            }
            ibv_ack_cq_events(scq.get(), 1);
            if(0 != ibv_req_notify_cq(scq.get(), 0)) {
                throw std::runtime_error("cannot request cq notification");
            }

            int n;
            ibv_wc wc;
            do {
                n = ibv_poll_cq(scq.get(), 1, &wc);
                if(n < 0) {
                    throw std::runtime_error("cannot poll cq");
                }
                else if(n == 0) {
                    continue;
                }
                else {
                    promises_[wc.wr_id].set_value(wc.status == IBV_WC_SUCCESS);
                    promises_.erase(wc.wr_id);
                }
            } while(n);
        }
    }

    std::function<void(bool)> cb_;
    ConnRole role_;
    uint32_t psn_;
    std::map<uint64_t, std::promise<bool>> promises_;
    std::atomic_uint_fast64_t wr_id_;
};

} //ib

#endif
