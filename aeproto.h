#pragma once

#define REQ_ID_GET_STREAM   0x1
#define REQ_ID_STREAM_FRAME 0x2
#define REQ_ID_STREAM_INFO  0x3

#define REQ_IP     0x1
#define REQ_PORT   0x2

typedef struct __attribute__((packed)) {
  uint16_t cmd;
  uint16_t flags;
  union {
    struct {
      uint8_t ip[4];
      int16_t port;
    } addr;

    int frame_size;
    struct {
      int width;
      int height;
    } stream_info;

    struct {

    } control_data;
  } payload;
} req_pkg;
