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
      int32_t stream_id;
      int32_t fragment_id;
      int32_t offset;
      int32_t fragment_size;
      int32_t total_size;
    } frame;

    struct {
      int32_t id;
    } stream_ping;    
    
    struct {
      int32_t id;
      int32_t width;
      int32_t height;
      int32_t size;
    } stream_info;

    struct {
      //todo
    } control_data;
  } payload;
} req_pkg;


