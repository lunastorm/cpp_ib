#ifndef IB_CM_MSG_HPP_
#define IB_CM_MSG_HPP_

namespace ib { namespace cm {

struct ConnInfo {
    uint16_t lid;
    uint32_t qpn;
    uint32_t psn;
};

} //cm
} //ib

#endif
