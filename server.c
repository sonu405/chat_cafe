#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdbool.h>
#include <sys/select.h>

#define SERVER_PORT "9340"

struct message {
  int user_id; // user's fd
  char username[16];
  char message[300];
};

enum client_state {
  WAITING_FOR_USERNAME,
  WAITING_FOR_ID,
  WAITING_FOR_ROOM,
  VERIFIED
};

struct user {
  int fd;
  int room_id;
  char username[16];
  int last_send; // index of the last message send to this user from the room's
                 // array of messages
  enum client_state state;
};

struct room {
  int id;
  int num_of_msg;
  struct message messages[1000];
};

// temporarily for logic -- initializing it
struct room rooms[10] = {{
    -1,
    0,
}};
int room_count = 1; // FIX THIS TO 0 and REMOVE THAT TEMPORARY initializing

struct user users[50];
int user_count = 0;

int main() {

  fd_set master, read_fds, write_fds;
  int fd_max;
  FD_ZERO(&master);
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);

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
    write_fds = master;

    // setting up timeval for printing the usr info every 10 secs
    struct timeval tv = {5, 0};
    if ((activity = select(fd_max + 1, &read_fds, &write_fds, NULL, &tv)) ==
        -1) {
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

      for (int i = 0; i < room_count; i++) {
        for (int j = 0; j < rooms[i].num_of_msg; j++)
          printf("%s\n", rooms[i].messages[j].message);
      }
      continue;
    }

    for (i = 0; i <= fd_max; i++) {
      if (FD_ISSET(i, &read_fds)) { // we got one to read!!
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
                (struct user){new_fd, -1, "", 0, WAITING_FOR_USERNAME};
          }
          send(new_fd, "Enter username: ", sizeof("Enter username: "), 0);
          // we can't recv right after send as we're unsure if the client is
          // ready to send data.

          printf("New connection...\n");
        } else {
          // not a listener, must be a client,

          // following block of code puts the client ready in the curr client
          // struct
          int k;
          for (k = 0; k < user_count; k++) {
            if (users[k].fd == i) {
              break; // due to this, k is the index of the curr client in user
                     // array
            }
          }

          // Deal with data, receive or send
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
            case WAITING_FOR_USERNAME: {
              strcpy(users[k].username, buf);
              users[k].state = WAITING_FOR_ID;

              // prompt for id
              char prompt[] = "Enter room id: ";
              send(i, prompt, strlen(prompt), 0);
              break;
            }
            case WAITING_FOR_ID: {
              unsigned long int room_id;
              char *badchar;

              room_id = strtol(buf, &badchar, 10);
              if (*badchar != '\0') {
                char prompt[] = "incorrect id, Please re-enter: ";
                send(i, prompt, strlen(prompt), 0);
                continue;
              }

              bool room_exists = false;
              // check if the id exists
              for (int i = 0; i < room_count; i++) {
                if (rooms[i].id == room_id) {
                  room_exists = true;
                  break;
                }
              }

              if (room_exists) {
                users[k].room_id = room_id;
                users[k].state = VERIFIED;
              } else {
                users[k].state = WAITING_FOR_ROOM;

                char prompt[] = "Room doesn't exists, would you like to make a "
                                "new room? (y or n): ";
                send(i, prompt, strlen(prompt), 0);
              }

              break;
            }
            case WAITING_FOR_ROOM: {
              if (strcmp(buf, "y") == 0 || strcmp(buf, "ye") == 0 ||
                  strcmp(buf, "yes") == 0) {
                // make a room

                // note that, i am using room count as the id for the room
                // later, i'll segregraate this logic. Make sure not to use
                // room count directly anywhere and always compare with id's
                rooms[room_count] = (struct room){room_count, 0};
                users[k].room_id = room_count;

                // send that new room's id to the user
                char id_prompt[256];
                sprintf(id_prompt, "[SERVER]: The id of the new room is: %d\n",
                        room_count);
                send(i, id_prompt, strlen(id_prompt), 0);

                room_count++;

                users[k].state = VERIFIED;
              } else {
                // prompt for id
                char prompt[] = "Enter room id: ";
                send(i, prompt, strlen(prompt), 0);
                users[k].state = WAITING_FOR_ID;
              }

              break;
            }
            case VERIFIED: {
              // note that this is only when app working in cmd and we want to
              // remove the entered message.

              // ANSI escape: move cursor up and clear the line
              char *clear_line = "\033[A\33[2K\r";
              send(i, clear_line, strlen(clear_line), 0);

              // we have already got some data that we must now deal with
              struct message msg;
              strcpy(msg.username, users[k].username);
              msg.user_id = users[k].fd;
              int msg_len;

              // TODO: REMOVE THIS \n at THE END AS IT CREATES PROBLEMS IN
              // STORING MSG
              snprintf(msg.message, sizeof(msg.message), "%s: %s",
                       users[k].username, buf);

              msg_len = strlen(msg.message);

              int room_index; // find the index of the room in which user
                              // exists in rooms
              for (int i = 0; i < room_count; i++) {
                if (users[k].room_id == rooms[i].id) {
                  room_index = i;
                  break;
                }
              }
              rooms[room_index].messages[rooms[room_index].num_of_msg++] = msg;

              break;
            }
            }
          }
        }
      } // if (fdisset -> readfds)
      else if (FD_ISSET(i, &write_fds)) {
        // means some client
        if (i != listener) {

          int k, room_index, num_of_msg; // num_of_msg in the room of user

          // this find index of the user.
          for (k = 0; k < user_count; k++) {
            if (users[k].fd == i) {
              break;
            }
          }

          for (int i = 0; i < room_count; i++) {
            if (users[k].room_id == rooms[i].id) {
              num_of_msg = rooms[i].num_of_msg;
              room_index = i;
              break;
            }
          }

          if (users[k].last_send < num_of_msg) {
            char msgs_buf[10000];
            int l;

            msgs_buf[0] = '\0';
            for (l = users[k].last_send; l < num_of_msg; l++) {
              strcat(msgs_buf, rooms[room_index].messages[l].message);
              strcat(msgs_buf, "\n");
            }
            users[k].last_send = l;

            send(i, msgs_buf, strlen(msgs_buf), 0);
          }
        }
      }
    }
  }
}
