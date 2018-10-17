#include "Camera.h"
//#include <stdexcept>

Camera::Camera(int width, int height) 
{
  __frameSize = width * height;
  __data = new uint8_t[__frameSize];
  for (int i = 0; i < __frameSize; i++)
  {
    __data[i] = 0;
  }
  for (int i = 0; i < width; i++)
  {
    __data[i] = 0xff;
  }
  __width = width;
  __height = height;
}

Camera::~Camera() 
{
  delete [] __data;
}

int Camera::width()
{
  return __width;
}

int Camera::height()
{
  return __height;
}

int Camera::frameSize()
{
  return __frameSize;
}

uint8_t Camera::operator[](int i)
{
  if (i >= __frameSize) {
    //throw std::out_of_range("Bad frame index");
    return 0;
  }
  return __data[i];
}

void Camera::catchFrame()
{
  //so far, just fill
}

const uint8_t *Camera::raw()
{
  return __data;
}
