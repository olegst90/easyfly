#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "string.h"
#include "aeproto.h"
#include "transport.h"
#include "utils.h"
#include <time.h>

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s n <ip> <port>\n"
                        "       %s s <serial> <baudrate>", argv[0]);
        return -1;
    }

    struct tr_param tr = {0};
    if (strcmp(argv[1],"s") == 0) {
        tr.type = TR_SERIAL;
        tr.value.serial.path = argv[2];
        tr.value.serial.baudrate = atoi(argv[3]);
    } else if (strcmp(argv[1],"n") == 0) {
        tr.type = TR_NETWORK;
        tr.value.network.remote_ip = argv[2];
        tr.value.network.remote_port = atoi(argv[3]);
        tr.value.network.local_port = 45123;
    }
    thandle h = tr_init(&tr);
    if (!h) {
        fprintf(stderr, "Connection failure\n");
        return -1;
    }

    uint8_t buffer[256] = {0};
    uint8_t *frame = NULL;
    int frame_size = 0;
    int frame_width, frame_height;
    req_pkg *pkg = (req_pkg *)buffer;
    memset(pkg, 0, sizeof(pkg));
    pkg->cmd = REQ_ID_GET_STREAM;

    int res = tr_write(h, pkg, sizeof(*pkg));
    if (res == -1) {
        fprintf(stderr, "Could not send to server: %s\n", strerror(errno));
        goto exit;
    }

    printf("Waiting for data...\n");

    int recv_len;
    int repeat_counter = 10; 
    int frame_id = 0;
    int frame_finished = 0;
    do {
        // ping stream
        if (--repeat_counter <= 0) {
            printf("pinging stream %d\n", pkg->payload.stream_info.id);
            pkg->cmd = REQ_ID_PING_STREAM;
            
            res = tr_write(h, pkg, sizeof(*pkg));
            if (res == -1) {
                fprintf(stderr, "Could not send to server: %s\n", strerror(errno));
                goto exit;
            }
            repeat_counter = 10;
            usleep(10*1000);
        }
        // read
        recv_len = tr_read(h, buffer, sizeof(buffer));
        if (recv_len == -1) {
            fprintf(stderr, "Could not read header from server: %d / %s\n", recv_len, strerror(errno));
            goto exit;
        }

        switch (pkg->cmd) {
        case REQ_ID_STREAM_INFO:
            printf("<stream_info:id %d, %d x %d, %d bytes frame>\n", 
                                                     pkg->payload.stream_info.id,
                                                     pkg->payload.stream_info.width,
                                                     pkg->payload.stream_info.height,
                                                     pkg->payload.stream_info.size);
            if (!frame) {
                frame_size = pkg->payload.stream_info.size;
                frame = (uint8_t *)calloc(1, frame_size);
                frame_width = pkg->payload.stream_info.width;
                frame_height = pkg->payload.stream_info.height;
            }
            break;
        case REQ_ID_STREAM_FRAME:
            if (!frame) {
                printf("no info yet, skipping...\n");
                continue;
            }

            if (sizeof(buffer) < sizeof(*pkg) + pkg->payload.frame.fragment_size) {
                fprintf(stderr, "Buffer is too small\n");
                continue;
            }

            printf("<frame:stream %d frame %d line %d size %d offset %d fragment %d>\n",
                                                                                 pkg->payload.frame.stream_id,
                                                                                 pkg->payload.frame.frame_id,
                                                                                 pkg->payload.frame.line_id,
                                                                                 pkg->payload.frame.fragment_size,
                                                                                 pkg->payload.frame.offset,
                                                                                 pkg->payload.frame.fragment_id);
            // check if this is a new frame
            if (frame_id != pkg->payload.frame.frame_id) {
               printf("new frame detected\n");
               char name[64];
               sprintf(name,"pic_%02d.ppm", frame_id);
               write_ppm(frame, frame_width, frame_height, name);
               frame_id = pkg->payload.frame.frame_id;
               memset(frame, 0, frame_size);
            }

            if (pkg->payload.frame.offset 
                 + pkg->payload.frame.fragment_size > frame_size ) {
                fprintf(stderr, "Insufficient buffer: need %d, has %d\n",
                                  pkg->payload.frame.offset + pkg->payload.frame.fragment_size, frame_size);
                goto exit;
            } 

            memcpy(frame + pkg->payload.frame.offset, 
                   buffer + sizeof(*pkg), 
                   pkg->payload.frame.fragment_size);
            break;
        }

    } while (1);

exit:
    if (frame) free(frame);
    tr_destroy(h);
    return res;
}
