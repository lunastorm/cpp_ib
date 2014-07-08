#include <iostream>
#include <ib++/conn.hpp>
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
    int device = 0;
    int ib_port = 0;
    int pkey_index = 0;
    int c;
    while((c = getopt(argc, argv, "d:p:k:")) != -1) {
        switch(c) {
        case 'd':
        {
            istringstream iss(optarg);
            iss >> device;
            cout << "device: " << device << endl;
            break;
        }
        case 'k':
        {
            istringstream iss(optarg);
            iss >> pkey_index;
            cout << "pkey_index: " << pkey_index << endl;
            break;
        }
        case 'p':
        {
            istringstream iss(optarg);
            iss >> ib_port;
            cout << "ib port: " << ib_port << endl;
            break;
        }
        case '?':
            return 1;
        default:
            abort();
        }
    }
    if((argc - optind) != 3) {
        cerr << "usage: " << argv[0] << " [opts] connect_string remote_path local_path" << endl;
        return 1;
    }

    ib::Conn<> conn(ib::CONNECTOR, argv[optind], device, ib_port, pkey_index);
    cout << "waiting for connection to be established" << endl;
    conn.WaitConnected();

    FileRequest req;
    strncpy(req.filepath, argv[optind+1], 1023);
    req.filepath[1023] = '\0';
    auto remote_info = request_file(req, conn);
    cout << "raddr: " << remote_info.addr << ", rkey: " << remote_info.key
        << ", size: " << remote_info.size << endl;

    auto mr_ptr = ib::make_file_mr(conn.pd, argv[optind+2], remote_info.size);

    auto start_tp = chrono::system_clock::now();
    auto future = conn.Read(mr_ptr, remote_info.addr, remote_info.key, remote_info.size);
    bool success = future.get();
    auto time_elapsed = chrono::system_clock::now() - start_tp;
    if(!success) {
        throw std::runtime_error("read remote file failed");
    }
    cout << "read time: " <<
        chrono::duration_cast<chrono::microseconds>(time_elapsed).count() << "us" << endl;

    conn.cm_conn.PutMsg(FileDone{});
}
