#include <Arduino.h>
#include "Camera.h"
#include "ov7670.h"
#include <Wire.h>

#ifdef ARDUINO_AVR_LEONARDO
  #include <avr/io.h>
  #include <avr/interrupt.h>
#endif

// qcif 176 (line) * 144
#define QCIF_W 176
#define QCIF_H 144


  
static volatile uint8_t *g_data_hw = NULL;
static volatile int g_line_width = 0;

static volatile struct cctx *g_cctx;

static volatile unsigned int g_frame = 0;
static volatile unsigned int g_frame_ready = 0;

static volatile unsigned int g_line = 0;
static volatile unsigned int g_line_ready = 0;
static volatile unsigned int g_pclk = 0;

void Camera::init() 
{
  __width = QCIF_W;
  __height = QCIF_H;
  __frameSize = __width * __height;
  __data_1 = new uint8_t[__width];
  __data_2 = new uint8_t[__width];
  for (int i = 0; i < __width; i++)
  {
    __data_1[i] = i;
    __data_2[i] = i;
  }
  
  g_cctx = &__cctx;
  g_line_width = __width;

  return;
  //DEBUG
  __width = 20;
  __height = 20;
  __frameSize = __width * __height;
}

Camera::~Camera() 
{
  delete [] __data_1;
  delete [] __data_2;
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
    return 0;
  }
  return __cctx.data[i];
}

int Camera::available()
{
  return !__cctx.writable;
}

int Camera::read()
{
  __cctx.writable = 1;
}
  
const uint8_t *Camera::raw()
{
  return __cctx.data;
}

// todo: delete?
ISR(TIMER4_COMPA_vect)
{

}

void Camera::write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(camAddr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t Camera::read_reg(uint8_t reg) {
    Wire.beginTransmission(camAddr);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom(camAddr, 1);
    return Wire.read();
}

void Camera::write_regs(const struct regval_list reglist[]) {
  uint8_t reg_addr, reg_val;
  const struct regval_list *next = reglist;
  while ((reg_addr != 0xff) | (reg_val != 0xff)){
    reg_addr = pgm_read_byte(&next->reg_num);
    reg_val = pgm_read_byte(&next->value);
    write_reg(reg_addr, reg_val);
    delay(50);
    next++;
  }
}

volatile int Camera::currentLine()
{
  return __cctx.line;
}

volatile int Camera::currentFrame()
{
  return __cctx.frame;
}

// on rising
void isr_pclk(void) {
  int i = g_pclk / 2;
  if (i >= g_line_width) {
    detachInterrupt(digitalPinToInterrupt(7));
    return;
  }
  int r = g_pclk % 2;
  if (r) {
    uint8_t p = digitalRead(11);
    p <<= 1;
    p |= digitalRead(10);
    p <<= 1;
    p |= digitalRead(A5);
    p <<= 1;
    p |= digitalRead(A4);
    p <<= 1;
    p |= digitalRead(A3);
    p <<= 1;
    p |= digitalRead(A2);
    p <<= 1;
    p |= digitalRead(A1);
    p <<= 1;
    p |= digitalRead(A0);
    
    
    g_data_hw[i] = p;
  }
  g_pclk++;
}

static volatile byte int_8 = 0, int_9 = 0;

ISR(PCINT0_vect)
{ 
  // hs(8) falling
  if (int_8 && !digitalRead(8)) {
    //falling
    /* END OF LINE */
    
    // swap buffers
    if (g_cctx->writable) {
      uint8_t *tmp = g_data_hw;
      g_data_hw = g_cctx->data;
      g_cctx->data = tmp;
      g_cctx->line = g_line;
      //DEBUG
      //g_cctx->line = g_line % 20;
      g_cctx->frame = g_frame;
      g_cctx->writable = 0;
    }
    g_line++;     
    g_line_ready = 1;
    //digitalWrite(4,HIGH);
    int_8 = 0;
  } else if (!int_8 && digitalRead(8)) {
      // rising
      /* NEW LINE */
      g_pclk = 0;
      g_line_ready = 0;
      attachInterrupt(digitalPinToInterrupt(7), isr_pclk, RISING);
      //digitalWrite(4,LOW);
      int_8  = 1;
  }

  if (int_9 && !digitalRead(9)) {
      //falling
      /* NEW FRAME */
      g_frame_ready = 0;
      g_line = 0;
      int_9 = 0;
  } else if (!int_9 && digitalRead(9)) {
      // rising
      /* END OF FRAME */
      g_frame++;
      g_frame_ready = 1;
      int_9  = 1;
  }
}


int Camera::start()
{ 
  __cctx.data = __data_1;
  g_data_hw = __data_2;
  __cctx.writable = 1;
  
#ifdef ARDUINO_AVR_LEONARDO
  sei(); 
  /* setting 6 mhz clock on 13 port (pc7) */
  // port to output
  DDRC |= (1<<DDC7);   
  // 48 mhz pll by default
  
  //PLLFRQ = (1 << PDIV2);// | (1 << PDIV1);
  // enable and lock PLL
  //PLLCSR = (1<<PLLE);
  //while((PLLCSR & (1<<PLOCK)) == 0);

  //96 postscaler 2 = 48mhz
  PLLFRQ |= (1 << PLLTM0) | (1 << PLLTM1) ;
  // enable  pwm on ocr4a == pc7
  TCCR4A = (1 << PWM4A);

  // 48mHz/4 = 12 mhz
  TCCR4B = (1 << CS41);
  TCCR4C = 0;
  TCCR4D = 0;

  // enable interrupt
  //TIMSK4 |= (1<< OCIE4A);
  
  // 12/2 = 6mhz
  OCR4A = 1;
  OCR4C = 2;

  // start
  TCCR4A |= (1 << COM4A1);

  /* start TWI */
  Wire.begin();
  delay(100);
  
  // 0x12 0x80
  write_reg(REG_COM7, COM7_RESET);
  delay(100);
  
  write_reg(REG_COM7, 1 << 3); //qcif

  // yuv 422
  write_reg(REG_TSLB, 0x14);
  write_reg(REG_COM13,  0x88);
  
  // slow down 
  write_reg(REG_CLKRC, 0x9f);

  // enable scaling
  //write_reg(REG_COM14, 0x0e | (1 << 3));
  
  
  // enable pin-change interrupts
  PCICR |= (1 << PCIE0);
  //pins 8, 9, 10, 11
  PCMSK0 |= (1 << PCINT4) | (1 << PCINT5);
  pinMode(8, INPUT);
  pinMode(9, INPUT);

  // 7 is the only hardware interrupt pin, so it will serve the fastest PCLK signal
  pinMode(7, INPUT);
  attachInterrupt(digitalPinToInterrupt(7), isr_pclk, RISING);

  //data pins
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  pinMode(A2, INPUT);
  pinMode(A3, INPUT);
  pinMode(A4, INPUT);
  pinMode(A5, INPUT);
  pinMode(10, INPUT);
  pinMode(11, INPUT);
#else
  #error only leonardo is supported
#endif
}

