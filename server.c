
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/select.h>

// int select(int nfds, fd_set *restrict readfds,
//     fd_set *restrict writefds, fd_set *restrict errorfds,
//     struct timeval *restrict timeout);

#define SERVER_PORT "9340"

struct message {
  int user_id;
  char *username;
  char *message;
};

struct user {
  int room_id;
};

struct room {
  int id;
  int num_of_msg;
  struct message messages[];
};

struct room rooms[5];

int main() {

  fd_set master, read_fds;
  int fd_max;
  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  struct addrinfo hints, *ai, *p;
  int rv, yes = 1, listener;

  memset(&hints, 0, sizeof hints);
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_UNSPEC;

  if ((rv = getaddrinfo(NULL, SERVER_PORT, &hints, &ai)) != 0) {
    fprintf(stderr, "server: %s\n", gai_strerror(rv));
    exit(1);
  }

  for (p = ai; p != NULL; p = p->ai_next) {
    if ((listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
      continue;
    }

    // lose the pesky "address already in use" error message
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
      // cant bind to that address, check again
      close(listener);
      continue;
    }

    break;
  }

  if (p == NULL) {
    // all checked, can't find even one address that listens.
    exit(2);
  }

  // after binding, we are done with this
  freeaddrinfo(ai);

  if (listen(listener, 10) == -1) {
    perror("listen ");
    exit(3);
  }

  FD_SET(listener, &master);
  fd_max = listener;
  int i, j;

  struct sockaddr_storage remote_addr;
  socklen_t addr_len;
  int new_fd;
  char buf[256];
  int nbytes;

  // setting up timeval for printing the usr info every 10 secs
  struct timeval tv;
  tv.tv_sec = 10;
  tv.tv_usec = 0;
  int activity;

  for (;;) {
    read_fds = master;

    if ((activity = select(fd_max + 1, &read_fds, NULL, NULL, &tv)) == -1) {
      perror("select error: ");
      exit(4);
    }

    // if (activity == 0) {
    // this is when timeval comes
    // for (int i = 0; i < room.num_of_msg; i++) {
    //   struct message msg = room.messages[i];
    //   printf("%s: %s", msg.username, msg.message);
    // }
    // }

    for (i = 0; i <= fd_max; i++) {
      if (FD_ISSET(i, &read_fds)) { // we got one!!
        if (i == listener) {
          // listener is ready means a new connection
          addr_len = sizeof remote_addr;
          if ((new_fd = accept(listener, (struct sockaddr *)&remote_addr,
                               &addr_len)) == -1) {
            perror("accept: ");
          }

          FD_SET(new_fd, &master);

          if (new_fd > fd_max) {
            fd_max = new_fd;
          }
          printf("New connection...");
        } else {
          // not a listener, must be a client, hungry for data
          if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
            if (nbytes == 0) {
              // connection closed
              printf("selectserver: socket %d hung up\n", i);
            } else {
              perror("recv error: ");
            }

            close(i);
            FD_CLR(i, &master);
          } else {
            // we have already got some data that we must send to everyone
            for (j = 0; j <= fd_max; j++) {
              if (FD_ISSET(j, &master)) {
                if (j != listener && j != i) {
                  if (send(j, buf, nbytes, 0) == -1) {
                    perror("send: ");
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
