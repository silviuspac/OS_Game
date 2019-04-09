
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

#include "common.h"
#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"

typedef struct handler_args{
  //argomenti per l main thread e poi usati nella con_handler
  int sdesc;
  struct sockaddr_in *client_addr;
} handler_args;

void *con_handler(void *args){
  int s = args->sdesc;
  struct sockaddr_in *cl_addr = args->client_addr;

  int ret;
  int recv_bytes;

  char buf[1024];
  size_t buf_len = sizeof(buf);
  size_t msg_len;

  char *quit = QUIT;
  size_t quit_len = strlen(quit_command);

  //accetto i client
  char c_ip[INET_ADDRSLEN];
  inet_ntop(AF_INET, &(cl_addr->sin_addr), c_ip, INET_ADDRSLEN);
  uint16_t c_port = nthos(cl_addr->sin_port); 

  //messaggio benvenuto
  sprintf(buf, "Sei %s, porta numero %hu.\n"
               "Per fermare manda %s.\n",
               c_ip, c_port, quit);
  msg_len = strlen(buf);
  while ((ret = send(s, buf, msg_len, 0)) < 0){
    if(errno == EINTR) continue;
    ERROR_HELPER (-1, "Errore scrittura su socket");
  }

  //chiusura socket
  ret = close(s);
  ERROR_HELPER(ret, "Errore chiudura socket per conn entranti");

  free(args->cl_addr);
  free(args);
  pthread_exit(NULL);


}



int main(int argc, char **argv) {
  	if (argc < 4) {
    	printf("usage: %s <elevation_image> <texture_image> <port>\n", argv[1]);
    	exit(-1);
  }
  char* elevation_filename=argv[1];
  char* texture_filename=argv[2];
  int server_port = strtol(argv[3], NULL, 0);
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

  //creazione thread
  int ret;
  int sdesc,cdesc;
  struct sockaddr_in server_addr = {0};
  int sockaddr_len = sizeof(struct sockaddr_in);

  //inizializzazione socket ascolto
  sdesc = socket(AF_INET, SOCK_STREAM, 0);
  ERROR_HELPER(sdesc, "Errore creazione socket");
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);

  //SO_REUSEADDR per i crash
  int reuse = 1;
  ret = setsockopt(sdesc, SOL_SOCKET, SO_REUSEADDR, &resuse, sizeof(reuse));
  ERROR_HELPER (ret, "Errore SO_REUSEADDR");

  ret = bind(sdesc, (struct sockaddr *)&server_addr, sockaddr_len);
  ERROR_HELPER(ret, "Errore bind");

  ret = listen(sodesc, MAX_CONNESSIONI);
  ERROR_HELPER(ret, "Errore ascolto");

  struct sockaddr_in *c_addr = calloc(1, sizeof(struct sockaddr_in));

  while(1){
    if(DEBUG)
      fprintf(stderr, "Attendere conferma..\n");

    cdesc = accept(sdesc, (struct sockaddr *)c_addr, (socklen_t *) &sockaddr_len);
    if(cdesc == 1 && errno == EINTR)
      continue;
    ERROR_HELPER(cdesc, "Errore socket per connessioni");

    if(DEBUG)
      fprintf(stderr, "Connessione accettata..\n");

    pthread thread;

    handler_args *thread_args = malloc(sizeof(handler_args));
    thread_args->sdesc;
    thread_args->client_addr = c_addr;

    ret = pthread_create(&thread, NULL, con_handler, thread_args);
    PTHREAD_ERROR_HELPER(ret, "Errore creazione thread");

    if(DEBUG)
      fprintf(stderr, "Thread creato per nuova connessione.\n");

    ret = pthread_detach(thread);
    PTHREAD_ERROR_HELPER(ret, "Errore thread");

    c_addr = calloc(1, sizeof(struct sockaddr_in));

  }







  // not needed here
  //   // construct the world
  // World_init(&world, surface_elevation, surface_texture,  0.5, 0.5, 0.5);

  // // create a vehicle
  // vehicle=(Vehicle*) malloc(sizeof(Vehicle));
  // Vehicle_init(vehicle, &world, 0, vehicle_texture);

  // // add it to the world
  // World_addVehicle(&world, vehicle);


  
  // // initialize GL
  // glutInit(&argc, argv);
  // glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
  // glutCreateWindow("main");

  // // set the callbacks
  // glutDisplayFunc(display);
  // glutIdleFunc(idle);
  // glutSpecialFunc(specialInput);
  // glutKeyboardFunc(keyPressed);
  // glutReshapeFunc(reshape);
  
  // WorldViewer_init(&viewer, &world, vehicle);

  
  // // run the main GL loop
  // glutMainLoop();

  // // check out the images not needed anymore
  // Image_free(vehicle_texture);
  // Image_free(surface_texture);
  // Image_free(surface_elevation);

  // // cleanup
  // World_destroy(&world);
  return 0;             
}
