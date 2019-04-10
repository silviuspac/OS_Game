
// #include <GL/glut.h> // not needed here
#include <arpa/inet.h>
#include <math.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include "common.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"
#include "so_game_protocol.h"

typedef struct {
  int cdesc;
  struct sockaddr_in caddr;
  Image* elevation_texture;
  Image* surface_elevation;
  int tcp_socket;
} tcp_args_t;

World world;
UserHead* users;

int TCPCore(int tcp_socket, int id, char* buff, Image* surface_elevation, Image* surface_texture, int len, User* user){
  
  PacketHeader* header = (PacketHeader*)buff;
  if (header->type == GetId) {
    printf("[TCP] Richiesta ID da (%d)...\n", id);
    IdPacket* id_to_send = (IdPacket*)malloc(sizeof(IdPacket));

    PacketHeader header;
    header.type = GetId;
    id_to_send->header = header;
    id_to_send->id = id;  

    char sendb[BUFFERSIZE];
    int p = Packet_serialize(
        sendb,
        &(id_to_send->header));
    int sent = 0;
    int ret;
    while (sent < p) {
      ret = send(tcp_socket, sendb + sent, p - sent, 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Errore assegnazione ID");
      if (ret == 0) break;
      sent += ret;
    }

    printf("[TCP] ID spedito a (%d)...\n", id);

    return 1;
  } else if (header->type == GetTexture) {
    printf("[TCP] Richiesta texure da (%d)...\n", id);

    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la texture
    // richiesta
    ImagePacket* texture_request = (ImagePacket*)buff;
    int id_request = texture_request->id;

    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostTexture;

    // Preparo il pacchetto per inviare la texture al client
    ImagePacket* texture_to_send = (ImagePacket*)malloc(sizeof(ImagePacket));
    texture_to_send->header = header_send;
    texture_to_send->id = id_request;
    texture_to_send->image = elevation_texture;

    char sendb[BUFFER_SIZE];
    int p = Packet_serialize(sendb,
        &(texture_to_send->header));  // Ritorna il numero di bytes scritti

    // Invio del messaggio tramite socket
    int sent = 0;
    int ret;
    while (sent < p) {
      ret = send(tcp_socket, bsendb + sent, p - sent, 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Errore richiesta texture!!!");
      if (ret == 0) break;
      sent += ret;
    }

    printf("[TCP] Texture inviata a (%d)...\n", id);

    return 1;

  } else if (header->type == GetElevation) {
    printf("[TCP] Richiesta Elevation da (%d)...\n", id);

    // Converto il pacchetto ricevuto in un ImagePacket per estrarne la
    // elevation richiesta
    ImagePacket* elevation_request = (ImagePacket*)buff;
    int id_request = elevation_request->id;

    // Preparo header per la risposta
    PacketHeader header_send;
    header_send.type = PostElevation;

    // Preparo il pacchetto per inviare la elevation al client
    ImagePacket* elevation_to_send = (ImagePacket*)malloc(sizeof(ImagePacket));
    elevation_to_send->header = header_send;
    elevation_to_send->id = id_request;
    elevation_to_send->image = surface_elevation;

    char end[BUFFERSIZE];
    int p = Packet_serialize(sendb, &(elevation_to_send->header));

    // Invio
    int sent = 0;
    int ret;
    while (sent < p) {
      ret = send(tcp_socket, sendb + sent, p - sent, 0);
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Errore richiesta elevation texture!!!");
      if (ret == 0) break;
      sent += ret;
    }

    // Packet_free(&(elevation_to_send->header));   // Libera la memoria del
    // pacchetto non più utilizzato free(elevation_to_send);

    printf("[TCP] Elevation inviata a (%d)...\n", id);  // DEBUG OUTPUT

    return 1;
  } else if (header->type == PostTexture) {
    int ret;

    if (len < header->size) {
      // printf("[TCP] Received packet vehicle (size = %d)...\n", len);
      return -1;
    }

        // Deserializzazione del pacchetto
    PacketHeader* received_header = Packet_deserialize(buff, header->size);
    ImagePacket* received_texture = (ImagePacket*)received_header;

    printf("[TCP] Vehicle inviato da (%d)...\n", id);

    // Aggiunta veicolo nuovo al mondo
    Vehicle* newv = malloc(sizeof(Vehicle));
    Vehicle_init(newv, &world, id, received_texture->image);
    World_addVehicle(&world, newv);

    // Rimanda la texture al client come conferma
    PacketHeader header_aux;
    header_aux.type = PostTexture;

    ImagePacket* texture_c = (ImagePacket*)malloc(sizeof(ImagePacket));
    texture_c->image = received_texture->image;
    texture_c->header = header_aux;

    // Serializza la texture da mandare
    int buff_size = Packet_serialize(buff, &texture_c->header);

    // Invia la texture
    while ((ret = send(tcp_socket, buff, buff_size, 0)) < 0) {
      if (errno == EINTR) continue;
      ERROR_HELPER(ret, "Impossibili scrivere su socket!!!");
    }

    printf("[TCP] texture veicolo inviata indietro s (%d)...\n", id);

    // Packet_free(&received_texture->header); // Libera la memoria del
    // pacchetto non più utilizzato Packet_free(&texture_for_client->header);
    User_insert_last(users, user);

    printf("[TCP] User (%d) inserito...\n", user->id);

    return 1;
  } else {
    printf("pacchetto sconosciuto da %d!!!\n", id);  // DEBUG OUTPUT
  }

  return -1;  // Return in caso di errore
}

void* TCPClientH(void* args){

  tcp_args_t* tcp_args = (tcp_args_t*)args;

  printf("[TCPClientH] Client con descriptor (%d)\n", tcp_args->client_desc);
  int tcp_client_desc = tcp_args->client_desc;
  int msg_length = 0;
  int ret;
  char receive[BUFFERSIZE];  // Conterrà il PacketHeader
  printf("[TCPClientH] Creazione user con id: %d\n", tcp_client_desc);

  User* user = (User*)malloc(sizeof(User));
  user->id = tcp_client_desc;
  user->user_addr_tcp = tcp_args->client_addr;
  user->x = 0;
  user->y = 0;
  user->theta = 0;
  user->translational_force = 0;
  user->rotational_force = 0;
  user->vehicle = NULL;

  // Ricezione del pacchetto
  int p = BUFFERSIZE;
  while (1) {
    while ((ret = recv(tcp_client_desc, receive + msg_length, p - msg_length, 0)) < 0) {
      if (ret == -1 && errno == EINTR) continue;
      ERROR_HELPER(ret, "Errore ricezione pacchetto");
    }
    // Client disconnesso
    if (ret == 0) {
      printf("[TCPClientH] Connessione chiusa con: %d\n", user->id);
      if (User_detach(users, user->id) == 1)
        printf("[TCPClientH] User %d rimosso.\n", user->id);

      break;
    }
    msg_length += ret;

    // printf("[TCP] Received packet (total size = %d)...\n", ((PacketHeader*)
    // buffer_recv)->size);

    // Gestione del pacchetto ricevuto tramite l'handler dei pacchetti
    ret = TCPCore(tcp_client_desc, tcp_args->client_desc, receive,
                     tcp_args->surface_elevation, tcp_args->elevation_texture,
                     msg_length, user);

    if (ret == 1) {
      // printf("[TCP] Success...\n");

      msg_length = 0;
      continue;
    } else {
            // printf("[TCP] Next packet...\n");
      continue;
    }
  }
  pthread_exit(0);
}

void* TCP_handler(void* args)
{
  printf("[TCP Handler] Started...\n");
  tcp_args_t* tcp_args = (tcp_args_t*)args;
  int ret;
  int tcp_client_desc;
  int tcp_socket = tcp_args->tcp_socket;

  printf("[TCP Handler] Accettazione connessioni...\n");

  int saddr_len = sizeof(struct sockaddr_in);
  struct sockaddr_in caddr;

  while ((tcp_client_desc = accept(tcp_socket, (struct sockaddr*)&caddr, (socklen_t*)&saddr_len)) > 0) {
    printf("[TCP Handler] Connection stabilita con (%d)...\n", tcp_client_desc);
    pthread_t client_thread;
    tcp_args_t tcp_args_aux;
    tcp_args_aux.client_desc = tcp_client_desc;
    tcp_args_aux.elevation_texture = tcp_args->elevation_texture;
    tcp_args_aux.surface_elevation = tcp_args->surface_elevation;
    tcp_args_aux.client_addr = caddr;

    ret = pthread_create(&client_thread, NULL, TCPClientH, &tcp_args_aux);
    PTHREAD_ERROR_HELPER(ret, "Errore creazione TCP thread!!!");
  }

  ERROR_HELPER(tcp_client_desc, "Errore accetta connessione TCP!!!");
  pthread_exit(0);
}


void* UDPReceiverH(void* args)
{

  int ret;
  udp_args_t* udp_args = (udp_args_t*)args;
  int udp_socket = udp_args->udp_socket;
  char receive[BUFFERSIZE];

  struct sockaddr_in caddr = {0};
  socklen_t addrlen = sizeof(struct sockaddr_in);
  while (1) {

    if ((ret = recvfrom(udp_socket, receive, BUFFERSIZE, 0,
                        (struct sockaddr*)&caddr, &addrlen)) > 0) {
    }  
    ERROR_HELPER(ret, "[ERROR] Error ricezione pacchetto UDP!!!");

    // Raccoglie il pacchetto ricevuto
    PacketHeader* header = (PacketHeader*)receive;

    VehicleUpdatePacket* packet =
        (VehicleUpdatePacket*)Packet_deserialize(receive, header->size);
    User* user = User_find_id(users, packet->id);
    user->user_addr_udp = caddr;

    if (!user) {
      printf("Impossibile trocare user con ID: %d!!!\n", packet->id);
      pthread_exit(0);
    }

    // Aggiorna la posizione dell'utente
    Vehicle* vehicle_aux = World_getVehicle(&world, user->id);
    vehicle_aux->translational_force_update = packet->translational_force;
    vehicle_aux->rotational_force_update = packet->rotational_force;

    // Update del mondo
    World_update(&world);
  }

  printf("[UDP RECEIVER]Chiusura...\n");

  pthread_exit(0);
}

void* UDPSenderH(void* args)
{
  printf("[UDPSenderH] started...\n");
  char sendb[BUFFERSIZE];

  udp_args_t* udp_args = (udp_args_t*)args;
  int udp_socket = udp_args->udp_socket;

  printf("[UDP SENDER] Invio updates...\n");
  while (1) {
    int n_users = users->size;
    if (n_users > 0) {
      PacketHeader header;
      header.type = WorldUpdate;

      WorldUpdatePacket* world_update =
          (WorldUpdatePacket*)malloc(sizeof(WorldUpdatePacket));
      world_update->header = header;
      world_update->updates =
          (ClientUpdate*)malloc(sizeof(ClientUpdate) * n_users);
      world_update->num_vehicles = users->size;

      User* user = users->first;

      for (int i = 0; i < n_users; i++) {
        ClientUpdate* client = &(world_update->updates[i]);
        user->vehicle = World_getVehicle(&world, user->id);
        client->id = user->id;
        client->x = user->vehicle->x;
        client->y = user->vehicle->y;
        client->theta = user->vehicle->theta;
        user = user->next;
      }
      int size = Packet_serialize(sendb, &world_update->header);

      user = users->first;

      while (user != NULL) {
        if (user->user_addr_udp.sin_addr.s_addr != 0) {
          int ret = sendto(udp_socket, sendb, size, 0,
                           (struct sockaddr*)&user->user_addr_udp,
                           (socklen_t)sizeof(user->user_addr_udp));
          ERROR_HELPER(ret, "Errore invio updates UDP!!!");
        }

        user = user->next;
      }
    }
    usleep(TIME_TO_SLEEP);
  }

  pthread_exit(0);
}

void* UpdateLoop(void* args)
{
  printf("[Update Loop] Started\n");
  while(1){
    World_update(&world);
    usleep(80000);
  }
} 



int main(int argc, char **argv) {
  	if (argc < 3) {
    	printf("usage: %s <elevation_image> <texture_image>\n", argv[1]);
    	exit(-1);
  }

  char* elevation_filename=argv[1];
  char* texture_filename=argv[2];
  char* vehicle_texture_filename="./images/arrow-right.ppm";
  printf("loading elevation image from %s ... ", elevation_filename);

  // load the images
  Image* surface_elevation = Image_load(elevation_filename);
  if (surface_elevation) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }


  printf("loading texture image from %s ... ", texture_filename);
  Image* surface_texture = Image_load(texture_filename);
  if (surface_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }

  printf("loading vehicle texture (default) from %s ... ", vehicle_texture_filename);
  Image* vehicle_texture = Image_load(vehicle_texture_filename);
  if (vehicle_texture) {
    printf("Done! \n");
  } else {
    printf("Fail! \n");
  }


  int ret;
  int serverTCP, serverUDP;

  // TCP
  serverTCP = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(serverTCP, "[TCP] Impossibile creare TCP socket");
  if (serverTCP >= 0) printf("[TCP] Socket aperto (%d)...\n", serverTCP);

  struct sockaddr_in tcp_server_addr = {0};
  int sockaddr_len = sizeof(struct sockaddr_in);
  tcp_server_addr.sin_addr.s_addr = INADDR_ANY;
  tcp_server_addr.sin_family = AF_INET;
  tcp_server_addr.sin_port = htons(TCP_PORT);

  int reuse= 1;
  ret = setsockopt(serverTCP, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  ERROR_HELPER(ret, "[ERROR] Failed setsockopt on TCP server socket!!!");

  ret = bind(serverTCP, (struct sockaddr*)&tcp_server_addr, sockaddr_len);
  ERROR_HELPER(ret, "[ERROR] Failed bind address on TCP server socket!!!");

  ret = listen(serverTCP, 3);
  ERROR_HELPER(ret, "[ERROR] Failed listen on TCP server socket!!!");
  if (ret >= 0) printf("[MAIN] Server listening on port %d...\n", PORT);

  // UDP Init
  serverUDP = socket(AF_INET, SOCK_DGRAM, 0);
  ERROR_HELPER(serverUDP, "[ERROR] Failed to create UDP socket!!!");

  struct sockaddr_in udp_server_addr = {0};
  udp_server_addr.sin_addr.s_addr = INADDR_ANY;
  udp_server_addr.sin_family = AF_INET;
  udp_server_addr.sin_port = htons(UDP_PORT);

  int reuse_udp = 1;
  ret = setsockopt(serverUDP, SOL_SOCKET, SO_REUSEADDR, &reuse_udp,
                   sizeof(reuseaddr_udp));
  ERROR_HELPER(ret, "[ERROR] Failed setsockopt on UDP server socket!!!");

  ret = bind(serverUDP, (struct sockaddr*)&udp_server_addr,
             sizeof(udp_server_addr));
  ERROR_HELPER(ret, "[ERROR] Failed bind address on UDP server socket!!!");

  printf("[MAIN] Server UDP started...\n"); 

  users = (UserHead*)malloc(sizeof(UserHead));
  Users_init(users);
  World_init(&world, surface_elevation, surface_texture, 0.5, 0.5, 0.5);
  pthread_t TCP_connection, UDP_sender_thread, UDP_receiver_thread, world_update;

  // Args TCP
  tcp_args_t tcp_args;
  tcp_args.elevation_texture = surface_texture;
  tcp_args.surface_elevation = surface_elevation;
  tcp_args.tcp_socket = serverTCP;

  udp_args_t udp_args;
  udp_args.udp_socket = serverUDP;

  // Threads
  ret = pthread_create(&TCP_connection, NULL, TCP_handler, &tcp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create TCP connection thread");

  ret = pthread_create(&UDP_sender_thread, NULL, UDP_sender_handler, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDP sender thread");

  ret = pthread_create(&UDP_receiver_thread, NULL, UDP_receiver_handler, &udp_args);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create UDP receiver thread");

  ret = pthread_create(&world_update, NULL, UpdateLoop, NULL);
  PTHREAD_ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to create World Update thread");


  // Join dei thread
  ret = pthread_join(world_update, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join World Updatethread");

  ret = pthread_join(TCP_connection, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join TCP server connection thread");

  ret = pthread_join(UDP_sender_thread, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join UDP server sender thread");

  ret = pthread_join(UDP_receiver_thread, NULL);
  ERROR_HELPER(ret, "[ERROR] [MAIN] Failed to join UDP server receiver thread");

  // Cleanup
  Image_free(surface_texture);
  Image_free(surface_elevation);
  World_destroy(&world);
  return 0;
} 