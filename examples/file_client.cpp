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

FileResponse request_file(const FileRequest& req, ib::Conn<>& conn) {
    FileResponse res;
    if(!conn.cm_conn.PutMsg(req)) {
        throw std::runtime_error("cannot request file");
    }
    if(!conn.cm_conn.GetMsg(&res)) {
        throw std::runtime_error("cannot get file response");
    }
    return res;
}

int main(int argc, char* argv[]) {
    if(argc != 4) {
        cerr << "usage: " << argv[0] << " connect_string remote_path local_path" << endl;
        return 1;
    }

    mutex mtx;
    condition_variable cv;
    ConnState conn_state = WAITING;

    ib::Conn<> conn([&conn_state, &mtx, &cv](bool success) {
        cout << "connect to server: " << success << endl;

        unique_lock<mutex> lock(mtx);
        conn_state = (success ? CONNECTED : ERROR);
        lock.unlock();
        cv.notify_one();
    }, ib::CONNECTOR, argv[1]);

    cout << "waiting for connection to be established" << endl;
    unique_lock<mutex> lock(mtx);
    cv.wait(lock, [&conn_state]{
        return conn_state != WAITING;
    });

    if(conn_state == ERROR) {
        cout << "failed to establish connection" << endl;
        return 1;
    }

    FileRequest req;
    strncpy(req.filepath, argv[2], 1023);
    req.filepath[1023] = '\0';
    auto remote_info = request_file(req, conn);
    cout << "raddr: " << remote_info.addr << ", rkey: " << remote_info.key
        << ", size: " << remote_info.size << endl;

    auto mr_ptr = ib::make_file_mr(conn.pd, argv[3], remote_info.size);

    auto start_tp = chrono::system_clock::now();
    auto future = conn.Read(mr_ptr, remote_info.addr, remote_info.key, remote_info.size);
    bool success = future.get();
    auto time_elapsed = chrono::system_clock::now() - start_tp;
    cout << "read success: " << success << ", read time: " <<
        chrono::duration_cast<chrono::microseconds>(time_elapsed).count() << "us" << endl;

    conn.cm_conn.PutMsg(FileDone{});
}
