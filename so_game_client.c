
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
#include <fcntl.h>
#include <netinet/in.h>

#include "common.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"

int sendGoodbye(int socket, int id);

//struct
typedef struct localWorld {
    int id_list[WORLD_SIZE];
    int players_online;
    char hv[WORLD_SIZE];
    struct timeval vlt[WORLD_SIZE]; //vehicle login
    Vehicle** vehicles;
} localWorld;

typedef struct lArgs{
    localWorld* lw;
    struct sockaddr_in saddr;
    int tcp_socket;
    int udp_socket;
} udpArgs;


int window;
World world;
Vehicle* vehicle; // The vehicle
int myid;

// Network
uint16_t nport;
int sdesc = -1;  
int udp_socket = -1;
struct timeval last_update_time;
struct timeval start_time;
pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

//flags
char connectivity = 1;
char exchange_update = 1;

int addUser(int id_list[], int size, int id2, int* position, int* players_online) {
  if (*players_online == WORLD_SIZE) {
    *position = -1;
    return -1;
  }
  for (int i = 0; i < size; i++) {
    if (id_list[i] == id2) {
      return i;
    }
  }
  for (int i = 0; i < size; i++) {
    if (id_list[i] == -1) {
      id_list[i] = id2;
      *players_online += 1;
      *position = i;
      break;
    }
  }
  return -1;
}

// Richiedere Texture Veicolo
Image* getVehicleTexture(int socket, int id) {
  char buf_send[BUFFERSIZE];
  char buf_rcv[BUFFERSIZE];
  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));
  PacketHeader ph;
  ph.type = GetTexture;
  request->header = ph;
  request->id = id;
  int size = Packet_serialize(buf_send, &(request->header));
  if (size == -1) return NULL;
  int bytes_sent = 0;
  int ret = 0;
  while (bytes_sent < size) {
    ret = send(socket, buf_send + bytes_sent, size - bytes_sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Can't request a texture of a vehicle");
    if (ret == 0) break;
    bytes_sent += ret;
  }
  Packet_free(&(request->header));

  int ph_len = sizeof(PacketHeader);
  int msg_len = 0;
  while (msg_len < ph_len) {
    ret = recv(socket, buf_rcv + msg_len, ph_len - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }
  PacketHeader* header = (PacketHeader*)buf_rcv;
  size = header->size - ph_len;
  char flag = 0;
  if (header->type == PostDisconnect) flag = 1;
  msg_len = 0;
  while (msg_len < size) {
    ret = recv(socket, buf_rcv + msg_len + ph_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(msg_len, "Cannot read from socket");
    msg_len += ret;
  }

  if (flag) {
    IdPacket* packet = (IdPacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
    Packet_free(&packet->header);
    return NULL;
  }
  ImagePacket* deserialized_packet =
      (ImagePacket*)Packet_deserialize(buf_rcv, msg_len + ph_len);
  printf("[Get Vehicle Texture] Received %d bytes \n", msg_len + ph_len);
  Image* im = deserialized_packet->image;
  free(deserialized_packet);
  return im;
}

//UDP
int sendUpdates(int udp_socket, struct sockaddr_in saddr, int serverlen) {
  char sendb[BUFFERSIZE];
  PacketHeader header;
  header.type = VehicleUpdate;
  VehicleUpdatePacket* vup = (VehicleUpdatePacket*)malloc(sizeof(VehicleUpdatePacket));
  vup->header = header;
  gettimeofday(&vup->time, NULL);
  pthread_mutex_lock(&vehicle->mutex);
  Vehicle_getForcesIntention(vehicle, &(vup->translational_force), &(vup->rotational_force));
  Vehicle_setForcesIntention(vehicle, 0, 0);
  pthread_mutex_unlock(&vehicle->mutex);
  vup->id = myid;
  int size = Packet_serialize(sendb, &vup->header);
  int sent = sendto(udp_socket, sendb, size, 0,
             (const struct sockaddr*)&saddr, (socklen_t)serverlen);
  printf(
      "[UDPSender] Inviati aggiornamenti veicolo di %d bytes con tf:%f rf:%f \n",
      sent, vup->translational_force, vup->rotational_force);
  Packet_free(&(vup->header));
  struct timeval current_time;
  gettimeofday(&current_time, NULL);
  if (sent < 0) return -1;
  return 0;
}

void* UDPSender(void* args){

  udpArgs udp_args = *(udpArgs*)args;
  int udp_socket = udp_args.udp_socket;
  struct sockaddr_in saddr = udp_args.saddr;
  int saddr_len = sizeof(saddr);

  while (connectivity && exchange_update) {
    int ret = sendUpdates(udp_socket, saddr, saddr_len);
    ERROR_HELPER(ret, "Errore invio updates al server");

    usleep(TIME_TO_SLEEP);
  }
  pthread_exit(NULL);
}

void* UDPReceiver(void* args) {

  udpArgs udp_args = *(udpArgs*)args;
  struct sockaddr_in saddr = udp_args.saddr;
  int udp_socket = udp_args.udp_socket;
  socklen_t saddrlen = sizeof(saddr);
  localWorld* lw = udp_args.lw;
  int tcp_socket = udp_args.tcp_socket;
  while (connectivity && exchange_update) {
    char receive[BUFFERSIZE];
    int read = recvfrom(udp_socket, receive, BUFFERSIZE, 0,
                              (struct sockaddr*)&saddr, &saddrlen);
    if (read == -1) {
      printf("[UDPReceiver] Impossibile riceve pacchetto \n");
      usleep(RECEIVER_SLEEP_C);
      continue;
    }
    if (read == 0) {
      usleep(RECEIVER_SLEEP_C);
      continue;
    }

    printf("[UDPReceiver] Ricevuti %d bytes\n", read);
    PacketHeader* header = (PacketHeader*)receive;
    if (header->size != read) {
      printf("[UDPReceiver] Lettura parziale del pacchetto \n");
      usleep(RECEIVER_SLEEP_C);
      continue;
    }
    switch (header->type) {
      case (PostDisconnect): {
        sendGoodbye(sdesc, myid);
        connectivity = 0;
        exchange_update = 0;
        WorldViewer_exit(0);
      }
      case (WorldUpdate): {
        WorldUpdatePacket* world_update =
            (WorldUpdatePacket*)Packet_deserialize(receive, read); 
        pthread_mutex_lock(&time_lock);
        if (last_update_time.tv_sec != -1 &&
            timercmp(&last_update_time, &world_update->time, >=)) {
          pthread_mutex_unlock(&time_lock);
          printf("Letture parziale di world update... \n");
          Packet_free(&world_update->header);
          usleep(RECEIVER_SLEEP_C);
          continue;
        }

        printf("WorldUpdatePacket contiene %d veicoli apparte me \n",
               world_update->num_vehicles - 1);
        last_update_time = world_update->time;
        pthread_mutex_unlock(&time_lock);
        char mask[WORLD_SIZE];
        for (int k = 0; k < WORLD_SIZE; k++) mask[k] = UNTOUCHED;
        for (int i = 0; i < world_update->num_vehicles; i++) {
          int new_position = -1;
          int id_struct = addUser(lw->id_list, WORLD_SIZE, world_update->updates[i].id,
                                  &new_position, &(lw->players_online));
          if (world_update->updates[i].id == myid) {
            pthread_mutex_lock(&lw->vehicles[0]->mutex);
            Vehicle_setXYTheta(lw->vehicles[0], world_update->updates[i].x,
                               world_update->updates[i].y, world_update->updates[i].theta);
            Vehicle_setForcesUpdate(lw->vehicles[0],
                                    world_update->updates[i].translational_force,
                                    world_update->updates[i].rotational_force);
            World_manualUpdate(&world, lw->vehicles[0],
                               world_update->updates[i].client_update_time);
            pthread_mutex_unlock(&lw->vehicles[0]->mutex);
          } else if (id_struct == -1) {
            if (new_position == -1) continue;
            mask[new_position] = TOUCHED;
            printf("New Vehicle with id %d and x: %f y: %f z: %f \n",
                   world_update->updates[i].id, world_update->updates[i].x, 
                   world_update->updates[i].y,
                   world_update->updates[i].theta);
            Image* img = getVehicleTexture(tcp_socket, world_update->updates[i].id);
            if (img == NULL) continue;
            Vehicle* new_vehicle = (Vehicle*)malloc(sizeof(Vehicle));
            Vehicle_init(new_vehicle, &world, world_update->updates[i].id, img);
            lw->vehicles[new_position] = new_vehicle;
            pthread_mutex_lock(&lw->vehicles[new_position]->mutex);
            Vehicle_setXYTheta(lw->vehicles[new_position], world_update->updates[i].x,
                               world_update->updates[i].y, world_update->updates[i].theta);
            Vehicle_setForcesUpdate(lw->vehicles[new_position],
                                    world_update->updates[i].translational_force,
                                    world_update->updates[i].rotational_force);
            pthread_mutex_unlock(&lw->vehicles[new_position]->mutex);
            World_addVehicle(&world, new_vehicle);
            lw->hv[new_position] = 1;
            lw->vlt[new_position] =
                world_update->updates[i].client_creation_time;
          } else {
            mask[id_struct] = TOUCHED;
            if (timercmp(&world_update->updates[i].client_creation_time,
                         &lw->vlt[id_struct], !=)) {
              printf("[WARNING] Forcing refresh for client with id %d",
                     world_update->updates[i].id);
              if (lw->hv[id_struct]) {
                Image* im = lw->vehicles[id_struct]->texture;
                World_detachVehicle(&world, lw->vehicles[id_struct]);
                Vehicle_destroy(lw->vehicles[id_struct]);
                if (im != NULL) Image_free(im);
                free(lw->vehicles[id_struct]);
              }
              Image* img = getVehicleTexture(tcp_socket, world_update->updates[i].id);
              if (img == NULL) continue;
              Vehicle* new_vehicle = (Vehicle*)malloc(sizeof(Vehicle));
              Vehicle_init(new_vehicle, &world, world_update->updates[i].id, img);
              lw->vehicles[id_struct] = new_vehicle;
              pthread_mutex_lock(&lw->vehicles[id_struct]->mutex);
              Vehicle_setXYTheta(lw->vehicles[id_struct], world_update->updates[i].x,
                                 world_update->updates[i].y, world_update->updates[i].theta);
              Vehicle_setForcesUpdate(lw->vehicles[id_struct],
                                      world_update->updates[i].translational_force,
                                      world_update->updates[i].rotational_force);
              World_manualUpdate(&world, lw->vehicles[id_struct],
                                 world_update->updates[i].client_update_time);
              pthread_mutex_unlock(&lw->vehicles[id_struct]->mutex);
              World_addVehicle(&world, new_vehicle);
              lw->hv[id_struct] = 1;
              lw->vlt[id_struct] =
                  world_update->updates[i].client_creation_time;
              continue;
            }
            printf("Updating veicolo con id %d and x: %f y: %f z: %f \n",
                   world_update->updates[i].id, world_update->updates[i].x, world_update->updates[i].y,
                   world_update->updates[i].theta);
            pthread_mutex_lock(&lw->vehicles[id_struct]->mutex);
            Vehicle_setXYTheta(lw->vehicles[id_struct], world_update->updates[i].x,
                               world_update->updates[i].y, world_update->updates[i].theta);
            Vehicle_setForcesUpdate(lw->vehicles[id_struct],
                                    world_update->updates[i].translational_force,
                                    world_update->updates[i].rotational_force);
            World_manualUpdate(&world, lw->vehicles[id_struct],
                               world_update->updates[i].client_update_time);
            pthread_mutex_unlock(&lw->vehicles[id_struct]->mutex);
          }
        }
        for (int i = 0; i < WORLD_SIZE; i++) {
          if (mask[i] == TOUCHED) continue;
          if (i == 0) continue;

          if (lw->id_list[i] == myid) continue;
          if (mask[i] == UNTOUCHED && lw->id_list[i] != -1) {
            printf("[WorldUpdate] Rimozione veicolo con ID %d \n", lw->id_list[i]);
            lw->players_online = lw->players_online - 1;
            if (!lw->hv[i]) continue;
            Image* im = lw->vehicles[i]->texture;
            World_detachVehicle(&world, lw->vehicles[i]);
            if (im != NULL) Image_free(im);
            Vehicle_destroy(lw->vehicles[i]);
            lw->id_list[i] = -1;
            free(lw->vehicles[i]);
            lw->hv[i] = 0;
          }
        }
        Packet_free(&world_update->header);
        break;
      }
      default: {
        printf(
            "[UDP_Receiver] Found an unknown udp packet. Terminating the "
            "client now... \n");
        sendGoodbye(sdesc, myid);
        connectivity = 0;
        exchange_update = 0;
        WorldViewer_exit(-1);
      }
    }
    usleep(RECEIVER_SLEEP_C);
  }
  pthread_exit(NULL);
}


//funzioni

int getID(int sdesc){
  char sendb[BUFFERSIZE];
  char receive[BUFFERSIZE];
  IdPacket* request = (IdPacket*)malloc(sizeof(IdPacket));
  PacketHeader header;
  header.type = GetId;
  request->header = header;
  request->id = -1;

  int size = Packet_serialize(sendb, &(request->header));
  if(size == -1) return -1;
  int sent = 0;
  int ret = 0;

  while(sent<size){
    ret = send(sdesc, sendb + sent, size - sent, 0);
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

  IdPacket* des = (IdPacket*)Packet_deserialize(receive, msg_len+header_len);
  printf("[getID] Ricevuti %d bytes \n", msg_len+header_len);

  int id = des->id;
  Packet_free(&(des->header));

  return id;
}



int sendVehicleTexture(int socket, Image* tx, int id){
  char sendb[BUFFERSIZE];
  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));
  PacketHeader header;
  header.type = PostTexture;
  request->header = header;
  request->id = id;
  request->image = tx;

  int size = Packet_serialize(sendb, &(request->header));
  if (size == -1) return -1;

  int sent = 0;
  int ret = 0;

  while (sent < size) {
    ret = send(socket, sendb + sent, size - sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Errore invio texture veicolo");
    if (ret == 0) break;
    sent += ret;
  }

  printf("[sendVehicleTexture] Inviati %d bytes \n", sent);
  return 0;
}

Image* getElevationMap(int socket){
  char sendb[BUFFERSIZE];
  char receive[BUFFERSIZE];

  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));

  PacketHeader header;
  header.type = GetElevation;

  request->header = header;
  request->id = -1;

  int size = Packet_serialize(sendb, &(request->header));
  if(size == -1) return NULL;

  int sent = 0;
  int ret = 0;

  while(sent < size){
    ret = send(socket, sendb+sent, size - sent, 0);
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
      (ImagePacket*)Packet_deserialize(receive, msg_len + header_len);
  printf("[getElevationMap] Ricevuti %d bytes \n", msg_len + header_len);
  Packet_free(&(request->header));
  Image* res = des->image;
  free(des);
  return res;

}

Image* getTextureMap(int socket) {
  char sendb[BUFFERSIZE];
  char receive[BUFFERSIZE];

  ImagePacket* request = (ImagePacket*)malloc(sizeof(ImagePacket));

  PacketHeader header;
  header.type = GetTexture;
  request->header = header;
  request->id = -1;

  int size = Packet_serialize(sendb, &(request->header));
  if (size == -1) return NULL;

  int sent = 0;
  int ret = 0;

  while (sent < size) {
    ret = send(socket, sendb + sent, size - sent, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Errore invio");
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


int sendGoodbye(int socket, int id) {
  char sendb[BUFFERSIZE];
  IdPacket* idpckt = (IdPacket*)malloc(sizeof(IdPacket));
  PacketHeader header;
  header.type = PostDisconnect;
  idpckt->id = id;
  idpckt->header = header;
  int size = Packet_serialize(sendb, &(idpckt->header));
  printf("[Goodbye] Sending goodbye  \n");
  int msg_len = 0;
  while (msg_len < size) {
    int ret = send(socket, sendb + msg_len, size - msg_len, 0);
    if (ret == -1 && errno == EINTR) continue;
    ERROR_HELPER(ret, "Can't send goodbye");
    if (ret == 0) break;
    msg_len += ret;
  }
  printf("[Goodbye] Goodbye was successfully sent %d \n", msg_len);
  return 0;
}




int main(int argc, char** argv) {
  if (argc < 3) {
    printf("usage: %s <player texture> <port_number> \n", argv[1]);
    exit(-1);
  }
  fprintf(stdout, "[Main] loading vehicle texture from %s ... ", argv[1]);
  Image* my_texture = Image_load(argv[1]);
  if (my_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }
  long tmp = strtol(argv[2], NULL, 0);

  fprintf(stdout, "[Main] Starting... \n");
  last_update_time.tv_sec = -1;
  nport = htons((uint16_t)tmp);  // we use network byte order
  sdesc = socket(AF_INET, SOCK_STREAM, 0);
  in_addr_t ip_addr = inet_addr(SERVER_ADDRESS);
  ERROR_HELPER(sdesc, "Cannot create socket \n");
  struct sockaddr_in server_addr = {
      0};  // some fields are required to be filled with 0
  server_addr.sin_addr.s_addr = ip_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = nport;

  int reuseaddr_opt = 1;  // recover server if a crash occurs
  int ret = setsockopt(sdesc, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_opt,
                       sizeof(reuseaddr_opt));
  ERROR_HELPER(ret, "Can't set SO_REUSEADDR flag");

  ret = connect(sdesc, (struct sockaddr*)&server_addr,
                sizeof(struct sockaddr_in));
  ERROR_HELPER(ret, "Cannot connect to remote server \n");
  printf("[Main] TCP connection established... \n");

  // setting up localWorld
  localWorld* local_world = (localWorld*)malloc(sizeof(localWorld));
  local_world->vehicles = (Vehicle**)malloc(sizeof(Vehicle*) * WORLD_SIZE);
  for (int i = 0; i < WORLD_SIZE; i++) {
    local_world->id_list[i] = -1;
    local_world->hv[i] = 0;
  }

  // Talk with server
  fprintf(stdout, "[Main] Starting ID,map_elevation,map_texture requests \n");
  myid = getID(sdesc);
  local_world->id_list[0] = myid;
  fprintf(stdout, "[Main] ID number %d received \n", myid);
  Image* surface_elevation = getElevationMap(sdesc);
  fprintf(stdout, "[Main] Map elevation received \n");
  Image* surface_texture = getTextureMap(sdesc);
  fprintf(stdout, "[Main] Map texture received \n");
  printf("[Main] Sending vehicle texture");
  sendVehicleTexture(sdesc, my_texture, myid);
  fprintf(stdout, "[Main] Client Vehicle texture sent \n");

  // create Vehicle
  World_init(&world, surface_elevation, surface_texture);
  vehicle = (Vehicle*)malloc(sizeof(Vehicle));
  Vehicle_init(vehicle, &world, myid, my_texture);
  World_addVehicle(&world, vehicle);
  local_world->vehicles[0] = vehicle;
  local_world->hv[0] = 1;

  // UDP Init
  uint16_t port_number_udp =
      htons((uint16_t)tmp);  // we use network byte order
  udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(sdesc, "Can't create an UDP socket");
  struct sockaddr_in udp_server = {0};
  udp_server.sin_addr.s_addr = ip_addr;
  udp_server.sin_family = AF_INET;
  udp_server.sin_port = port_number_udp;
  printf("[Main] Socket UDP created and ready to work \n");
  gettimeofday(&start_time, NULL);  // Accounting

  // Create UDP Threads
  pthread_t UDP_sender, UDP_receiver;
  udpArgs udp_args;
  udp_args.tcp_socket = sdesc;
  udp_args.saddr = udp_server;
  udp_args.udp_socket = udp_socket;
  udp_args.lw = local_world;
  ret = pthread_create(&UDP_sender, NULL, UDPSender, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[MAIN] pthread_create on thread UDP_sender");
  ret = pthread_create(&UDP_receiver, NULL, UDPReceiver, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[MAIN] pthread_create on thread UDP_receiver");
  WorldViewer_runGlobal(&world, vehicle, &argc, argv);

  // Waiting threads to end and cleaning resources
  printf("[Main] Disabling and joining on UDP and TCP threads \n");
  connectivity = 0;
  exchange_update = 0;
  ret = pthread_join(UDP_sender, NULL);
  PTHREAD_ERROR_HELPER(ret, "pthread_join on thread UDP_sender failed");
  ret = pthread_join(UDP_receiver, NULL);
  PTHREAD_ERROR_HELPER(ret, "pthread_join on thread UDP_receiver failed");
  ret = close(udp_socket);
  ERROR_HELPER(ret, "Failed to close UDP socket");

  fprintf(stdout, "[Main] Cleaning up... \n");
  sendGoodbye(sdesc, myid);

  // Clean resources
  pthread_mutex_destroy(&time_lock);
  for (int i = 0; i < WORLD_SIZE; i++) {
    if (local_world->id_list[i] == -1) continue;
    if (i == 0) continue;
    local_world->players_online--;
    if (!local_world->hv[i]) continue;
    Image* im = local_world->vehicles[i]->texture;
    World_detachVehicle(&world, local_world->vehicles[i]);
    if (im != NULL) Image_free(im);
    Vehicle_destroy(local_world->vehicles[i]);
    free(local_world->vehicles[i]);
  }

  free(local_world->vehicles);
  free(local_world);
  ret = close(sdesc);
  ERROR_HELPER(ret, "Failed to close TCP socket");
  World_destroy(&world);
  Image_free(surface_elevation);
  Image_free(surface_texture);
  Image_free(my_texture);
  exit(EXIT_SUCCESS);
}
