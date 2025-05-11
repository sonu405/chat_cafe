// bug: I am not instantly getting the "New Connection Printed!";

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdbool.h>
#include <sys/select.h>

// int select(int nfds, fd_set *restrict readfds,
//     fd_set *restrict writefds, fd_set *restrict errorfds,
//     struct timeval *restrict timeout);

#define SERVER_PORT "9340"

struct message {
  int user_id; // user's fd
  char username[16];
  char message[300];
};

enum client_state { WAITING_FOR_USERNAME, WAITING_FOR_ID, VERIFIED };

struct user {
  int fd;
  int room_id;
  char username[16];
  enum client_state state;
};

struct room {
  int id;
  int num_of_msg;
  struct message messages[1000];
};

// temporarily for logic -- initializing it
struct room rooms[5] = {{
    -1,
    0,
}};
int room_count;

struct user users[50];
int user_count = 0;

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

  int activity;

  // handling the logic of validation
  for (;;) {
    read_fds = master;

    // setting up timeval for printing the usr info every 10 secs
    struct timeval tv = {5, 0};
    if ((activity = select(fd_max + 1, &read_fds, NULL, NULL, &tv)) == -1) {
      perror("select error: ");
      exit(4);
    }

    // this is when timeval comes
    if (activity == 0) {
      printf("User Count: %d\n", user_count);
      for (int i = 0; i < user_count; i++) {
        printf("user: %s, fd: %d, room_id: %d\n", users[i].username,
               users[i].fd, users[i].room_id);
      }

      for (int i = 0; i <= rooms[0].num_of_msg; i++) {
        printf("%s\n", rooms[0].messages[i].message);
      }
      continue;
    }

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

          // for every connection, we make it a user
          if (user_count < 50) { // 50 is max users
            users[user_count++] =
                (struct user){new_fd, -1, "", WAITING_FOR_USERNAME};
          }
          send(new_fd, "Enter username: ", sizeof("Enter username: "), 0);
          // we can't recv right after send as we're unsure if the client is
          // ready to send data.

          printf("New connection...\n");
        } else {
          // not a listener, must be a client,

          // flowing block of code puts the client ready in the curr client
          // struct
          int k;
          for (k = 0; k < user_count; k++) {
            if (users[k].fd == i) {
              break; // due to this, k is the index of the curr client in user
                     // array
            }
          }

          // Deal with data, recieve or send
          // -1 leaves space for null termination
          if ((nbytes = recv(i, buf, sizeof(buf) - 1, 0)) <= 0) {
            if (nbytes == 0) {
              // connection closed
              printf("selectserver: socket %d hung up\n", i);
            } else {
              perror("recv error: ");
            }

            close(i);
            FD_CLR(i, &master);
          } else {
            // validate it if he isn't verified already.
            // removes new line if present in the buf message (that is
            // username or id) from recv
            buf[strcspn(buf, "\r\n")] = '\0';
            switch (users[k].state) {
            case WAITING_FOR_USERNAME:
              strcpy(users[k].username, buf);
              users[k].state = WAITING_FOR_ID;

              // prompt for id
              char prompt[] = "Enter room id: ";
              send(i, prompt, sizeof(prompt), 0);
              break;
            case WAITING_FOR_ID: {
              unsigned long int room_id;
              char *badchar;

              room_id = strtol(buf, &badchar, 10);
              if (*badchar != '\0') {
                char prompt[] = "incorrect id, Please re-enter: ";
                send(i, prompt, sizeof(prompt), 0);
                continue;
              }
              users[k].room_id = room_id;
              users[k].state = VERIFIED;
              break;
            }
            case VERIFIED: {
              // we have already got some data that we must send to everyone
              for (j = 0; j <= fd_max; j++) {
                if (FD_ISSET(j, &master)) {
                  if (j != listener && j != i) {

                    // send the buffered input that we took from the current
                    // client (target of this loop's recv) and send to all other
                    // clients.
                    struct message msg;
                    strcpy(msg.username, users[k].username);
                    msg.user_id = users[k].fd;
                    int msg_len;

                    // buf[nbytes] = '\0'; // null terminate buf returned from
                    // recv
                    snprintf(msg.message, sizeof(msg.message), "%s: %s\n",
                             users[k].username, buf);

                    msg_len = strlen(msg.message);
                    rooms[0].messages[rooms[0].num_of_msg++] = msg;

                    if (send(j, msg.message, msg_len, 0) == -1) {
                      perror("send: ");
                    }
                  }
                }
              }
              break;
            }
            }
          }
        }
      }
    }
  }
}
