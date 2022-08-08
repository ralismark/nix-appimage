#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <sched.h>
#include <stdarg.h>
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

int mount_bind(const char* inbase, const char* outbase, const char* rel)
{
	char in_buf[PATH_MAX];
	if (snprintf(in_buf, PATH_MAX, "%s/%s", inbase, rel) >= PATH_MAX) {
		fprintf(stderr, "%s: path too long: %s/%s", argv0, inbase, rel);
		return 1;
	}

	char out_buf[PATH_MAX];
	if (snprintf(out_buf, PATH_MAX, "%s/%s", outbase, rel) >= PATH_MAX) {
		fprintf(stderr, "%s: path too long: %s/%s", argv0, outbase, rel);
		return 1;
	}

	// check the thing we're mounting exists
	struct stat statbuf;
	if (stat(in_buf, &statbuf) < 0) {
		fprintf(stderr, "%s: cannot stat %s: %s\n", argv0, in_buf, strerror(errno));
		return 1;
	}

	// TODO mount files/symlinks?
	if (statbuf.st_mode & S_IFDIR) {
		if (mkdir(out_buf, statbuf.st_mode & ~S_IFMT) < 0 && errno != EEXIST) {
			fprintf(stderr, "%s: cannot mkdir %s: %s\n", argv0, out_buf, strerror(errno));
			return 1;
		}
	} else {
		return 0; // TODO
	}

	if (mount(in_buf, out_buf, "none", MS_BIND | MS_REC, NULL) < 0) {
		fprintf(stderr, "%s: cannot mount %s -> %s: %s\n", argv0, in_buf, out_buf, strerror(errno));
		return 1;
	}

	return 0;
}

static int write_to(const char* path, const char* fmt, ...)
{
	int fd = open(path, O_WRONLY);
	if (fd > 0) {
		va_list args;
		va_start(args, fmt);
		vdprintf(fd, fmt, args);
		va_end(args);
		close(fd);
		return 0;
	}
	return 1;
}

static int update_uid_gid(uid_t uid, gid_t gid)
{
	// fixes issue #1 where writing to /proc/self/gid_map fails
	// see user_namespaces(7) for more documentation
	write_to("deny", "/proc/self/setgroups");

	// map the original uid/gid in the new ns
	if (write_to("/proc/self/uid_map", "%d %d 1", uid, uid)) {
		fprintf(stderr, "%s: /proc/self/uid_map: %s'\n", argv0, strerror(errno));
		return 1;
	}
	if (write_to("/proc/self/gid_map", "%d %d 1", gid, gid)) {
		fprintf(stderr, "%s: /proc/self/gid_map: %s'\n", argv0, strerror(errno));
		return 1;
	}

	return 0;
}

int main(int argc, char** argv)
{
	argv0 = argv[0];

	// get uid, gid before going to new namespace
	uid_t uid = getuid();
	gid_t gid = getgid();

	// create new user ns so we can mount() in userland
	if (unshare(CLONE_NEWUSER | CLONE_NEWNS) < 0) {
		fprintf(stderr, "%s: cannot unshare: %s\n", argv0, strerror(errno));
		return EXIT_EXECERROR;
	}

	// make new mountpoint
	char mountroot[] = "/tmp/appimage-root-XXXXXX";
	if (!mkdtemp(mountroot)) {
		fprintf(stderr, "%s: cannot make mountroot %s: %s\n", argv0, mountroot, strerror(errno));
		return EXIT_EXECERROR;
	}

	// Mounts --------------------------------------------------------------------

	// copy root filesystem
	DIR* rootfs = opendir("/");
	struct dirent* rootentry;
	while ((rootentry = readdir(rootfs))) {
		if (strcmp(rootentry->d_name, ".") == 0 || strcmp(rootentry->d_name, "..") == 0) {
			continue;
		}

		if (mount_bind("/", mountroot, rootentry->d_name)) {
			return EXIT_EXECERROR;
		}
	}
	closedir(rootfs);

	// mount in nix
	char appdir_buf[PATH_MAX];
	char* appdir = dirname(realpath("/proc/self/exe", appdir_buf));
	if (!appdir) {
		fprintf(stderr, "%s: cannot access /proc/self/exe: %s\n", argv0, strerror(errno));
		return EXIT_EXECERROR;
	}

	if (mount_bind(appdir, mountroot, "nix")) {
		return EXIT_EXECERROR;
	}

	// Chroot --------------------------------------------------------------------

	// map uid/gid
	update_uid_gid(uid, gid);

	// save where we were so we can cd into it
	char cwd_buf[PATH_MAX];
	if (!getcwd(cwd_buf, PATH_MAX)) {
		fprintf(stderr, "%s: cannot getcwd: %s\n", argv0, strerror(errno));
		return EXIT_EXECERROR;
	}

	int pid = fork();
	if (pid < 0) {
		fprintf(stderr, "%s: cannot fork: %s\n", argv0, strerror(errno));
		return EXIT_EXECERROR;
	} else if (pid == 0) {
		// child

		// chroot
		if (chroot(mountroot) < 0) {
			fprintf(stderr, "%s: cannot chroot %s: %s\n", argv0, mountroot, strerror(errno));
			return EXIT_EXECERROR;
		}

		if (chdir(cwd_buf) < 0) {
			fprintf(stderr, "%s: cannot chdir %s: %s\n", argv0, cwd_buf, strerror(errno));
			return EXIT_EXECERROR;
		}

		char exec_buf[PATH_MAX];
		if (snprintf(exec_buf, PATH_MAX, "%s/entrypoint", appdir) >= PATH_MAX) {
			fprintf(stderr, "%s: path too long: %s/entrypoint\n", argv0, appdir);
			return EXIT_EXECERROR;
		}

		execv(exec_buf, argv);
		fprintf(stderr, "%s: could not exec %s: %s\n", argv0, exec_buf, strerror(errno));
		return EXIT_EXECERROR;
	} else {
		// parent
		int status = 0;
		int rv = waitpid(pid, &status, 0);
		status = rv > 0 && WIFEXITED (status) ? WEXITSTATUS (status) : EXIT_EXECERROR;

		// TODO cleanup

		return status;
	}

}
