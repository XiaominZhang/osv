/*-
 * Copyright (c) 2005-2009, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * device.c - device I/O support routines
 */

/**
 * The device_* system calls are interfaces to access the specific
 * device object which is handled by the related device driver.
 *
 * The routines in this moduile have the following role:
 *  - Manage the name space for device objects.
 *  - Forward user I/O requests to the drivers with minimum check.
 *  - Provide the table for the Driver-Kernel Interface.
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "../vfs/prex.h"
#include "device.h"

#define sched_lock()	do {} while (0)
#define sched_unlock()	do {} while (0)


/* list head of the devices */
static struct device *device_list = NULL;

/*
 * Look up a device object by device name.
 */
static struct device *
device_lookup(const char *name)
{
	struct device *dev;

	for (dev = device_list; dev != NULL; dev = dev->next) {
		if (!strncmp(dev->name, name, MAXDEVNAME))
			return dev;
	}
	return NULL;
}


/*
 * device_create - create new device object.
 *
 * A device object is created by the device driver to provide
 * I/O services to applications.  Returns device ID on
 * success, or 0 on failure.
 */
struct device *
device_create(struct driver *drv, const char *name, int flags)
{
	struct device *dev;
	size_t len;
	void *private = NULL;

	assert(drv != NULL);

	/* Check the length of name. */
	len = strnlen(name, MAXDEVNAME);
	if (len == 0 || len >= MAXDEVNAME)
		return NULL;

	sched_lock();

	/* Check if specified name is already used. */
	if (device_lookup(name) != NULL)
		sys_panic("duplicate device");

	/*
	 * Allocate a device structure and device private data.
	 */
	if ((dev = malloc(sizeof(*dev))) == NULL)
		sys_panic("device_create");

	if (drv->devsz != 0) {
		if ((private = malloc(drv->devsz)) == NULL)
			sys_panic("devsz");
		memset(private, 0, drv->devsz);
	}
	strlcpy(dev->name, name, len + 1);
	dev->driver = drv;
	dev->flags = flags;
	dev->active = 1;
	dev->refcnt = 1;
	dev->private = private;
	dev->next = device_list;
	device_list = dev;

	sched_unlock();
	return dev;
}

#if 0
int
device_destroy(struct device *dev)
{

	sched_lock();
	if (!device_valid(dev)) {
		sched_unlock();
		return ENODEV;
	}
	dev->active = 0;
	device_release(dev);
	sched_unlock();
	return 0;
}
#endif

#if 0
/*
 * Return device's private data.
 */
static void *
device_private(struct device *dev)
{
	assert(dev != NULL);
	assert(dev->private != NULL);

	return dev->private;
}
#endif

/*
 * Return true if specified device is valid.
 */
static int
device_valid(struct device *dev)
{
	struct device *tmp;
	int found = 0;

	for (tmp = device_list; tmp != NULL; tmp = tmp->next) {
		if (tmp == dev) {
			found = 1;
			break;
		}
	}
	if (found && dev->active)
		return 1;
	return 0;
}

/*
 * Increment the reference count on an active device.
 */
static int
device_reference(struct device *dev)
{

	sched_lock();
	if (!device_valid(dev)) {
		sched_unlock();
		return ENODEV;
	}
	dev->refcnt++;
	sched_unlock();
	return 0;
}

/*
 * Decrement the reference count on a device. If the reference
 * count becomes zero, we can release the resource for the device.
 */
static void
device_release(struct device *dev)
{
	struct device **tmp;

	sched_lock();
	if (--dev->refcnt > 0) {
		sched_unlock();
		return;
	}
	/*
	 * No more references - we can remove the device.
	 */
	for (tmp = &device_list; *tmp; tmp = &(*tmp)->next) {
		if (*tmp == dev) {
			*tmp = dev->next;
			break;
		}
	}
	free(dev);
	sched_unlock();
}

/*
 * device_open - open the specified device.
 *
 * Even if the target driver does not have an open
 * routine, this function does not return an error. By
 * using this mechanism, an application can check whether
 * the specific device exists or not. The open mode
 * should be handled by an each device driver if it is
 * needed.
 */
int
device_open(const char *name, int mode, struct device **devp)
{
	struct devops *ops;
	struct device *dev;
	int error;

	sched_lock();
	if ((dev = device_lookup(name)) == NULL) {
		sched_unlock();
		return ENXIO;
	}
	error = device_reference(dev);
	if (error) {
		sched_unlock();
		return error;
	}
	sched_unlock();

	ops = dev->driver->devops;
	assert(ops->open != NULL);
	error = (*ops->open)(dev, mode);
	*devp = dev;

	device_release(dev);
	return error;
}

/*
 * device_close - close a device.
 *
 * Even if the target driver does not have close routine,
 * this function does not return any errors.
 */
int
device_close(struct device *dev)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->close != NULL);
	error = (*ops->close)(dev);

	device_release(dev);
	return error;
}

/*
 * device_read - read from a device.
 *
 * Actual read count is set in "nbyte" as return.
 * Note: The size of one block is device dependent.
 */
int
device_read(struct device *dev, void *buf, size_t *count, int blkno)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->read != NULL);
	error = (*ops->read)(dev, buf, count, blkno);

	device_release(dev);
	return error;
}

/*
 * device_write - write to a device.
 *
 * Actual write count is set in "nbyte" as return.
 */
int
device_write(struct device *dev, const void *buf, size_t *count, int blkno)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->write != NULL);
	error = (*ops->write)(dev, buf, count, blkno);

	device_release(dev);
	return error;
}

/*
 * device_ioctl - I/O control request.
 *
 * A command and its argument are completely device dependent.
 * The ioctl routine of each driver must validate the user buffer
 * pointed by the arg value.
 */
int
device_ioctl(struct device *dev, u_long cmd, void *arg)
{
	struct devops *ops;
	int error;

	if ((error = device_reference(dev)) != 0)
		return error;

	ops = dev->driver->devops;
	assert(ops->ioctl != NULL);
	error = (*ops->ioctl)(dev, cmd, arg);

	device_release(dev);
	return error;
}

#if 0
/*
 * Device control - devctl is similar to ioctl, but is invoked from
 * other device driver rather than from user application.
 */
static int
device_control(struct device *dev, u_long cmd, void *arg)
{
	struct devops *ops;
	int error;

	assert(dev != NULL);

	sched_lock();
	ops = dev->driver->devops;
	assert(ops->devctl != NULL);
	error = (*ops->devctl)(dev, cmd, arg);
	sched_unlock();
	return error;
}

/*
 * device_broadcast - broadcast devctl command to all device objects.
 *
 * If "force" argument is true, we will continue command
 * notification even if some driver returns an error. In this
 * case, this routine returns EIO error if at least one driver
 * returns an error.
 *
 * If force argument is false, a kernel stops the command processing
 * when at least one driver returns an error. In this case,
 * device_broadcast will return the error code which is returned
 * by the driver.
 */
static int
device_broadcast(u_long cmd, void *arg, int force)
{
	struct device *dev;
	struct devops *ops;
	int error, retval = 0;

	sched_lock();

	for (dev = device_list; dev != NULL; dev = dev->next) {
		/*
		 * Call driver's devctl() routine.
		 */
		ops = dev->driver->devops;
		if (ops == NULL)
			continue;

		assert(ops->devctl != NULL);
		error = (*ops->devctl)(dev, cmd, arg);
		if (error) {
			if (force)
				retval = EIO;
			else {
				retval = error;
				break;
			}
		}
	}
	sched_unlock();
	return retval;
}
#endif

/*
 * Return device information.
 */
int
device_info(struct devinfo *info)
{
	u_long target = info->cookie;
	u_long i = 0;
	struct device *dev;
	int error = ESRCH;

	sched_lock();
	for (dev = device_list; dev != NULL; dev = dev->next) {
		if (i++ == target) {
			info->cookie = i;
			info->id = dev;
			info->flags = dev->flags;
			strlcpy(info->name, dev->name, MAXDEVNAME);
			error = 0;
			break;
		}
	}
	sched_unlock();
	return error;
}

int
enodev(void)
{
	return ENODEV;
}

int
nullop(void)
{
	return 0;
}