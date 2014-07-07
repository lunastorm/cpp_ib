#ifndef FILE_REQUEST_HPP_
#define FILE_REQUEST_HPP_

struct FileRequest {
    char filepath[1024];
};

struct FileResponse {
    uint64_t addr;
    uint64_t size;
    uint32_t key;
};

struct FileDone {
};

#endif
