#include <arpa/inet.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include <fev/fev.h>

static struct sockaddr_in server_addr;

static const struct timespec timeout = {
    .tv_sec = 3,
    .tv_nsec = 0,
};

static void *echo(void *arg)
{
  char buffer[1024];
  struct fev_socket *socket = arg;

  for (;;) {
    ssize_t num_read = fev_socket_try_read_for(socket, buffer, sizeof(buffer), &timeout);
    if (num_read <= 0) {
      if (num_read < 0) {
        int err = (int)(-num_read);
        fprintf(stderr, "Reading from socket failed: %s\n", strerror(err));
      }
      break;
    }

    ssize_t num_written = fev_socket_write(socket, buffer, (size_t)num_read);
    if (num_written != num_read) {
      if (num_written < 0) {
        int err = (int)(-num_written);
        fprintf(stderr, "Writing to socket failed: %s\n", strerror(err));
      }
      break;
    }
  }

  fev_socket_close(socket);
  fev_socket_destroy(socket);

  return NULL;
}

static void *acceptor(void *arg)
{
  struct fev_socket *socket;
  int ret;

  (void)arg;

  ret = fev_socket_create(&socket);
  if (ret != 0) {
    fprintf(stderr, "Creating socket failed: %s\n", strerror(-ret));
    goto out;
  }

  ret = fev_socket_open(socket, AF_INET, SOCK_STREAM, 0);
  if (ret != 0) {
    fprintf(stderr, "Opening socket failed: %s\n", strerror(-ret));
    goto out_destroy;
  }

  ret = fev_socket_set_opt(socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
  if (ret != 0) {
    fprintf(stderr, "Setting SO_REUSEADDR failed: %s\n", strerror(-ret));
    goto out_close;
  }

  ret = fev_socket_bind(socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret != 0) {
    fprintf(stderr, "Binding socket failed: %s\n", strerror(-ret));
    goto out_close;
  }

  ret = fev_socket_listen(socket, /*backlog=*/1024);
  if (ret != 0) {
    fprintf(stderr, "Listening on socket failed: %s\n", strerror(-ret));
    goto out_close;
  }

  for (;;) {
    struct fev_socket *new_socket;

    ret = fev_socket_create(&new_socket);
    if (ret != 0) {
      fprintf(stderr, "Creating new socket failed: %s\n", strerror(-ret));
      goto out_close;
    }

    ret = fev_socket_accept(socket, new_socket, /*address=*/NULL, /*address_len=*/NULL);
    if (ret != 0) {
      fprintf(stderr, "Accepting socket failed: %s\n", strerror(-ret));
      fev_socket_destroy(new_socket);
      goto out_close;
    }

    /* We can pass NULL as sched, the fiber will be spawned in the current scheduler. */
    ret = fev_fiber_spawn(/*sched=*/NULL, &echo, new_socket);
    if (ret != 0) {
      fprintf(stderr, "Spawning echo fiber failed: %s\n", strerror(-ret));
      fev_socket_destroy(new_socket);
      goto out_close;
    }
  }

out_close:
  fev_socket_close(socket);

out_destroy:
  fev_socket_destroy(socket);

out:
  return NULL;
}

int main(int argc, char **argv)
{
  struct fev_sched *sched;
  const char *host;
  uint16_t port;
  int err, ret = 1;

  /* Parse arguments. */

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <HOST-IPV4> <PORT>\n", argv[0]);
    return 1;
  }

  host = argv[1];

  if (sscanf(argv[2], "%" SCNu16, &port) != 1) {
    fputs("Parsing port failed\n", stderr);
    return 1;
  }

  /* Initialize server address. */

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    fprintf(stderr, "Converting host IPv4 '%s' failed\n", host);
    return 1;
  }

  /* Run. */

  err = fev_sched_create(&sched, /*attr=*/NULL);
  if (err != 0) {
    fprintf(stderr, "Creating scheduler failed: %s\n", strerror(-err));
    return 1;
  }

  /* Spawn a fiber in `sched`. */
  err = fev_fiber_spawn(sched, &acceptor, NULL);
  if (err != 0) {
    fprintf(stderr, "Spawning fiber failed: %s\n", strerror(-err));
    goto out_sched;
  }

  err = fev_sched_run(sched);
  if (err != 0) {
    fprintf(stderr, "Running scheduler failed: %s\n", strerror(-err));
    goto out_sched;
  }

  ret = 0;

out_sched:
  fev_sched_destroy(sched);

  return ret;
}
