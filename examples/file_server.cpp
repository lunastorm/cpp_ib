#include <iostream>
#include <ib++/conn.hpp>
#include <condition_variable>

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
}
