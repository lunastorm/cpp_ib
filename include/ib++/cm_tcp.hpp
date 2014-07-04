#ifndef IB_TCP_CM_HPP_
#define IB_TCP_CM_HPP_

#include <string>
#include <stdexcept>
#include <sstream>
#include <ib++/utils.hpp>
#include <ib++/cm_msg.hpp>

namespace ib { namespace cm { namespace tcp {

struct Socket {
    Socket(int fd_in=-1) {
        if(fd_in >= 0) {
            fd = fd_in;
            return;
        }
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(fd == -1) {
            throw std::runtime_error("cannot create socket");
        }
    }

    Socket(const Socket&) = delete;

    Socket(Socket&& o) {
        this->~Socket();
        fd = o.fd;
        o.fd = -1;
    }

    Socket& operator=(Socket&& o) {
        if(this != &o) {
            this->~Socket();
            fd = o.fd;
            o.fd = -1;
        }
    }

    ~Socket() {
        if(fd >= 0) {
        std::cout << "close socket " << fd << std::endl;
            close(fd);
        }
    }

    int fd;
};

struct Conn {
    Conn(ConnRole role, std::string connect_str_in): role_(role),
        connect_str(connect_str_in), sock_()
    {
        if(role == CONNECTOR) {
            return;
        }

        sockaddr addr;
        ConnectStringToSockaddr(connect_str,
            reinterpret_cast<sockaddr_in *>(&addr));
        if(-1 == ::bind(sock_.fd, &addr, sizeof(addr))) {
            throw std::runtime_error("bind error");
        }
        if(-1 == listen(sock_.fd, 1)) {
            throw std::runtime_error("listen error");
        }
        socklen_t len = sizeof(addr);
        getsockname(sock_.fd, &addr, &len);
        std::ostringstream tmp;
        tmp << GetHostIP() << ":" << ntohs(((sockaddr_in *)&addr)->sin_port);
        connect_str = tmp.str();
    }

    void accept() {
        sockaddr addr;
        socklen_t len = sizeof(addr);
        int res = ::accept(sock_.fd, &addr, &len);
        if(res == -1) {
            throw std::runtime_error("cannot accept");
        }
        sock_ = Socket(res);
    }

    void connect() {
        sockaddr addr;
        ConnectStringToSockaddr(connect_str,
            reinterpret_cast<sockaddr_in *>(&addr));
        bool connected = false;
        for(int i=0; i<5; ++i) {
            if(0 == ::connect(sock_.fd, &addr, sizeof(addr))) {
                connected = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if(!connected) {
            throw std::runtime_error("cannot connect");
        }
    }

    ConnInfo XchgInfo(const ConnInfo& info) {
        if(!PutMsg(info)) {
            throw std::runtime_error("cannot send info to peer");
        }
        ConnInfo res;
        if(!GetMsg(&res)) {
            throw std::runtime_error("cannot recv info to peer");
        }
        return res;
    }

    template<typename T=ConnInfo>
    bool GetMsg(T *msg) {
        size_t size_read = 0;
        while(size_read < sizeof(T)) {
            ssize_t n = read(sock_.fd, reinterpret_cast<char*>(msg)+size_read,
                sizeof(T)-size_read);
            if(n <= 0) {
                return false;
            }
            size_read += n;
        }
        return true;
    }

    template<typename T=ConnInfo>
    bool PutMsg(T msg) {
        size_t size_written = 0;
        while(size_written < sizeof(T)) {
            ssize_t n = write(sock_.fd, reinterpret_cast<char*>(&msg)+size_written,
                sizeof(T)-size_written);
            if(n <= 0) {
                return false;
            }
            size_written += n;
        }
        return true;
    }

    std::string connect_str;

private:
    Socket sock_;
    ConnRole role_;
};

} //tcp
} //cm
} //ib

#endif
