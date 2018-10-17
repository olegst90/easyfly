/*********
  Rui Santos
  Complete project details at http://randomnerdtutorials.com  
*********/

// Load Wi-Fi library
#include <list>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduCAM.h>
#include <pt.h>
#include "aeproto.h"
#include "Camera.h"

using namespace std;

typedef char(*fp_thread)(struct thr_ctx *ctx);

struct thr_ctx {
  struct pt pt;
  fp_thread thread;
  IPAddress ip;
  int16_t port;
  void *user_data;
};

// Replace with your network credentials
const char* ssid     = "volynska.net";
const char* password = "5Mqmx325Mqmx32";
int16_t server_port = 80;

WiFiUDP Udp;
Camera camera(32, 32);

PT_THREAD(camera_thread(struct thr_ctx *ctx));
PT_THREAD(stream_thread(struct thr_ctx *ctx));

list<thr_ctx> threads;

void setup() {
  randomSeed(analogRead(0));
  Serial.begin(9600);
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

  // starting camera thread
  struct thr_ctx ctx;
  PT_INIT(&ctx.pt);
  ctx.thread = camera_thread;
  threads.push_back(ctx);
}

struct stream_ctx {
  int id;
  int info_counter;
  int alive_counter;
  int frame_i;
  int frame_count;
};

PT_THREAD(stream_thread(struct thr_ctx *ctx))
{
  req_pkg pkg;
  int remainder;
    
  PT_BEGIN(&ctx->pt);

  ctx->user_data = new struct stream_ctx;

#define L ((struct stream_ctx *)ctx->user_data)  

  L->info_counter = 10;
  L->alive_counter = 20;
  L->id = random(1024);

  do {
    // frame info
    if (--L->info_counter <= 0) {
      Serial.println("sending info");   
      memset(&pkg, 0, sizeof(pkg));
      pkg.cmd = REQ_ID_STREAM_INFO;
      pkg.payload.stream_info.id = L->id;
      pkg.payload.stream_info.width = camera.width();
      pkg.payload.stream_info.height = camera.height();
      pkg.payload.stream_info.size = camera.frameSize();
      Udp.beginPacket(ctx->ip, ctx->port);
      Udp.write((uint8_t *)&pkg, sizeof(pkg));
      Udp.endPacket();
      L->info_counter = 20;
      PT_YIELD(&ctx->pt);
    } 
    // frame proper
    Serial.println("sending frame");
    L->frame_count = camera.frameSize() / MAX_PAYLOAD_SIZE;
    for (L->frame_i = 0; L->frame_i < L->frame_count; L->frame_i++) {
      memset(&pkg, 0, sizeof(pkg));
      pkg.cmd = REQ_ID_STREAM_FRAME;
      remainder = camera.frameSize() - L->frame_i * MAX_PAYLOAD_SIZE;
      pkg.payload.frame.fragment_size = remainder < MAX_PAYLOAD_SIZE ? remainder : MAX_PAYLOAD_SIZE;
      pkg.payload.frame.total_size = camera.frameSize();
      pkg.payload.frame.offset = L->frame_i * MAX_PAYLOAD_SIZE;
      pkg.payload.frame.fragment_id = L->frame_i; 
      pkg.payload.frame.stream_id = ((struct stream_ctx *)ctx->user_data)->id;
      Udp.beginPacket(ctx->ip, ctx->port);
      Udp.write((uint8_t *)&pkg, sizeof(pkg));
      Udp.write(camera.raw() + pkg.payload.frame.offset, pkg.payload.frame.fragment_size);
      Udp.endPacket();
      PT_YIELD(&ctx->pt);
    }
  } while (--((struct stream_ctx *)ctx->user_data)->alive_counter > 0);

  Serial.printf("Terminating abandoning stream %d\n", ((struct stream_ctx *)ctx->user_data)->id);
  
  PT_END(&ctx->pt);
}

PT_THREAD(camera_thread(struct thr_ctx *ctx))
{
  PT_BEGIN(&ctx->pt);

  while (true) {
    camera.catchFrame();
    PT_YIELD(&ctx->pt);
  }
     
  PT_END(&ctx->pt);
}

void loop(){
  int packet_size = Udp.parsePacket();
  
  if (packet_size) {
    Serial.printf("Received %d bytes from %s:%d\n", packet_size, Udp.remoteIP().toString().c_str(), Udp.remotePort());
    req_pkg pkg;
    memset(&pkg, 0, sizeof(pkg));
    int len = Udp.read((uint8_t *)&pkg, sizeof(pkg));
    if (len != sizeof(pkg)) {
      Serial.println("Wrong packet size");
      return;
    }

    if (pkg.cmd == REQ_ID_GET_STREAM) {
      struct thr_ctx ctx;
       
      if (pkg.flags & REQ_IP) {
        ctx.ip = IPAddress(pkg.payload.addr.ip[0], 
                           pkg.payload.addr.ip[1],
                           pkg.payload.addr.ip[2],
                           pkg.payload.addr.ip[3]);
      } else {
        ctx.ip = Udp.remoteIP();
      }
        
      if (pkg.flags & REQ_PORT) {
        ctx.port = pkg.payload.addr.port;
      } else {
        ctx.port = Udp.remotePort();
      }
  
      ctx.thread = &stream_thread;
      PT_INIT(&ctx.pt);
      threads.push_back(ctx);
    } else if (pkg.cmd == REQ_ID_PING_STREAM) {
      Serial.printf("Ping stream %d\n", pkg.payload.stream_ping.id);
      for (list<thr_ctx>::iterator i = threads.begin(); i != threads.end(); ++i) {
        if (i->thread == &stream_thread
            && ((struct stream_ctx *)i->user_data)->id == pkg.payload.stream_ping.id) {
              Serial.println("....stream found");
              ((struct stream_ctx *)i->user_data)->alive_counter = 20;
              break;
        }
      }
    }
  }
  
  if (!threads.empty()) {
    //Serial.println("Scheduling client threads...");
    for (list<thr_ctx>::iterator i = threads.begin(); i != threads.end(); ++i) {
      //Serial.println("...scheduling client thread");
      if (!PT_SCHEDULE(i->thread(&(*i)))) {
        Serial.println("......thread exited");
        threads.erase(i++);
      }
    }
  }
  delay(5);
}
