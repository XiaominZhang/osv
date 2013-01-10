
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include "libc.hh"

// XXX: move to header
extern "C" { int ll_readdir(int fd, struct dirent *d); }

struct __dirstream {
	int fd;
};


DIR *opendir(const char *path)
{
	DIR *dir = new DIR;

	if (!dir) 
		return libc_error_ptr<DIR>(ENOMEM);


	dir->fd = open(path, O_RDONLY);
	if (dir->fd < 0) {
		delete dir;
		return NULL;
	}
	return dir;
}

int closedir(DIR *dir)
{
	close(dir->fd);
	delete dir;
	return 0;
}

struct dirent *readdir(DIR *dir)
{
	static struct dirent entry, *result;	// XXX: tls?
	int ret;

	ret = readdir_r(dir, &entry, &result);
	if (ret)
		return libc_error_ptr<struct dirent>(ret);

	errno = 0;
	return result;
}

int readdir_r(DIR *dir, struct dirent *entry, struct dirent **result)
{
	int ret;

	ret = ll_readdir(dir->fd, entry);
	if (ret == 0)
		*result = entry;
	else
		*result = NULL;
	return 0;
}