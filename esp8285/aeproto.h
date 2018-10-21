#pragma once

#include <stdint.h>

#define REQ_ID_GET_STREAM        0x1
#define REQ_ID_PING_STREAM       0x2
#define REQ_ID_STREAM_FRAME      0x3
#define REQ_ID_STREAM_INFO       0x4

#define REQ_IP                   0x1
#define REQ_PORT                 0x2

#define MAX_PAYLOAD_SIZE         32

typedef struct __attribute__((packed)) {
  uint16_t cmd;
  uint16_t flags;
  union {
    struct {
      uint8_t ip[4];
      int16_t port;
    } addr;

    struct {
      int stream_id;
      int fragment_id;
      int offset;
      int fragment_size;
      int total_size;
    } frame;

    struct {
      int id;
    } stream_ping;    
    
    struct {
      int id;
      int width;
      int height;
      int size;
    } stream_info;

    struct {
      //todo
    } control_data;
  } payload;
} req_pkg;
