#include <iostream>
#include <ib++/conn.hpp>
#include <condition_variable>
#include "file_request.hpp"

using namespace std;

enum ConnState {
    WAITING,
    CONNECTED,
    ERROR,
};

int main() {
    mutex mtx;
    condition_variable cv;
    ConnState conn_state = WAITING;

    ib::Conn<> conn([&conn_state, &mtx, &cv](bool success) {
        cout << "client connected " << success << endl;

        unique_lock<mutex> lock(mtx);
        conn_state = (success ? CONNECTED : ERROR);
        lock.unlock();
        cv.notify_one();
    });

    cout << "waiting for connection@ " << conn.connect_str << endl;
    unique_lock<mutex> lock(mtx);
    cv.wait(lock, [&conn_state]{
        return conn_state != WAITING;
    });

    if(conn_state == ERROR) {
        cout << "failed to establish connection" << endl;
        return 1;
    }

    FileRequest req;
    if(!conn.cm_conn.GetMsg(&req)) {
        throw std::runtime_error("cannot get file request");
    }
    cout << "request file: " << req.filepath << endl;

    auto file_mr = ib::make_file_mr(conn.pd, req.filepath);
    FileResponse res;
    res.addr = reinterpret_cast<uint64_t>(file_mr->addr);
    res.key = file_mr->rkey;
    res.size = file_mr->length;
    if(!conn.cm_conn.PutMsg(res)) {
        throw std::runtime_error("cannot send file response");
    }

    FileDone done;
    if(!conn.cm_conn.GetMsg(&done)) {
        cout << "file transfer error" << endl;
    }
}
