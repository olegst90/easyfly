#include "transport.h"
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <aio.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <proto_serial.h>

struct tr_ctx {
    union {
        struct {
            int sock;
            struct sockaddr_in si_local;
            struct sockaddr_in si_remote;
        } network;
        struct {
            int fd;
        } serial;
    } data;
    ssize_t (*write)(struct tr_ctx *ctx, const unsigned char *data, size_t data_size);
    ssize_t (*read)(struct tr_ctx *ctx, unsigned char *data, size_t *data_size);
    void (*destroy)(struct tr_ctx *ctx);
};

static ssize_t __write_serial(struct tr_ctx *ctx, const unsigned char *data, size_t data_size)
{
    uint8_t b = AE_START;
    write(ctx->data.serial.fd, &b, 1);
    uint32_t u32 = AE_MAGIC;
    write(ctx->data.serial.fd, (char *)&u32, sizeof(u32));
    u32 = data_size;
    write(ctx->data.serial.fd, (char *)&u32, sizeof(u32));
    size_t res = write(ctx->data.serial.fd, data, data_size);
    tcdrain(ctx->data.serial.fd);
    return res;
}

static ssize_t __read_serial(struct tr_ctx *ctx, unsigned char *data, size_t data_size)
{
    uint32_t u32;
    uint8_t b;
    while (1) {
        if (read(ctx->data.serial.fd, (unsigned char *)&b, sizeof(b)) != sizeof(b)) {
            fprintf(stderr, "Can't read start byte: %s\n", strerror(errno));
            return -1;
        }
        if (b != AE_START) {
            fprintf(stderr, "Bad start byte %2x\n",(int)b);
            continue;
        }
        if (read(ctx->data.serial.fd, (unsigned char *)&u32, sizeof(u32)) != sizeof(u32)) {
            fprintf(stderr, "Can't read magic lo: %s\n", strerror(errno));
            return -1;
        }
        if (u32 != AE_MAGIC) {
            fprintf(stderr, "Bad magic\n");
            DumpHex(&u32, sizeof(u32));
            continue;
        }
        if (read(ctx->data.serial.fd, (unsigned char *)&u32, sizeof(u32)) != sizeof(u32)) {
            fprintf(stderr, "Can't read packet size %s\n", strerror(errno));
            return -1;
        }
        size_t to_read = u32 > data_size? data_size : u32; 
        ssize_t res = read(ctx->data.serial.fd, data, to_read);
        if (res == -1) {
            fprintf(stderr, "Can't read data:  %s\n", strerror(errno));
            return -1;
        }
        //discard the rest of the packet
        if (u32 > data_size) {
            char *tmp = malloc(u32 - data_size);
            read(ctx->data.serial.fd, tmp, u32 - data_size);
            free(tmp);
        }
        return res;
    }
}

static void __destroy_serial(struct tr_ctx *ctx)
{
    if (ctx) {
        close(ctx->data.serial.fd);
        free(ctx);
    }
}

static thandle __init_serial(struct tr_param *param)
{
    int fd = open(param->value.serial.path, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        fprintf(stderr, "Could not open serial %s: %s\n", 
                param->value.serial.path,
                strerror(errno));
        return NULL;
    }
    fcntl(fd, F_SETFL, 0);
    struct termios tty;
    int res = tcgetattr(fd, &tty);
    if (res == -1) {
        fprintf(stderr, "failed to get attr: %d, %s", res, strerror(errno));
        close(fd);
        return NULL;
    }

    cfsetospeed(&tty, (speed_t)B9600);//param->value.serial.baudrate);
    cfsetispeed(&tty, (speed_t)B9600);//param->value.serial.baudrate);

    cfmakeraw(&tty);

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    //tty.c_cflag &= ~CSTOPB;
    //tty.c_cflag &= ~CRTSCTS;    /* no HW flow control? */
    tty.c_cflag |= CLOCAL | CREAD;
    res = tcsetattr(fd, TCSANOW, &tty);
    if (res == -1) {
        fprintf(stderr, "failed to set attr: %d, %s", res, strerror(errno));
        goto error;
    }
    struct tr_ctx *h = (struct tr_ctx *)calloc(1, sizeof(*h));
    if (!h) {
        fprintf(stderr, "no memory\n");
        goto error;
    }

    h->data.serial.fd = fd;

    h->write = &__write_serial;
    h->read = &__read_serial;
    h->destroy = &__destroy_serial;
    
    return h;
error:
    if (h) {
        close(h->data.serial.fd);
        free(h);
    }
    return NULL;    
}

static ssize_t __write_network(struct tr_ctx *ctx, const unsigned char *data, size_t data_size)
{
    return sendto(ctx->data.network.sock,
                  data, data_size, 0,
                  (struct sockaddr *)&ctx->data.network.si_remote,
                  sizeof(ctx->data.network.si_remote));
}

static ssize_t __read_network(struct tr_ctx *ctx, unsigned char *data, size_t data_size)
{
    int si_remote_size = sizeof(ctx->data.network.si_remote);
    return recvfrom(ctx->data.network.sock,
                    data, data_size, 0,
                    (struct sockaddr *)&ctx->data.network.si_remote,
                    &si_remote_size); 
}

static void __destroy_network(struct tr_ctx *ctx) 
{
    close(ctx->data.network.sock);
}

static thandle __init_network(struct tr_param *param) 
{
    printf("Connecting to %s:%d, local port %d\n",
            param->value.network.remote_ip,
            param->value.network.remote_port,
            param->value.network.local_port);

    thandle h = (thandle)calloc(1,sizeof(*h));
    if (!h) {
        fprintf(stderr, "Can't allocate handle\n");
        return NULL;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        fprintf(stderr, "Could not create socket: %s\n", strerror(errno));
        return NULL;
    }

    h->data.network.sock = sock;
    h->data.network.si_local.sin_family = AF_INET;
    h->data.network.si_local.sin_port = htonl(param->value.network.local_port);
    h->data.network.si_local.sin_addr.s_addr = htons(INADDR_ANY);

    int res = bind(sock, 
                   (struct sockaddr*)&h->data.network.si_local, 
                   sizeof(h->data.network.si_local));
    if (res == -1) {
        fprintf(stderr, "Could not bind socket: %s\n", strerror(errno));
        goto error;
    }

    h->data.network.si_remote.sin_family = AF_INET;
    h->data.network.si_remote.sin_port = htons(param->value.network.remote_port); 
    inet_pton(AF_INET, 
              param->value.network.remote_ip,
              &(h->data.network.si_remote.sin_addr));

    h->write = &__write_network;
    h->read = &__read_network;
    h->destroy = &__destroy_network;
    return h;
error:
    if (h) {
        close(h->data.network.sock);
        free(h);
    }
    return NULL;
}

thandle tr_init(struct tr_param *param)
{
    if (!param) {
        return NULL;
    }

    switch (param->type) {
        case TR_SERIAL:
            return __init_serial(param);
        case TR_NETWORK:
            return __init_network(param);
        default:
            return NULL;
    }
}

ssize_t tr_write(thandle h, const unsigned char *data, size_t data_size)
{
    //printf("requested write of %d bytes\n", data_size);
    return h->write(h, data, data_size);
}

ssize_t tr_read(thandle h, unsigned char *data, size_t data_size)
{
    //printf("requested read of %d bytes\n", data_size);
    return h->read(h, data, data_size);
}

void tr_destroy(thandle h)
{
    return h->destroy(h);
}

