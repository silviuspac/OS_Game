
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

//struct
typedef struct localWorld {
    int id_list[WORLD_SIZE]; //TODO aggiungere WORLD_SIZE in common.h
    int players_online;
    Vehicle** vehicles;
} localWorld;

typedef struct {
    localWorld* lw;
    struct sockaddr_in saddr;
    int tcp_socket;
    int udp_socket;
} udp_args_t;

typedef struct {
    volatile int run;
    World* world;
} UpdaterArgs;

int window;
WorldViewer viewer;
World world;
Vehicle* vehicle; // The vehicle

int myid;
int tcp_socket;
int udp_socket;

//UDP
void* UDPSender(void* args){
  int ret;
  char buff[BUFFERSIZE];

  printf("[UDPSender] Thread seander attivato...\n");

  udp_args_t* udp_args = (udp_args_t*)args;
  int udp_socket = udp_args->udp_socket;
  struct sockaddr_in saddr = udp_args->saddr;
  int saddr_len = sizeof(server_addr);

  while (1) {
    VehicleUpdatePacket* vehicle_packet = (VehicleUpdatePacket*)malloc(sizeof(VehicleUpdatePacket));
    PacketHeader header;
    header.type = VehicleUpdate;

    vehicle_packet->header = header;
    vehicle_packet->rotational_force = vehicle->rotational_force_update;
    vehicle_packet->translational_force =vehicle->translational_force_update;

    int buff_size = Packet_serialize(buff, &vehicle_packet->header);

    ret = sendto(udp_socket, buff, buff_size, 0,
                (const struct soaddr*)&seaddr, (socklen_t)saddr_len);
    ERROR_HELPER(ret, "Errore invio updates al server");

    usleep(TIME_TO_SLEEP);
  }

  printf("UDPSender chiuso...\n");

  pthread_exit(0);
}

void* UDPReceiver(void* args){
  int ret;
  int buff_size = 0;
  char buff[BUFFERSIZE];

  printf("[UDPReceiver] Ricezzione aggiornamenti...\n");

  //connessione
  udp_args_t* udp_args = (udp_args_t*)args;
  int udp_socket = udp_args->udp_socket;

  struct sockaddr_in saddr = udp_args->saddr;
  socklen_t addrlen = sizeof(struct sockaddr_in);
  while ((ret = recvfrom(udp_socket, buff, BUFFERSIZE, 0,(struct sockaddr*)&saddr, &addrlen)) > 0) {
    ERROR_HELPER(ret, "Errore nella Ricezzione dal server");

    buff_size += ret;
    WorldUpdatePacket* world_update=(WorldUpdatePacket*)Packet_deserialize(buff, buff_size);
    // Aggiorna le posizioni dei veicoli

    for (int i = 0; i < world_update->num_vehicles; i++) {
      ClientUpdate* client = &(world_update->updates[i]);

      Vehicle* client_vehicle = World_getVehicle(&world, client->id);

      printf("[UDPReceiver] Id veicolo (%d)...\n", client_vehicle->id);

      if (client_vehicle == 0) {
        Vehicle* v = (Vehicle*)malloc(sizeof(Vehicle));
        Vehicle_init(v, &world, client->id, vehicle->texture);
        World_addVehicle(&world, v);
      }

      client_vehicle = World_getVehicle(&world, client->id);

      client_vehicle->x = client->x;
      client_vehicle->y = client->y;
      client_vehicle->theta = client->theta;
    }

    World_update(&world);
  }

  printf("UDP Receiver chiuso\n");
  pthread_exit(0);
}


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
  
  // todo: connect to the server
  //   -get ad id
  //   -send your texture to the server (so that all can see you)
  //   -get an elevation map
  //   -get the texture of the surface

  //int id;

  int ret;

  //TCP
  struct sockaddr_in saddr = {0};
  uint16_t port = htons((uint16_t)PORT); //TODO common
  tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
  in_addr_t ip = inet_addr(SERVER_ADDRESS);
  ERROR_HELPER(tcp_socket, "Errore creazione socket \n");
  saddr.sin_addr.s_addr = ip;
  saddr.sin_family = AF_INET;
  saddr.sin_port = port;

  int reuseaddr = 1;
  ret = setsockopt(sdesc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
  ERROR_HELPER (ret, "Errore SO_REUSEADDR");

  ret = connect(tcp_socket, (struct sockaddr*)&saddr, sizeof(struct sockaddr_in));
  ERROR_HELPER(ret, "Errore connessione al server");
  printf("[MAIN] Connessione stabilita...\n");

  // Apertura connessione UDP
  udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(udp_socket, "[ERROR] Can't create an UDP socket");
  struct sockaddr_in udp_server = { 0 };
  udp_server.sin_addr.s_addr = inet_addr(SERVER_ADDRESS);
  udp_server.sin_family = AF_INET;
  udp_server.sin_port = htons(PORT); //TODO common


  printf("[Main] Inizializzazione ID, richieste map_elevation, map_texture\n");

  id = getID(tcp_socket);
  printf("Ricevuto ID n: %d\n", my_id);

  Image* surface_elevation = getElevationMap(sdesc);
  printf("Elevation ricevuta\n");

  Image* surface_texture = getTextureMap(sdesc);
  printf("Texture ricevuta\n");

  sendVehicleTexture(tcp_socket, my_texture, id);
  printf("Texture veicolo inviate\n");

  // construct the world
  World_init(&world, map_elevation, map_texture, 0.5, 0.5, 0.5);
  vehicle=(Vehicle*) malloc(sizeof(Vehicle));
  Vehicle_init(&vehicle, &world, id, my_texture_from_server);
  World_addVehicle(&world, vehicle);

  // spawn a thread that will listen the update messages from
  // the server, and sends back the controls
  // the update for yourself are written in the desired_*_force
  // fields of the vehicle variable
  // when the server notifies a new player has joined the game
  // request the texture and add the player to the pool
  /*FILLME*/

  pthread_t UDPSender_thread, UDPReceiver_thread;
  udp_args_t udp_args;
  udp_args.server_addr = udp_server;
  udp_args.udp_socket = udp_socket;
  udp_args.tcp_socket = tcp_socket;

  // Threads
  ret = pthread_create(&UDPSender_thread, NULL, UDPSender, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "Errore creazione UDPsender");

  ret = pthread_create(&UDPReceiver_thread, NULL, UDPReceiver, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "Errore creazione UDPReceiver");


  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // cleanup
  World_destroy(&world);
  return 0;             
}
