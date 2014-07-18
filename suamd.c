#define _BSD_SOURCE

#include <error.h>
#include <errno.h>
#include <libudev.h>
#include <linux/limits.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MOUNT_PREFIX "/media/"
#define NTFS3G_FS_TYPE "ntfs-3g"
#define EXTRA_CHARACTER "_"

int path_exist(const char *path)
{
	struct stat s;
	return (stat(path, &s) == 0);
}

int is_directory(const char *path)
{
	struct stat s;
	return (stat(path, &s) == 0) ? S_ISDIR(s.st_mode) : 0;
}

int is_mount_point(const char *path, char **dev_node)
{
	FILE *mtab;
	struct mntent *partition;
	int ret = 0;

	if(dev_node)
		*dev_node = NULL;

	if((mtab = setmntent("/etc/mtab", "r")))
	{
		while((partition = getmntent(mtab)) && !ret)
		{
			if(strcmp(partition->mnt_dir, path) == 0)
			{
				ret = 1;
				if(dev_node)
					*dev_node = strdup(partition->mnt_fsname);
			}
		}

		endmntent(mtab);
	}

	return ret;
}

char* generate_mount_point(struct udev_device *device)
{
	const char *id_fs_label;
	char mount_point[PATH_MAX] = MOUNT_PREFIX;
	size_t len;

	id_fs_label = udev_device_get_property_value(device, "ID_FS_LABEL");
	if(!id_fs_label)
		id_fs_label = udev_device_get_property_value(device, "ID_MODEL");
	if(!id_fs_label)
		id_fs_label = EXTRA_CHARACTER;

	strncat(mount_point, id_fs_label, PATH_MAX - strlen(mount_point) - 1);
	len = strlen(mount_point);

	while(path_exist(mount_point) &&
			(is_mount_point(mount_point, NULL) || !is_directory(mount_point))
			&& len < PATH_MAX)
	{
		strncat(mount_point, EXTRA_CHARACTER, sizeof(EXTRA_CHARACTER));
		len = strlen(mount_point);
	}

	return (len < PATH_MAX) ? strdup(mount_point) : NULL;
}

int is_mounted(const char *dev_node, char **mount_point)
{
	FILE *mtab;
	struct mntent *partition;
	int ret = 0;

	if(mount_point)
		*mount_point = NULL;

	if((mtab = setmntent("/etc/mtab", "r")))
	{
		while((partition = getmntent(mtab)) && !ret)
		{
			if(strcmp(partition->mnt_fsname, dev_node) == 0)
			{
				ret = 1;
				if(mount_point)
					*mount_point = strdup(partition->mnt_dir);
			}
		}

		endmntent(mtab);
	}

	return ret;
}

void mount_device(struct udev_device *device, const char *mount_point)
{
	const char *devnode = udev_device_get_devnode(device);
	const char *fs_type = udev_device_get_property_value(device, "ID_FS_TYPE");
	int status = 0;
	pid_t p;


#ifdef USE_NTFS3G
	if(strcmp(fs_type, "ntfs") == 0)
		fs_type = NTFS3G_FS_TYPE;
#endif

	if(mkdir(mount_point, S_IRWXU | S_IRGRP | S_IXGRP) == -1 && errno != EEXIST)
	{
		error(0, errno, "Failed to create mount point %s", mount_point);
	}
	else
	{
		p = fork();

		if(p == -1)
		{
			perror("fork syscall error");
			fprintf(stderr, "unplug and plug you device again to retry\n");
			return;
		}
		else if(p == 0)
		{
			setuid(0);
			execl("/sbin/mount", "/sbin/mount", "-t", fs_type, devnode,
					mount_point, NULL);
			perror("/sbin/mount");
			exit(1);
		}

		wait(&status);

		if(status != 0)
			fprintf(stderr, "Failed to mount %s on %s", devnode, mount_point);
		else
			printf("Device %s successfuly mounted on %s\n", devnode, mount_point);
	}
}

void unmount_device(struct udev_device *device)
{
	const char *dev_node = udev_device_get_devnode(device);
	char *mount_point = NULL;

	if(is_mounted(dev_node, &mount_point))
	{
		if(mount_point)
		{
			if(umount(mount_point) == -1)
			{
				error(0, errno,
						"Failed to unmount device %s (mount point: %s)\n",
						dev_node, mount_point);
			}
			else
			{
				printf("Device %s successfuly unmounted (mount point: %s)\n",
						dev_node, mount_point);

				if(rmdir(mount_point) == -1)
					error(0, errno, "Failed to delete %s\n", mount_point);
			}

			free(mount_point);
		}
	}
}

void create_prefix()
{
	if(path_exist(MOUNT_PREFIX))
		return;

	if(mkdir(MOUNT_PREFIX, S_IRWXU |
				S_IRGRP | S_IXGRP |
				S_IROTH | S_IXOTH) == -1 && errno != EEXIST)
	{
		error(0, errno, "Failed to create prefix directory for mounting (%s)\n",
				MOUNT_PREFIX);
		exit(EXIT_FAILURE);
	}
}

int main(void)
{
	struct udev *udev;
	struct udev_monitor *umon;
	struct udev_device *udevice;
	char *mount_point;
	const char *dev_node;

	if(geteuid() != 0)
	{
		fprintf(stderr, "This program needs root privileges (make sure it is "
				"installed with the setuid bit set)\n");
		return 1;
	}

	umask(0);

	udev = udev_new();

	if(!udev)
	{
		fprintf(stderr, "Failed to create udev\n");
		return 2;
	}

	umon = udev_monitor_new_from_netlink(udev, "udev");

	if(!umon)
	{
		fprintf(stderr, "Failed to create udev_monitor\n");
		return 2;
	}

	udev_monitor_filter_add_match_subsystem_devtype(umon, "block", "partition");
	udev_monitor_enable_receiving(umon);

	while(1)
	{
		udevice = udev_monitor_receive_device(umon);

		if(udevice)
		{
			dev_node = udev_device_get_devnode(udevice);

			if(strcmp("add", udev_device_get_action(udevice)) == 0)
			{
				printf("[ADD] device %s added\n", dev_node);

				mount_point = generate_mount_point(udevice);

				if(mount_point)
				{
					mount_device(udevice, mount_point);
					free(mount_point);
				}
				else
				{
					fprintf(stderr, "Failed to generate mount point for %s\n",
							udev_device_get_devnode(udevice));
				}
			}
			else if(strcmp("remove", udev_device_get_action(udevice)) == 0)
			{
				printf("[REMOVE] device %s removed\n", dev_node);
				unmount_device(udevice);
			}
		}

		sleep(1);
	}

	udev_monitor_unref(umon);
	udev_unref(udev);

	return 0;
}
