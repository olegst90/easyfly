#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "string.h"
#include "aeproto.h"
#include "transport.h"

int write_ppm(const uint8_t *buffer, int dimx, int dimy, const char *path)
{
  int i, j;
  FILE *fp = fopen(path, "wb"); 
  (void) fprintf(fp, "P6\n%d %d\n255\n", dimx, dimy);
  for (j = 0; j < dimy; ++j)
  {
    for (i = 0; i < dimx; ++i)
    {
      static unsigned char color[3];
      color[0] = buffer[j*dimy + i];  /* red */
      color[1] = buffer[j*dimy + i];  /* green */
      color[2] = buffer[j*dimy + i];  /* blue */
      (void) fwrite(color, 1, 3, fp);
    }
  }
  (void) fclose(fp);
  return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s ip port\n", argv[0]);
        return -1;
    }

    struct tr_param tr = {0};
    tr.type = TR_NETWORK;
    tr.value.network.remote_ip = argv[1];
    tr.value.network.remote_port = atoi(argv[2]);
    tr.value.network.local_port = 45123;
    thandle h = tr_init(&tr);
    if (!h) {
        fprintf(stderr, "Connection failure\n");
        return -1;
    }

    uint8_t buffer[64];
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
    do {
        recv_len = tr_read(h, buffer, sizeof(buffer));
        if (recv_len == -1) {
            fprintf(stderr, "Could not read header from server: %d / %s\n", recv_len, strerror(errno));
            goto exit;
        }

        //printf("received %d bytes\n", recv_len);
        switch (pkg->cmd) {
        case REQ_ID_STREAM_INFO:
            printf("<stream_info:id %d, %d x %d, %d bytes frame>\n", 
                                                     pkg->payload.stream_info.id,
                                                     pkg->payload.stream_info.width,
                                                     pkg->payload.stream_info.height,
                                                     pkg->payload.stream_info.size);
            frame_size = pkg->payload.stream_info.size;
            if (frame) free(frame);
            frame = (uint8_t *)malloc(frame_size);
            frame_width = pkg->payload.stream_info.width;
            frame_height = pkg->payload.stream_info.height;
            break;
        case REQ_ID_STREAM_FRAME:
            if (sizeof(buffer) < sizeof(*pkg) + pkg->payload.frame.fragment_size) {
                fprintf(stderr, "Buffer is too small\n");
                continue;
            }
            printf("<frame:stream %d %d bytes offset %d>\n", pkg->payload.frame.stream_id,
                                                             pkg->payload.frame.fragment_size,
                                                             pkg->payload.frame.offset);
            if (pkg->payload.frame.offset 
                 + pkg->payload.frame.fragment_size > frame_size ) {
                fprintf(stderr, "Insufficient buffer\n");
                continue;
            } 

            memcpy(frame + pkg->payload.frame.offset, 
                   buffer + sizeof(*pkg), 
                   pkg->payload.frame.fragment_size);
            if (pkg->payload.frame.offset + 
                pkg->payload.frame.fragment_size == frame_size) {
                //printf("...full frame received:\n%*s\n", frame_size, (const char *)frame);   
                write_ppm(frame, frame_width, frame_height, "pic.ppm");
                goto exit;
            }
            break;
        }
        if (--repeat_counter <= 0) {
            printf("pinging stream %d\n", pkg->payload.stream_info.id);
            pkg->cmd = REQ_ID_PING_STREAM;
            
            res = tr_write(h, pkg, sizeof(*pkg));
            if (res == -1) {
                fprintf(stderr, "Could not send to server: %s\n", strerror(errno));
                goto exit;
            }
            repeat_counter = 10;
        }

    } while (1);

exit:
    if (frame) free(frame);
    tr_destroy(h);
    return res;
}
