
#include <GL/glut.h>
#include <arpa/inet.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

#include "common.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"

//connessione
uint16_t port_number;
int sdesc = -1; //tcp
int sudp = -1;  //udp
struct timeval ultimo_aggiornamento;
struct timeval tempo_inizio;
pthread_mutex_t time = PTHREAD_MUTEX_INITIALIZER;


int window;
WorldViewer viewer;
World world;
Vehicle* vehicle; // The vehicle

int id;

//funzioni

int getID(int sdesc){
  char send[BUFFERSIZE];
  char receive[BUFFERSIZE];
  IdPacket* request = (IdPacket*)malloc(sizeof(IdPacket));
  PacketHeader header;
  header.type = GetId;
  request->header = header;
  request->id = -1;

  int size = Packet_serialize(send, &(request->header));
  if(size == -1) return -1;
  int sent = 0;
  int ret = 0;

  while(sent<size){
    ret = send(sdesc, send+sent, size-sent, 0);
    if(ret == 1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Errore richiesta id");
    if(ret == 0) break;
    sent += ret;
  }

  Packet_free(&(request->header));
  int header_len = sizeof(PacketHeader);
  int msg_len = 0;

  while(msg_len<header_len){
    ret = recv(sdesc, receive + msg_len, header_len - msg_len, 0);
    if(ret==-1 && errno==EINTR) continue;
    ERROR_HELPER(msg_len, "Errore lettura da da socket");
    msg_len += ret;
  }

  PacketHeader* h = (PacketHeader*)receive;
  size = h->size - header_len;

  msg_len = 0;
  while(msg_len < size){
    ret = recv(sdesc, receive+msg_len+header_len, size - msg_len, 0);
    if(ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len,"Errore lettura da socket");
    msg_len += ret;
  }

  IdPacket* des = (IdPacket*)Packet_deserialize(receive, msg_len+header_len),
  printf("[getID] Ricevuti %d bytes \n", msg_len+header_len);

  int id = des->id;
  Packet_free(&(des->header));

  return id;
}



int sendVehicleTexture(int socket, Image* tx, int id){
  char send[BUFFERSIZE];
  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));
  PacketHeader header;
  header.type = PostTexture;
  request->header = header;
  request->id = id;
  request->image = texture;

  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1) return -1;

  int sent = 0;
  int ret = 0;

  while (sent < size) {
    ret = send(socket, send + sent, size - sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Errore invio texture veicolo");
    if (ret == 0) break;
    sent += ret;
  }

  printf("[sendVehicleTexture] Inviati %d bytes \n", sent);
  return 0;
}

Image* getElevationMap(int socket){
  char send[BUFFERSIZE];
  char receive[BUFFERSIZE];

  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));

  PacketHeader header;
  header.type = GetElevation;

  request->header = header;
  request->id = -1;

  int size = Packet_serialize(send, &(request->header));
  if(size == -1) return NULL;

  int sent = 0;
  int ret = 0;

  while(sent < size){
    ret = send(socket, send+sent, size - sent, 0);
    if(ret==-1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Errore richiesta Elevation Map");
    if(ret==0) break;
    sent+=ret;
  }

    printf("[getElevationMap] Inviati %d bytes \n", sent);
  int msg_len = 0;
  int header_len = sizeof(PacketHeader);

  while (msg_len < header_len) {
    ret = recv(socket, receive, header_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }

  PacketHeader* in_pak = (PacketHeader*)receive;
  size = in_pak->size - header_len;
  msg_len = 0;

  while (msg_len < size) {
    ret = recv(socket, receive + msg_len + header_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Cannot read from socket");
    msg_len += ret;
  }

  ImagePacket* des =
      (ImagePacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[getElevationMap] Ricevuti %d bytes \n", msg_len + header_len);
  Packet_free(&(request->header));
  Image* res = des->image;
  free(des);
  return res;

}

Image* getTextureMap(int socket) {
  char send[BUFFERSIZE];
  char receive[BUFFERSIZE];

  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));

  PacketHeader header;
  header.type = GetTexture;
  request->header = header;
  request->id = -1;

  int size = Packet_serialize(send, &(request->header));
  if (size == -1) return NULL;

  int sent = 0;
  int ret = 0;

  while (sent < size) {
    ret = send(socket, send + sent, size - sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Send Error");
    if (ret == 0) break;
    sent += ret;
  }

  printf("[getTextureMap] Inviati %d bytes \n", sent);
  int msg_len = 0;
  int header_len = sizeof(PacketHeader);

  while (msg_len < header_len) {
    ret = recv(socket, receive, header_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Errore lettura socket");
    msg_len += ret;
  }

  PacketHeader* in_pak = (PacketHeader*)receive;
  size = in_pak->size - header_len;
  printf("[getTextureMap] Size da leggere %d \n", size);

  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, receive + msg_len + header_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Errore lettura socket");
    msg_len += ret;
  }

  ImagePacket* des =
      (ImagePacket*)Packet_deserialize(receive, msg_len + header_len);
  printf("[getTextureMap] Ricevuti %d bytes \n", msg_len + header_len);
  Packet_free(&(request->header));
  Image* res = des->image;
  free(des);
  return res;
}





int main(int argc, char **argv) {
  if (argc<3) {
    printf("usage: %s <server_address> <player texture>\n", argv[1]);
    exit(-1);
  }

  printf("loading texture image from %s ... ", argv[2]);
  Image* my_texture = Image_load(argv[2]);
  if (my_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }
  
  Image* my_texture_for_server;
  // todo: connect to the server
  //   -get ad id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface

  // these come from the server
  int my_id;
  Image* map_elevation;
  Image* map_texture;
  Image* my_texture_from_server;

  printf("[Main] Starting...\n");
  port_number = htons((uint16_t)8888);

  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  in_addr_t server_ip = inet_addr("127.0.0.1");
  ERROR_HELPER(socket_desc, "Errore creazione socket");

  struct sockaddr_in server_addr = {0};
  server_addr.sin_addr.s_addr = server_ip;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = port_number;

  int reuseaddr = 1;
  ret = setsockopt(sdesc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
  ERROR_HELPER (ret, "Errore SO_REUSEADDR");

  ret = connect(sdesc, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in));
  ERROR_HELPER(ret, "Errore connessione al server");

  fprintf(stdout, "[Main] Inizializzazione ID, richieste map_elevation, map_texture\n");

  my_id = getID(sdesc);
  fprintf(stdout, "Ricevuto id n: %d\n", my_id);

  Image* surface_elevation = getElevationMap(sdesc);
  fprintf(stdout, "Elevation ricevuta\n");
  Image* surface_texture = getTextureMap(sdesc);
  fprintf(stdout, "Texture ricevuta\n");

  sendVehicleTexture(socket_desc, my_texture, id);
  fprintf(stdout, "Texture veicolo inviate\n");





  // construct the world
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle=(Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(&vehicle, &world, my_id, my_texture_from_server);
  World_addVehicle(&world, vehicle);

  // spawn a thread that will listen the update messages from
  // the server, and sends back the controls
  // the update for yourself are written in the desired_*_force
  // fields of the vehicle variable
  // when the server notifies a new player has joined the game
  // request the texture and add the player to the pool
  /*FILLME*/

  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // cleanup
  World_destroy(&world);
  return 0;             
}
