#include <pt.h>
#include "aeproto.h"
#include "Camera.h"
#include "ulist.h"
#include "proto_serial.h"
#include <MemoryFree.h>

#ifdef ESP8266
  #include <ESP8266WiFi.h>
  #include <WiFiUdp.h>
#endif

using namespace std;

typedef char(*fp_thread)(struct thr_ctx *ctx);

struct thr_ctx {
  struct ulist *next;
  
  struct pt pt;
  fp_thread thread;
#ifdef ESP8266
  IPAddress ip;
  int16_t port;
#endif  
  void *user_data;
};

#ifdef ESP8266
const char* ssid     = "volynska.net";
const char* password = "5Mqmx325Mqmx32";
int16_t server_port = 80;
WiFiUDP Udp;
#endif

Camera camera;

PT_THREAD(camera_thread(struct thr_ctx *ctx));
PT_THREAD(stream_thread(struct thr_ctx *ctx));

struct thr_ctx threads = {0};

void setup() {
  randomSeed(analogRead(0));
  Serial.begin(9600);
  Serial.setTimeout(200);
  camera.init();
  camera.start();
  
 #ifdef ESP8266 
  // Set output power to max
  WiFi.setOutputPower(20.5);
  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and initialize UDP
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Udp.begin(server_port);
  Serial.print("Port:");
  Serial.println(server_port);
#endif
  return;
  // starting camera thread
  struct thr_ctx *ctx = (struct thr_ctx *)malloc(sizeof(*ctx));
  PT_INIT(&ctx->pt);
  ctx->thread = camera_thread;
  UL_ADD(&threads, ctx);
}

struct stream_ctx {
  int id;
  int info_counter;
  int alive_counter;

  int fragments_per_line;
  int i;
};

PT_THREAD(stream_thread(struct thr_ctx *ctx))
{
  req_pkg pkg;
  int remainder;
  uint32_t u32;
    
  PT_BEGIN(&ctx->pt);

  ctx->user_data = new struct stream_ctx;

#define L ((struct stream_ctx *)ctx->user_data)  

  L->info_counter = STREAM_INFO_FREQ;
  L->alive_counter = STREAM_TIMEOUT;
  L->id = random(1024);

  do {
    if (--L->info_counter <= 0) {
      memset(&pkg, 0, sizeof(pkg));
      pkg.cmd = REQ_ID_STREAM_INFO;
      pkg.payload.stream_info.id = L->id;
      pkg.payload.stream_info.width = camera.width();
      pkg.payload.stream_info.height = camera.height();
      pkg.payload.stream_info.size = camera.frameSize();
 #ifdef ESP8266
      Udp.beginPacket(ctx->ip, ctx->port);
      Udp.write((uint8_t *)&pkg, sizeof(pkg));
      Udp.endPacket();
 #else
      Serial.write(AE_START);
      u32 = AE_MAGIC;
      Serial.write((uint8_t *)&u32, sizeof(u32));
      u32 = sizeof(pkg);
      Serial.write((uint8_t *)&u32, sizeof(u32));
      Serial.write((uint8_t *)&pkg, sizeof(pkg));
      //Serial.flush();
 #endif     
      L->info_counter = STREAM_INFO_FREQ;
      PT_YIELD(&ctx->pt);
    } 
    
    PT_WAIT_UNTIL(&ctx->pt, camera.available());
    L->fragments_per_line = camera.width() / MAX_PAYLOAD_SIZE;
    
    if (camera.width() % MAX_PAYLOAD_SIZE) 
      L->fragments_per_line++;
      
    for (L->i = 0; L->i < L->fragments_per_line; L->i++) {
      memset(&pkg, 0, sizeof(pkg));
      pkg.cmd = REQ_ID_STREAM_FRAME;
      pkg.payload.frame.stream_id = ((struct stream_ctx *)ctx->user_data)->id;
      pkg.payload.frame.frame_id = camera.currentFrame();
      pkg.payload.frame.line_id = camera.currentLine();
    
      remainder = camera.width() - L->i * MAX_PAYLOAD_SIZE;
      pkg.payload.frame.fragment_size = remainder < MAX_PAYLOAD_SIZE ? remainder : MAX_PAYLOAD_SIZE;
      pkg.payload.frame.total_size = camera.frameSize();
      pkg.payload.frame.offset = camera.currentLine() * camera.width() + L->i * MAX_PAYLOAD_SIZE;
      // TODO: frag id?
      pkg.payload.frame.fragment_id = 0; //L->fragment_id++; 
    
  #ifdef ESP8266      
      Udp.beginPacket(ctx->ip, ctx->port);
      Udp.write((uint8_t *)&pkg, sizeof(pkg));
      Udp.write(camera.raw() + pkg.payload.frame.offset, pkg.payload.frame.fragment_size);
      Udp.endPacket();
  #else
      Serial.write(AE_START);
      u32 = AE_MAGIC;
      Serial.write((uint8_t *)&u32, sizeof(u32));
      u32 = sizeof(pkg) + pkg.payload.frame.fragment_size;
      Serial.write((uint8_t *)&u32, sizeof(u32));
      
      Serial.write((uint8_t *)&pkg, sizeof(pkg));
      Serial.write(camera.raw() + L->i * MAX_PAYLOAD_SIZE, pkg.payload.frame.fragment_size);

      Serial.flush();
   #endif
      PT_YIELD(&ctx->pt); 
    }      
    camera.read();
  } while (--((struct stream_ctx *)ctx->user_data)->alive_counter > 0);
  PT_END(&ctx->pt);
}

PT_THREAD(camera_thread(struct thr_ctx *ctx))
{
  PT_BEGIN(&ctx->pt);

  while (true) {
    PT_YIELD(&ctx->pt);
  }
     
  PT_END(&ctx->pt);
}
void loop(){
  req_pkg pkg;
#ifdef ESP8266  
  int packet_size = Udp.parsePacket();
#else
  int packet_size = Serial.available();
#endif  
  if (packet_size) {
#ifdef ESP8266  
    Serial.printf("Received %d bytes from %s:%d\n", packet_size, Udp.remoteIP().toString().c_str(), Udp.remotePort());
#endif    
    memset(&pkg, 0, sizeof(pkg));
#ifdef ESP8266    
    int len = Udp.read((uint8_t *)&pkg, sizeof(pkg));
#else
    uint8_t start = Serial.read();
    if (start != AE_START) {
      Serial.write(0xf0);
      return;
    }
    uint32_t u32;
    Serial.readBytes((uint8_t *)&u32, sizeof(u32));
    if (u32 != AE_MAGIC) {
      Serial.write(0xf1);
      return;
    }
    Serial.readBytes((uint8_t *)&u32, sizeof(u32));
    int len =  u32;
    Serial.readBytes((uint8_t *)&pkg, sizeof(pkg));  
#endif    
    if (len != sizeof(pkg)) {
      Serial.write(0xf2);
      Serial.write(sizeof(pkg));
      Serial.write(len);
      return;
    }
    
    if (pkg.cmd == REQ_ID_GET_STREAM) {
      struct thr_ctx *ctx = (struct thr_ctx *)malloc(sizeof(*ctx)); 
 #ifdef ESP8266 
      if (pkg.flags & REQ_IP) {
        ctx->ip = IPAddress(pkg.payload.addr.ip[0], 
                           pkg.payload.addr.ip[1],
                           pkg.payload.addr.ip[2],
                           pkg.payload.addr.ip[3]);
      } else {
        ctx->ip = Udp.remoteIP();
      }
        
      if (pkg.flags & REQ_PORT) {
        ctx->port = pkg.payload.addr.port;
      } else {
        ctx->port = Udp.remotePort();
      }
 #endif
      ctx->thread = &stream_thread;
      PT_INIT(&ctx->pt);
      UL_ADD(&threads, ctx);
    } else if (pkg.cmd == REQ_ID_PING_STREAM) {
#ifdef ESP8266      
      Serial.printf("Ping stream %d\n", pkg.payload.stream_ping.id);
#endif      
      UL_FOREACH(&threads, i){
        if (((struct thr_ctx *)i)->thread == &stream_thread
            && ((struct stream_ctx *)((struct thr_ctx *)i)->user_data)->id == pkg.payload.stream_ping.id) {
              ((struct stream_ctx *)((struct thr_ctx *)i)->user_data)->alive_counter = STREAM_TIMEOUT;
              break;
        }
      }
    }
  }
  
  if (threads.next) {
    //Serial.println("Scheduling client threads...");
    UL_FOREACH (&threads, i) {
      //Serial.println("...scheduling client thread");
      if (!PT_SCHEDULE(((struct thr_ctx *)i)->thread(((struct thr_ctx *)i)))) {
        //Serial.println("......thread exited");
        UL_REMOVE(&threads, i);
      }
    }
  }
  
  //Serial.print("free memory: ");
  //Serial.println(freeMemory());
  //Serial.println(camera.get_pixels());
  
  //Serial.print(camera.get_lines());
  //Serial.print(" ");
  //Serial.println(camera.get_frames());
  
  delay(5);
}

