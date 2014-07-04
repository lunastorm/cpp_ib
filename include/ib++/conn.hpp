#ifndef IB_CONN_HPP_
#define IB_CONN_HPP_

#include <thread>
#include <functional>
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
        pd(make_pd(ctx)), scq(make_cq(ctx)), rcq(make_cq(ctx)),
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
    }

    std::function<void(bool)> cb;
    DevicesPtr devices;
    CtxPtr ctx;
    PdPtr pd;
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
            std::cout << "get rep lid: " << remote_info.lid << std::endl;
            std::cout << "get rep qpn: " << remote_info.qpn << std::endl;
            std::cout << "get rep psn: " << remote_info.psn << std::endl;
            enterRtr(remote_info);
            enterRts();
        }
        catch(...) {
            this->cb_(false);
            return;
        }
        this->cb_(true);
    }

/*
    void runClient() {
        try {
            auto rep = cm_cnxn.GetRep({getLid(), qp->qp_num, psn_});
            std::cout << "get rep lid: " << rep.lid << std::endl;
            std::cout << "get rep qpn: " << rep.qpn << std::endl;
            std::cout << "get rep psn: " << rep.psn << std::endl;
            enterRtr(rep);
            enterRts();

            cm_cnxn.PutRtu();
            try {
                this->cb(true);
            }
            catch(const std::runtime_error& e) {
                std::cout << "client error: " << e.what() << std::endl;
            }
        }
        catch(...) {
            this->cb(false);
        }
    }

    void runServer() {
        try {
            auto req = cm_cnxn.GetReq();
            std::cout << "get req lid: " << req.lid << std::endl;
            std::cout << "get req qpn: " << req.qpn << std::endl;
            std::cout << "get req psn: " << req.psn << std::endl;
            enterRtr(req);
            enterRts();
            cm_cnxn.GetRtu({getLid(), qp->qp_num, psn_});

            try {
                this->cb(true);
            }
            catch(const std::runtime_error& e) {
                std::cout << "server error: " << e.what() << std::endl;
            }
        } catch(...) {
            this->cb(false);
        }
    }
    */

    std::function<void(bool)> cb_;
    ConnRole role_;
    uint32_t psn_;
};

} //ib

#endif
