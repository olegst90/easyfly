#pragma once 

#include <stdint.h>

struct cctx {
  uint8_t *data;
  int line;
  int frame;
  uint8_t writable;
};

class Camera {
  int __width;
  int __height;
  int __frameSize;
  
  uint8_t *__data_1;
  uint8_t *__data_2;

  volatile struct cctx  __cctx;
  
  void write_reg(uint8_t reg, uint8_t val);
  uint8_t read_reg(uint8_t reg);
  void write_regs(const struct regval_list reglist[]);
public:
  void init();
  ~Camera();
  int width();
  int height();
  int frameSize();
  int start();

  int available();
  int read();
  
  uint8_t operator[](int i);
  const uint8_t *raw();
  volatile int currentLine();
  volatile int currentFrame();
  
};

