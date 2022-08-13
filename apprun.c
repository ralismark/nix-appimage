#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Exit status to use when launching an AppImage fails.
 * For applications that assign meanings to exit status codes (e.g. rsync),
 * we avoid "cluttering" pre-defined exit status codes by using 127 which
 * is known to alias an application exit status and also known as launcher
 * error, see SYSTEM(3POSIX).
 */
#define EXIT_EXECERROR 127

static const char* argv0;
static char mountroot[] = "/tmp/appimage-root-XXXXXX";

static void die_if(bool cond, const char* fmt, ...)
{
	if (cond) {
		fprintf(stderr, "%s: ", argv0);
		va_list args;
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		fprintf(stderr, ": %s\n", strerror(errno));
		exit(EXIT_EXECERROR);
	}
}

const char* strprintf(const char* fmt, ...)
{
	va_list args1;
	va_start(args1, fmt);
	va_list args2;
	va_copy(args2, args1);

	int len = vsnprintf(NULL, 0, fmt, args1);
	if (len < 0) {
		fprintf(stderr, "%s: vsnprintf '%s' failed\n", argv0, fmt);
		exit(EXIT_EXECERROR);
	}

	char* buf = malloc(len + 1);
	if (!buf) {
		fprintf(stderr, "%s: malloc %d\n", argv0, len + 1);
		exit(EXIT_EXECERROR);
	}

	va_end(args1);

	if (vsnprintf(buf, len + 1, fmt, args2) != len) {
		fprintf(stderr, "%s: vsnprintf '%s' returned unexpected length\n", argv0, fmt);
		exit(EXIT_EXECERROR);
	}

	va_end(args2);

	return buf;
}

static int write_to(const char* path, const char* fmt, ...)
{
	int fd = open(path, O_WRONLY);
	if (fd > 0) {
		va_list args;
		va_start(args, fmt);
		if (vdprintf(fd, fmt, args) < 0) {
			va_end(args);
			close(fd);
			return 1;
		}
		va_end(args);
		close(fd);
		return 0;
	}
	return 1;
}

void child_main(char** argv, int w)
{
	close(w);

	// get location of exe
	char appdir_buf[PATH_MAX];
	char* appdir = dirname(realpath("/proc/self/exe", appdir_buf));
	die_if(!appdir, "cannot access /proc/self/exe");

	// get uid, gid before going to new namespace
	uid_t uid = getuid();
	gid_t gid = getgid();

	// create new user ns so we can mount() in userland
	die_if(unshare(CLONE_NEWUSER | CLONE_NEWNS) < 0, "cannot unshare");

	// UID/GID Mapping -----------------------------------------------------------

	// see user_namespaces(7)
	// > The data written to uid_map (gid_map) must consist of a single line that
	// > maps the writing process's effective user ID (group ID) in the parent
	// > user namespace to a user ID (group ID) in the user namespace.
	die_if(write_to("/proc/self/uid_map", "%d %d 1\n", uid, uid), "cannot write uid_map");

	// see user_namespaces(7):
	// > In the case of gid_map, use of the setgroups(2) system call must first
	// > be denied by writing "deny" to the /proc/[pid]/setgroups file (see
	// > below) before writing to gid_map.
	die_if(write_to("/proc/self/setgroups", "deny"), "cannot write setgroups");
	die_if(write_to("/proc/self/gid_map", "%d %d 1\n", uid, gid), "cannot write gid_map");

	// Mountpoint ----------------------------------------------------------------

	// tmpfs so we don't need to cleanup
	die_if(mount("tmpfs", mountroot, "tmpfs", 0, 0) < 0, "mount tmpfs -> %s", mountroot);

	// copy over root directories
	DIR* rootdir = opendir("/");
	struct dirent* rootentry;
	while ((rootentry = readdir(rootdir))) {
		// ignore . and .. and nix
		if (strcmp(rootentry->d_name, ".") == 0
			|| strcmp(rootentry->d_name, "..") == 0
			|| strcmp(rootentry->d_name, "nix") == 0) {
			continue;
		}

		const char* from = strprintf("/%s", rootentry->d_name);
		const char* to = strprintf("%s/%s", mountroot, rootentry->d_name);

		die_if(mkdir(to, 0777) < 0, "mkdir %s", to);
		die_if(mount(from, to, "none", MS_BIND | MS_REC, 0) < 0, "mount %s -> %s", from, to);

		free((void*) from);
		free((void*) to);
	}

	// mount in /nix
	const char* nix_from = strprintf("%s/nix", appdir);
	const char* nix_to = strprintf("%s/nix", mountroot);

	die_if(mkdir(nix_to, 0777) < 0, "mkdir %s", nix_to);
	die_if(mount(nix_from, nix_to, "none", MS_BIND | MS_REC, 0) < 0, "mount %s -> %s", nix_from, nix_to);

	free((void*) nix_from);
	free((void*) nix_to);

	// Chroot --------------------------------------------------------------------

	// save where we were so we can cd into it
	char cwd[PATH_MAX];
	die_if(!getcwd(cwd, PATH_MAX), "cannot getcwd");

	// chroot
	die_if(chroot(mountroot) < 0, "cannot chroot %s", mountroot);

	// cd back again
	die_if(chdir(cwd) < 0, "cannot chdir %s", cwd);;

	// Exec ----------------------------------------------------------------------

	const char* exe = strprintf("%s/entrypoint", appdir);
	execv(exe, argv);
	die_if(true, "cannot exec %s", exe);
}

int main(int argc, char** argv)
{
	argv0 = argv[0];

	// make new mountpoint
	die_if(!mkdtemp(mountroot), "mkdtemp %s", mountroot);

	int pipefd[2];
	die_if(pipe(pipefd) < 0, "cannot make pipe");

	int w = pipefd[1];
	int r = pipefd[0];

	int pid = fork();
	die_if(pid < 0, "cannot fork");
	if (pid == 0) {
		// child
		child_main(argv, w);
	} else {
		char c;
		die_if(read(r, &c, 1) < 0, "parent read");

		// parent
		int status = 0;
		int rv = waitpid(pid, &status, 0);
		status = rv > 0 && WIFEXITED (status) ? WEXITSTATUS (status) : EXIT_EXECERROR;

		rmdir(mountroot);

		return status;
	}
}
