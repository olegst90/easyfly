/*********
  Rui Santos
  Complete project details at http://randomnerdtutorials.com  
*********/

// Load Wi-Fi library
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <list>
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
Camera camera(8, 8);

PT_THREAD(camera_thread(struct thr_ctx *ctx));
PT_THREAD(stream_thread(struct thr_ctx *ctx));

list<thr_ctx> threads;

void setup() {
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

PT_THREAD(stream_thread(struct thr_ctx *ctx))
{
  req_pkg pkg;
  
  PT_BEGIN(&ctx->pt);

  //ctx->user_data = 

  do {
    // frame info
    
    memset(&pkg, 0, sizeof(pkg));
    pkg.cmd = REQ_ID_STREAM_INFO;
    pkg.payload.stream_info.width = camera.width();
    pkg.payload.stream_info.height = camera.height();
    Udp.beginPacket(ctx->ip, ctx->port);
    Udp.write((uint8_t *)&pkg, sizeof(pkg));
    Udp.endPacket();
     
    PT_YIELD(&ctx->pt);
          
    // frame proper
    memset(&pkg, 0, sizeof(pkg));
    pkg.cmd = REQ_ID_STREAM_FRAME;
    pkg.payload.frame_size = camera.frameSize();
    Udp.beginPacket(ctx->ip, ctx->port);
    Udp.write((uint8_t *)&pkg, sizeof(pkg));
    Udp.write(camera.raw(), 30);//camera.frameSize());
    Udp.endPacket();
    
    PT_YIELD(&ctx->pt);
  } while (true);
  
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
}
