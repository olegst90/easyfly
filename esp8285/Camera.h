#pragma once 

#include <stdint.h>

class Camera {
  int __width;
  int __height;
  int __frameSize;
  uint8_t *__data;
public:
  Camera(int width, int height);
  ~Camera();
  int width();
  int height();
  int frameSize();
  uint8_t operator[](int i);
  const uint8_t *raw();
  void catchFrame();
};
