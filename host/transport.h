#pragma once

#include <unistd.h>

#define TR_NETWORK 0x1
#define TR_SERIAL  0x2

struct tr_param {
    int type;
    union {
        struct {
            const char *path;
            int baudrate;
        } serial;
        struct {
            const char *remote_ip;
            int remote_port;
            int local_port;
        } network;
    } value;
};

typedef struct tr_ctx *thandle;

thandle tr_init(struct tr_param *param);
ssize_t tr_write(thandle h, const unsigned char *data, size_t data_size);
ssize_t tr_read(thandle h, unsigned char *data, size_t data_size);
void tr_destroy(thandle h);

