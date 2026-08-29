/*
 * fnx/fs/devfs/nodes.c
 *
 * Copyright 2026, Kyle J Cardoza. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * The devfs node registry - the make_dev() analog. Drivers call
 * devfs_make_node() at probe time for each device node they own; the
 * registry is walked by devfs lookup/readdir to synthesize /dev. The
 * registry is independent of the mount, so nodes added after boot (e.g. a
 * hotplugged usb-storage disk) show up in /dev without any cache
 * invalidation: devfs inodes are synthesized on demand and never cached.
 */

#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/fs_devfs.h>
#include <fnx/mm.h>
#include <fnx/string.h>

struct devfs_node *devfs_nodes = NULL;

int devfs_make_node(const char *name, __dev_t dev, __mode_t mode)
{
	struct devfs_node **n, *new;
	int name_len;

	/* idempotent: a node with this name already exists */
	if(devfs_find_node(name)) {
		return 0;
	}
	name_len = strlen(name);
	if(name_len > 15) {
		return -ENAMETOOLONG;
	}

	if(!(new = (struct devfs_node *)kmalloc(sizeof(struct devfs_node)))) {
		return -ENOMEM;
	}
	memset_b(new, 0, sizeof(struct devfs_node));
	strncpy(new->name, name, 15);
	new->name[15] = '\0';
	new->dev = dev;
	new->mode = mode;

	/* keep the registry sorted by device number for a stable /dev listing */
	n = &devfs_nodes;
	while(*n && (*n)->dev < dev) {
		n = &(*n)->next;
	}
	new->next = *n;
	*n = new;
	return 0;
}

void devfs_remove_node(__dev_t dev)
{
	struct devfs_node **n, *tmp;

	n = &devfs_nodes;
	while(*n) {
		if((*n)->dev == dev) {
			tmp = *n;
			*n = (*n)->next;
			kfree((addr_t)tmp);
			return;
		}
		n = &(*n)->next;
	}
}

struct devfs_node *devfs_find_node(const char *name)
{
	struct devfs_node *n;

	for(n = devfs_nodes; n; n = n->next) {
		if(!strcmp(n->name, name)) {
			return n;
		}
	}
	return NULL;
}

struct devfs_node *devfs_find_node_dev(__dev_t dev)
{
	struct devfs_node *n;

	for(n = devfs_nodes; n; n = n->next) {
		if(n->dev == dev) {
			return n;
		}
	}
	return NULL;
}
