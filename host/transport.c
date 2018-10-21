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

#define PACKET_BUFFER_SIZE  4096
#define AIO_BUFFER_SIZE     256

struct ring_buffer {
    unsigned char *buffer;
    size_t capacity; 
    size_t size;
    size_t offset;
};

struct tr_ctx {
    union {
        struct {
            int sock;
            struct sockaddr_in si_local;
            struct sockaddr_in si_remote;
        } network;
        struct {
            struct aiocb aio;
            struct ring_buffer rb;
        } serial;
    } data;
    ssize_t (*write)(struct tr_ctx *ctx, const unsigned char *data, size_t data_size);
    ssize_t (*read)(struct tr_ctx *ctx, unsigned char *data, size_t *data_size);
    void (*destroy)(struct tr_ctx *ctx);
};

static int rb_init(struct ring_buffer *rb, size_t capacity) 
{
    rb->buffer = malloc(capacity);
    if (!rb->buffer) {
        fprintf(stderr, "Could not allocate ring buffer\n");
        return -1;
    }
    rb->size = 0;
    rb->offset = 0;
    rb->capacity = capacity;
    return 0;
}

static int rb_write(struct ring_buffer *rb, const volatile unsigned char *data, size_t data_size) 
{
    if (rb->size + data_size > rb->capacity) {
        fprintf(stderr,"rb overflow\n");
        return -1;
    }
    size_t i0 = rb->offset + rb->size;
    for (int i = 0; i < data_size; i++) {
        rb->buffer[(i0 + i) % rb->capacity] = data[i];
    }
    rb->size += data_size;
    return 0;
}

static int rb_read(struct ring_buffer *rb, volatile unsigned char *data, size_t data_size) 
{
    if (rb->size < data_size) {
        fprintf(stderr,"rb doesn't have so much\n");
        return -1;
    }
    if (!data) {
        for (int i = 0; i < data_size; i++) {
            data[i] = rb->buffer[(rb->offset + i) % rb->capacity];
        }
    }
    rb->size -= data_size;
    rb->offset += data_size;
    return 0;
}

static void rb_destroy(struct ring_buffer *rb) 
{
    free(rb->buffer);
}

static size_t rb_available_space(struct ring_buffer *rb) 
{
    return rb->capacity - rb->size;
}

static size_t rb_available_data(struct ring_buffer *rb) 
{
    return rb->size;
}

static void aio_handler(int sig, siginfo_t *si, void *ucontext)
{
    printf("%s\n", __FUNCTION__);
    struct tr_ctx *ctx = si->si_value.sival_ptr;
    ssize_t bytes = aio_return(&ctx->data.serial.aio); 
    if (bytes == -1) {
        fprintf(stderr, "aio returned error\n");
        // TODO: cleanup
        exit(-1);
    }

    if (rb_available_space(&ctx->data.serial.rb) < bytes + sizeof(bytes)) {
        fprintf(stderr, "buffer is full, discarding packet\n");
    } else {
        if (rb_write(&ctx->data.serial.rb, (unsigned char *)&bytes, sizeof(bytes)) != 0) {
            fprintf(stderr, "can't write to rb\n");
            goto read_next;
        }
        if (rb_write(&ctx->data.serial.rb, ctx->data.serial.aio.aio_buf, bytes) != 0) {
            fprintf(stderr, "can't write to rb\n");
            goto read_next;
        }
    }
read_next:
    if (aio_read(&ctx->data.serial.aio) == -1) {
        fprintf(stderr, "could not restart operation, fatal error!\n");
        // TODO: clean-up
        exit(-1);
    }
}

static ssize_t __write_serial(struct tr_ctx *ctx, const unsigned char *data, size_t data_size)
{
    return write(ctx->data.serial.aio.aio_fildes, data, data_size);
}

static ssize_t __read_serial(struct tr_ctx *ctx, unsigned char *data, size_t data_size)
{
    while (rb_available_data(&ctx->data.serial.rb) < sizeof(size_t) + 1);
    if (aio_cancel(ctx->data.serial.aio.aio_fildes, &ctx->data.serial.aio) == AIO_NOTCANCELED) {
        while (aio_error(&ctx->data.serial.aio) == EINPROGRESS);
    }
    size_t bytes, bytes_total;
    // read packet size
    rb_read(&ctx->data.serial.rb, (volatile unsigned char *)&bytes_total, sizeof(bytes_total));
    if (bytes_total > data_size) {
        bytes = data_size;
    } else {
        bytes = bytes_total;
    }
    // packet body
    rb_read(&ctx->data.serial.rb, data, bytes);
    // discard the rest 
    rb_read(&ctx->data.serial.rb, data, bytes_total - bytes);

    aio_read(&ctx->data.serial.aio);
    return bytes;
}

static void __destroy_serial(struct tr_ctx *ctx)
{
    if (ctx) {
        close(ctx->data.serial.aio.aio_fildes);
        if (ctx->data.serial.aio.aio_buf)
            free(ctx->data.serial.aio.aio_buf);
        rb_destroy(&ctx->data.serial.rb);
        free(ctx);
    }
}

static thandle __init_serial(struct tr_param *param)
{
    int fd = open(param->value.serial.path, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        fprintf(stderr, "Could not open serial %s: %s\n", 
                param->value.serial.path,
                strerror(errno));
        return NULL;
    }
    struct termios tty;
    int res = tcgetattr(fd, &tty);
    if (res == -1) {
        fprintf(stderr, "failed to get attr: %d, %s", res, strerror(errno));
        close(fd);
        return NULL;
    }

    cfsetospeed(&tty, (speed_t)param->value.serial.baudrate);
    cfsetispeed(&tty, (speed_t)param->value.serial.baudrate);

    cfmakeraw(&tty);

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 10;

    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;    /* no HW flow control? */
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

    if (!rb_init(&h->data.serial.rb, PACKET_BUFFER_SIZE)) {
        fprintf(stderr, "can't init rb\n");
        goto error;
    }

    struct sigaction sa;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = &aio_handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        fprintf(stderr, "could not set sighandler: %s\n", strerror(errno));
        goto error;
    }

    h->data.serial.aio.aio_fildes = fd;
    h->data.serial.aio.aio_buf = malloc(AIO_BUFFER_SIZE);
    h->data.serial.aio.aio_nbytes = AIO_BUFFER_SIZE;
    h->data.serial.aio.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    h->data.serial.aio.aio_sigevent.sigev_signo = SIGCHLD;
    h->data.serial.aio.aio_sigevent.sigev_value.sival_ptr = h;

    if (aio_read(&h->data.serial.aio) == -1) {
        fprintf(stderr, "Could not start aio io: %s\n", strerror(errno));
        goto error;
    }

    h->write = &__write_serial;
    h->read = &__read_serial;
    h->destroy = &__destroy_serial;
    
    return h;
error:
    if (h) {
        close(h->data.serial.aio.aio_fildes);
        if (h->data.serial.aio.aio_buf)
            free(h->data.serial.aio.aio_buf);
        rb_destroy(&h->data.serial.rb);
        free(h);
    }
    // TODO: restore signal handler
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
    return h->write(h, data, data_size);
}

ssize_t tr_read(thandle h, unsigned char *data, size_t data_size)
{
    return h->read(h, data, data_size);
}

void tr_destroy(thandle h)
{
    return h->destroy(h);
}

