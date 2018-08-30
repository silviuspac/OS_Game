
// #include <GL/glut.h> // not needed here
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "image.h"
#include "surface.h"
#include "world.h"
#include "vehicle.h"
#include "world_viewer.h"



typedef struct handler_args_s {
  /*
   * Specify fields for the arguments that will be populated in the
   * main thread and then accessed in connection_handler(void* arg).
   **/
  int socket_desc;
  struct sockaddr_in *client_addr;
} handler_args_t;
void *connection_handler(void *arg) {
  handler_args_t *args = (handler_args_t *)arg;
  /* We make local copies of the fields from the handler's arguments
   * data structure only to share as much code as possible with the
   * other two versions of the server. In general this is not a good
   * coding practice: using simple indirection is better! */
  int socket_desc = args->socket_desc;
  struct sockaddr_in *client_addr = args->client_addr;
  int ret, recv_bytes;
  char buf[1024];
  size_t buf_len = sizeof(buf);
  size_t msg_len;
  char *quit_command = SERVER_COMMAND;
  size_t quit_command_len = strlen(quit_command);
  // parse client IP address and port
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
  uint16_t client_port =
      ntohs(client_addr->sin_port); // port number is an unsigned short
  // send welcome message
  sprintf(buf,
          "It's Work! You are %s talking on port %hu.\n "
          "I will stop if you send me %s :-)\n",
          client_ip, client_port, quit_command);
  msg_len = strlen(buf);
  while ((ret = send(socket_desc, buf, msg_len, 0)) < 0) {
    if (errno == EINTR)
      continue;
    ERROR_HELPER(-1, "Cannot write to the socket");
  }
  while(1) { }
  // close socket
  ret = close(socket_desc);
  ERROR_HELPER(ret, "Cannot close socket for incoming connection");
  /** SOLUTION
   *
   * Suggestions
   * - free memory allocated for this thread inside the main thread
   * - print a debug message to inform the user that the thread has
   *   completed its work
   */
  if (DEBUG)
    fprintf(stderr, "Thread created to handle the request has completed.\n");
  free(args->client_addr); // do not forget to free this buffer!
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
