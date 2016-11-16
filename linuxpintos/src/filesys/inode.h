#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"
#include "threads/synch.h" //Added

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

//Added
void inode_acquire_read_lock(struct inode *inode);
void inode_release_read_lock(struct inode *inode);
void inode_acquire_write_lock(struct inode *inode);
void inode_release_write_lock(struct inode *inode);
void inode_acquire_open_close_lock(struct inode *inode);
void inode_release_open_close_lock(struct inode *inode);
void inode_acquire_dir_lock(struct inode *inode);
void inode_release_dir_lock(struct inode *inode);
int inode_reader_cnt(struct inode *inode);
void inode_add_reader(struct inode *inode);
void inode_remove_reader(struct inode *inode);
bool inode_is_removed(struct inode *inode);

#endif /* filesys/inode.h */
