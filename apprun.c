#define _GNU_SOURCE
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

int mount_bind(const char* argv0, const char* inbase, const char* outbase, const char* rel)
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
		fprintf(stderr, "cannot stat %s: %s\n", in_buf, strerror(errno));
		return 1;
	}

	// TODO mount files/symlinks?
	if (statbuf.st_mode & S_IFDIR) {
		if (mkdir(out_buf, statbuf.st_mode & ~S_IFMT) < 0 && errno != EEXIST) {
			fprintf(stderr, "cannot mkdir %s: %d %s\n", out_buf, errno, strerror(errno));
			return 1;
		}
	} else {
		return 0; // TODO
	}

	if (mount(in_buf, out_buf, "none", MS_BIND | MS_REC, NULL) < 0) {
		fprintf(stderr, "%s: mount %s -> %s: %s\n", argv0, in_buf, out_buf, strerror(errno));
		return 1;
	}

	return 0;
}

int main(int argc, char** argv)
{
	if (unshare(CLONE_NEWUSER | CLONE_NEWNS) < 0) {
		fprintf(stderr, "%s: unshare: %s\n", argv[0], strerror(errno));
		return 1;
	}

	// make new mountpoint
	char mountroot[] = "/tmp/appimage-root-XXXXXX";
	if (!mkdtemp(mountroot)) {
		fprintf(stderr, "%s: could not make mountroot %s: %s\n", argv[0], mountroot, strerror(errno));
	}

	// copy root filesystem
	DIR* rootfs = opendir("/");
	struct dirent* rootentry;
	while ((rootentry = readdir(rootfs))) {
		if (strcmp(rootentry->d_name, ".") == 0 || strcmp(rootentry->d_name, "..") == 0) {
			continue;
		}

		if (mount_bind(argv[0], "/", mountroot, rootentry->d_name)) {
			return 1;
		}
	}
	closedir(rootfs);

	// mount in nix
	char appdir_buf[PATH_MAX];
	char* appdir = dirname(realpath("/proc/self/exe", appdir_buf));
	if (!appdir) {
		fprintf(stderr, "%s: could not access /proc/self/exe: %s\n", argv[0], strerror(errno));
		return 1;
	}

	if (mount_bind(argv[0], appdir, mountroot, "nix")) {
		return 1;
	}

	// chroot
	if (chroot(mountroot) < 0) {
		fprintf(stderr, "%s: chroot %s: %s\n", argv[0], mountroot, strerror(errno));
		return 1;
	}

	char exec_buf[PATH_MAX];
	if (snprintf(exec_buf, PATH_MAX, "%s/entrypoint", appdir) >= PATH_MAX) {
		fprintf(stderr, "%s: path too long: %s/entrypoint", argv[0], appdir);
		return 1;
	}

	execv(exec_buf, argv + 1);
	fprintf(stderr, "%s: could not exec %s: %s\n", argv[0], exec_buf, strerror(errno));
	return 1;
}
