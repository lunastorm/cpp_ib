#ifndef IB_VERBS_HPP_
#define IB_VERBS_HPP_

#include <memory>
#include <stdexcept>
#include <infiniband/verbs.h>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

namespace ib {

using DevicesPtr = std::shared_ptr<ibv_device*>;
static DevicesPtr get_devices() {
    int size;
    auto ptr = ibv_get_device_list(&size);
    if(!ptr) {
        throw std::runtime_error("cannot get devices");
    }
    return DevicesPtr(ptr, ibv_free_device_list);
}

static int get_num_devices(DevicesPtr devices=get_devices()) {
    int num = 0;
    for(ibv_device **iter=devices.get(); *iter; ++iter) {
        ++num;
    }
    return num;
}

static ibv_device *get_device(DevicesPtr devices, int i) {
    if(i < 0 || i >= get_num_devices(devices)) {
        throw std::out_of_range("cannot get device");
    }
    return devices.get()[i];
}

using CtxPtr = std::shared_ptr<ibv_context>;
static CtxPtr make_ctx(DevicesPtr devices, int i) {
    auto ptr = ibv_open_device(get_device(devices, i));
    if(!ptr) {
        throw std::runtime_error("cannot open device");
    }
    return CtxPtr(ptr, ibv_close_device);
}

using PdPtr = std::shared_ptr<ibv_pd>;
static PdPtr make_pd(CtxPtr ctx) {
    auto ptr = ibv_alloc_pd(ctx.get());
    if(!ptr) {
        throw std::runtime_error("cannot allocate pd");
    }
    return PdPtr(ptr, ibv_dealloc_pd);
}

using CqPtr = std::shared_ptr<ibv_cq>;
static CqPtr make_cq(CtxPtr ctx) {
    auto ptr = ibv_create_cq(ctx.get(), 1, nullptr, nullptr, 0);
    if(!ptr) {
        throw std::runtime_error("cannot create cq");
    }
    return CqPtr(ptr, ibv_destroy_cq);
}

using QpPtr = std::shared_ptr<ibv_qp>;
static QpPtr make_qp(PdPtr pd, CqPtr scq, CqPtr rcq) {
    ibv_qp_init_attr attr;
    attr.qp_context = nullptr;
    attr.send_cq = scq.get();
    attr.recv_cq = rcq.get();
    attr.srq = nullptr;
    attr.cap.max_send_wr = 10;
    attr.cap.max_recv_wr = 10;
    attr.cap.max_send_sge = 10;
    attr.cap.max_recv_sge = 10;
    attr.cap.max_inline_data = 0;
    attr.qp_type = IBV_QPT_RC;
    attr.sq_sig_all = 0;

    auto ptr = ibv_create_qp(pd.get(), &attr);
    if(!ptr) {
        throw std::runtime_error("cannot create qp");
    }
    return QpPtr(ptr, ibv_destroy_qp);
}

using MrPtr = std::shared_ptr<ibv_mr>;
static MrPtr make_mr(PdPtr pd, size_t size, int access=IBV_ACCESS_LOCAL_WRITE |
    IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC) {
    auto buf = new char[size];
    auto ptr = ibv_reg_mr(pd.get(), buf, size, access);
    if(!ptr) {
        throw std::runtime_error("cannot create mr");
    }
    return MrPtr(ptr, [](ibv_mr *ptr){
        ibv_dereg_mr(ptr);
        delete[] reinterpret_cast<char*>(ptr->addr);
    });
}

static MrPtr make_file_mr(PdPtr pd, const char *pathname, size_t size=-1,
    int access=IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ |
    IBV_ACCESS_REMOTE_ATOMIC) {
    int fd = open(pathname, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if(fd == -1) {
        throw std::runtime_error("cannot open file");
    }

    if(size == -1) {
        struct stat st;
        int res = fstat(fd, &st);
        if(res == -1) {
            throw std::runtime_error("cannot get file stat");
        }
        size = st.st_size;
    }
    else {
        if(-1 == ftruncate(fd, size)) {
            throw std::runtime_error("cannot set file size");
        }
    }
    auto buf = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(buf == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("cannot mmap file");
    }
    std::cout << "buf: " << buf << std::endl;
    std::cout << "size: " << size << std::endl;
    auto ptr = ibv_reg_mr(pd.get(), buf, size, access);
    if(!ptr) {
        munmap(buf, size);
        close(fd);
        throw std::runtime_error("cannot create file mr");
    }
    return MrPtr(ptr, [fd, buf, size](ibv_mr *ptr) {
        ibv_dereg_mr(ptr);
        munmap(buf, size);
        close(fd);
    });
}

} //ib

#endif
