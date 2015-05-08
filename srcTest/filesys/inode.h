#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"


#define INODE_DIRECT_BLOCKS 123
#define INDEX_DIRECT_BLOCKS 128
#define INDIRECT_INDEX_BLOCKS 128


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    bool is_dir;                        /* Does this inode correspond to a directory? */
    block_sector_t doubly_indirect_block;                        // 4 bytes
    block_sector_t index_block;                                  // 4 bytes
    block_sector_t direct_blocks[INODE_DIRECT_BLOCKS];           // 4 * 124 bytes
  };

struct index_block
{
  block_sector_t direct_blocks[INDEX_DIRECT_BLOCKS];
};

struct indirect_block
{
  block_sector_t index_sectors[INDIRECT_INDEX_BLOCKS];
};

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct index_block * index_block_pointer;
    struct indirect_block * indirect_block_pointer;
    struct index_block * indirect_index_blocks[INDIRECT_INDEX_BLOCKS];

    struct lock inode_lock;
  };

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
