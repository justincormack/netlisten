#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern char **environ;

int main(int argc, char ** argv) {
	int s, in, out, conn;
	const struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(8000), .sin_addr = INADDR_ANY};
	int inpipe[2], outpipe[2];
	pid_t pid;
	char buf[4096];
	ssize_t n;
	struct pollfd p[2];
	int status;

	if (argc == 1) {
		fprintf(stderr, "Usage: %s program ...\n", argv[0]);
		return 1;
	}

	/* set stdin, stdout, stderr to CLOEXEC as we use these */
	if (fcntl(0, F_SETFD, FD_CLOEXEC) == -1) {
		perror("fcntl CLOEXEC");
		return 1;
	}
	if (fcntl(1, F_SETFD, FD_CLOEXEC) == -1) {
		perror("fcntl CLOEXEC");
		return 1;
	}
	if (fcntl(2, F_SETFD, FD_CLOEXEC) == -1) {
		perror("fcntl CLOEXEC");
		return 1;
	}

	/* create pipes for stdin, stdout of new process */
	if (pipe(inpipe) == -1) {
		perror("pipe");
		return 1;
	}
	if (pipe(outpipe) == -1) {
		perror("pipe");
		return 1;
	}
	/* set CLOEXEC on write end of stdin, read end of stdout */
	if (fcntl(inpipe[1], F_SETFD, FD_CLOEXEC) == -1) {
		perror("fcntl CLOEXEC");
		return 1;
	}
	if (fcntl(outpipe[0], F_SETFD, FD_CLOEXEC) == -1) {
		perror("fcntl CLOEXEC");
		return 1;
	}

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1) {
		perror("socket");
		return 1;
	}
	if (bind(s, (const struct sockaddr *)&sa, (socklen_t)sizeof(sa)) == -1) {
		perror("bind");
		return 1;
	}
	/* set CLOEXEC on socket */
	if (fcntl(s, F_SETFD, FD_CLOEXEC) == -1) {
		perror("fcntl CLOEXEC");
		return 1;
	}
	/* listen with a backlog of only 1 */
	if (listen(s, 1) == -1) {
		perror("listen");
		return 1;
	}

	pid = fork();
	if (pid == -1) {
		perror("fork");
		return 1;
	}

	/* child */
	if (pid == 0) {
		dup2(inpipe[0], 0);
		dup2(outpipe[1], 1);
		dup2(outpipe[1], 2);
		close(inpipe[0]);
		close(outpipe[1]);
		if (execve(argv[1], &argv[1], environ) == -1) {
			perror("execve");
		}
		return 1;
	}
	/* close our copy of the other end of the pipes */
	close(inpipe[0]);
	close(outpipe[1]);

	/* TODO poll on connect or stdin */
	p[0].fd = 0;
	p[1].fd = s;
	p[0].events = POLLIN | POLLHUP;
	p[1].events = POLLIN;

	if (poll(p, 2, -1) == -1) {
		perror("poll");
		return 1;
	}
	if (p[0].revents) {
		in = 0;
		out = 1;
	} else {
		conn = accept(s, NULL, 0);
		in = conn;
		out = conn;
	}
	/* copy input to stdin */
	while (1) {
		n = read(in, buf, sizeof(buf));
		if (n == -1) {
			if (errno == EINTR)
				continue;
			perror("read");
			return 1;
		}
		if (n == 0) {
			break;
		}
		while (n > 0) {
			off_t off = 0;
			ssize_t ret = write(inpipe[1], buf + off, n);
			if (ret == -1) {
				if (errno == EINTR)
					continue;
				perror("write");
				return 1;
			}
			n -= ret;
			off += ret;
		}
	}
	/* input finished, close */
	close(inpipe[1]);
	/* copy stdout to output */
	while (1) {
		n = read(outpipe[0], buf, sizeof(buf));
		if (n == -1) {
			if (errno == EINTR)
				continue;
			perror("read");
			return 1;
		}
		if (n == 0) {
			break;
		}
		while (n > 0) {
			off_t off = 0;
			ssize_t ret = write(out, buf + off, n);
			if (ret == -1) {
				if (errno == EINTR)
					continue;
				perror("write");
				return 1;
			}
			n -= ret;
			off += ret;
		}
	}
	/* output finished, close */
	close(outpipe[0]);
	close(in);
	close(out);
	close(s);

	/* wait for child */
	while (1) {
		if (wait4(pid, &status, 0, NULL) == -1) {
			if (errno == EINTR)
				continue;
			perror("wait4");
			return 1;
		}
		/* return child's exit code */
		if (WIFEXITED(status))
			return WEXITSTATUS(status);
		return 1;
	}

	return 0;
}
