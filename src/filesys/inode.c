#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define INODE_DIRECT_BLOCKS 122
#define INDEX_DIRECT_BLOCKS 127
#define INDIRECT_INDEX_BLOCKS 63

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  off_t size;                       /* File size in bytes. */  // 4 bytes
  struct indirect_block * indir_block;                         // 4 bytes
  block_sector_t doubly_indirect_block;                        // 4 bytes
  struct index_block * indx_block;                             // 4 bytes
  block_sector_t index_block;                                  // 4 bytes
  block_sector_t direct_blocks[INODE_DIRECT_BLOCKS];           // 4 * 122 bytes
  unsigned magic;                     /* Magic number. */      // 4 bytes
};

struct index_block
{
  bool is_full;
  block_sector_t direct_blocks[INDEX_DIRECT_BLOCKS];
};

struct indirect_block
{
  bool is_full;
  int * dummy;
  block_sector_t index_sectors[INDIRECT_INDEX_BLOCKS];
  struct index_block * index_blocks[INDIRECT_INDEX_BLOCKS];
};

//our helper functions
bool direct_block_allocation (struct inode_disk *, char *, size_t numsectors);
bool index_block_allocation(struct index_block *, char *, size_t numsectors, block_sector_t *);
bool indirect_block_allocation(struct indirect_block *, char *zeros, size_t numsectors, block_sector_t *);


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  off_t sector_index = pos / BLOCK_SECTOR_SIZE;


  if (sector_index < INODE_DIRECT_BLOCKS)
  {
    //printf("a");
    // printf("sectorIndex: %d\n", sector_index);
    //printf("sector# = %d\n", inode->data.direct_blocks[sector_index]);
    return inode->data.direct_blocks[sector_index];
  }

  else if (sector_index < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS){
    //printf("messed up");
    if (inode->data.indx_block == NULL)
    { 
      inode->data.indx_block =  calloc(1, sizeof * inode->data.indx_block);
      block_read(fs_device, inode->data.index_block, inode->data.indx_block);
    }
    return inode->data.indx_block->direct_blocks[sector_index-INODE_DIRECT_BLOCKS];
  }

  else if (sector_index < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS + INDIRECT_INDEX_BLOCKS * INDEX_DIRECT_BLOCKS)
  {
     //printf("messed up");
    unsigned index_block_index = (sector_index - (INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS))/INDEX_DIRECT_BLOCKS;
    unsigned direct_block_index = (sector_index - (INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS))%INDEX_DIRECT_BLOCKS;
    if (inode->data.indir_block == NULL) 
    {
      inode->data.indir_block = calloc(1, sizeof * inode->data.indir_block);
      block_read(fs_device, inode->data.doubly_indirect_block, inode->data.indir_block);

      /*if(inode->data.indir_block->index_blocks[index_block_index] == NULL) {
        inode->data.indir_block->index_blocks[index_block_index] =  calloc(1, sizeof (struct index_block));
        block_read(fs_device, inode->data.indir_block->index_sectors[index_block_index], inode->data.indir_block->index_blocks[index_block_index]);
      }*/
    }
    /* I moved this outside because I think there is a scenario where the indir block might already be calloced but 
    This specific index_block_index has not been calloced yet. If it is within the larger for statement as shown by
    the comment above, it may not create a new index_block when needed.*/
    if(inode->data.indir_block->index_blocks[index_block_index] == NULL) 
    {
        inode->data.indir_block->index_blocks[index_block_index] =  calloc(1, sizeof (struct index_block));
        block_read(fs_device, inode->data.indir_block->index_sectors[index_block_index], inode->data.indir_block->index_blocks[index_block_index]);
    }
    
    return inode->data.indir_block->index_blocks[index_block_index]->direct_blocks[direct_block_index];
  }
  //position too far
  else{
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = true;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  printf("Creating inode of length %d at sector %d\n", length, sector);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->index_block = -1;
    disk_inode->doubly_indirect_block = -1;  

    //printf("length %d\n", length );
    size_t sectors = bytes_to_sectors (length);
    disk_inode->size = length;
    disk_inode->magic = INODE_MAGIC;
    char zeros[BLOCK_SECTOR_SIZE];

    bool direct_success;
    bool index_success;
    bool indir_success;

    int x;
    for(x = 0; x < INODE_DIRECT_BLOCKS; x++)
    {
      disk_inode->direct_blocks[x] = -1;
    }

    if (sectors < INODE_DIRECT_BLOCKS)
    {
      disk_inode->indx_block = NULL;
      disk_inode->indir_block = NULL;

      success = direct_block_allocation( disk_inode, zeros, sectors);
      //printf("direct success: %d\n", success);
    }

    else if(sectors < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS)
    {
      //printf("messed up");
      disk_inode->indx_block = calloc (1, sizeof * disk_inode->indx_block);
      disk_inode->indir_block = NULL;

      direct_success = direct_block_allocation(disk_inode, zeros, INODE_DIRECT_BLOCKS);
      index_success = index_block_allocation(disk_inode->indx_block, zeros, sectors - INODE_DIRECT_BLOCKS , &disk_inode->index_block);
      success = direct_success && index_success;

      free(disk_inode->indx_block);
    }

    else if(sectors < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS + INDIRECT_INDEX_BLOCKS * INDEX_DIRECT_BLOCKS)
    {
      disk_inode->indx_block = calloc (1, sizeof * disk_inode->indx_block);
      disk_inode->indir_block = calloc (1, sizeof * disk_inode->indir_block);

      //printf("messed up");
      direct_success = direct_block_allocation( disk_inode, zeros, INODE_DIRECT_BLOCKS);
      index_success = index_block_allocation(disk_inode->indx_block, zeros, INDEX_DIRECT_BLOCKS, &disk_inode->index_block);
      indir_success = indirect_block_allocation(disk_inode->indir_block, zeros, sectors-(INDEX_DIRECT_BLOCKS+INODE_DIRECT_BLOCKS), disk_inode->doubly_indirect_block);
      success = direct_success && index_success && indir_success;

      free(disk_inode->indir_block);
      free(disk_inode->indx_block);
    }
    else
    {
      success = false;
    }

    disk_inode->indx_block = NULL;
    disk_inode->indir_block = NULL;

    block_write (fs_device, sector, disk_inode);
  ASSERT(disk_inode->magic == INODE_MAGIC);
    free (disk_inode);
  }

  //printf("Success: %d\n", success);
  return success;
}

bool
direct_block_allocation (struct inode_disk *disk_inode, char *zeros, size_t numsectors){
   int i;
   bool success = true;
   //printf("numsectors: %d\n", numsectors);
   for (i = 0; i < numsectors; ++i)
    {
      //printf("should not print in test\n");
      if(free_map_allocate (1, &disk_inode->direct_blocks[i])){
        block_write (fs_device, disk_inode->direct_blocks[i], zeros);
      }
      else{
        success = false;
      }
    }
    return success;
}

bool
index_block_allocation(struct index_block *indx_block, char *zeros, size_t numsectors, block_sector_t *index_block_sector){
  int i;
  bool success = true;
  indx_block->is_full = false;

  for(i = 0; i < INDEX_DIRECT_BLOCKS; i++)
  {
    indx_block->direct_blocks[i] = -1;
  }

  for (i = 0; i < numsectors; ++i)
  {
    if(free_map_allocate (1, &indx_block->direct_blocks[i])){
      block_write (fs_device, indx_block->direct_blocks[i], zeros);
    }
    else{
      success = false;
    }
  }

  if (free_map_allocate(1, index_block_sector))
  {
    block_write (fs_device, *index_block_sector, indx_block);
  }
  else{
    success = false;
  }
  return success;
}

bool
indirect_block_allocation(struct indirect_block *indir_block, char *zeros, size_t numsectors, block_sector_t *indir_block_sector){
  int i;
  bool success = true;
  indir_block->is_full = false;
  for(i = 0; i < INDIRECT_INDEX_BLOCKS; i++)
  {
    indir_block->index_sectors[i] = -1;
  }


  for (i = 0; i < numsectors/INDEX_DIRECT_BLOCKS; ++i)
  {
    indir_block->index_blocks[i] = calloc(1, sizeof * indir_block->index_blocks[i]);
    index_block_allocation(indir_block->index_blocks[i], zeros, INDEX_DIRECT_BLOCKS, indir_block->index_sectors[i]);

    if(free_map_allocate (1, &indir_block->index_sectors[i])){
      block_write (fs_device, indir_block->index_sectors[i], zeros);
    }
    else{
      success = false;
    }
    free(indir_block->index_blocks[i]);
  }

  if(numsectors%INDEX_DIRECT_BLOCKS != 0){
    index_block_allocation(indir_block->index_blocks[i], zeros, numsectors%INDEX_DIRECT_BLOCKS, indir_block->index_sectors[i]);

    if(free_map_allocate (1, &indir_block->index_sectors[i])){
      block_write (fs_device, indir_block->index_sectors[i], zeros);
    }
    else{
      success = false;
    }
  }

  if (free_map_allocate(1, indir_block_sector))
  {
    block_write (fs_device, *indir_block_sector, indir_block);
  }
  else{
    success = false;
  }

  for(i = 0; i < INDIRECT_INDEX_BLOCKS; i++)
  {
    indir_block->index_blocks[i] = NULL;
  }

  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{


  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      int i;
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          int j;

          if(inode->data.indir_block != NULL)
          {
            //free the indirect block
            for(i = 0; i < INDIRECT_INDEX_BLOCKS; i++)
            {
              if(inode->data.indir_block->index_blocks[i] != NULL) 
              {
                //free the direct blocks for each individual index block in the indirect block
                for(j = 0; j < INDEX_DIRECT_BLOCKS; j++) 
                {
                  if(inode->data.indir_block->index_blocks[i]->direct_blocks[j] != -1)
                    free_map_release(inode->data.indir_block->index_blocks[i]->direct_blocks[j], 1);
                }
                //free the index block itself  
                free_map_release(inode->data.indir_block->index_sectors[i], 1);
              }
            }
            //free the indirect block
            free_map_release(inode->data.doubly_indirect_block, 1);
          }

          //free the index block
          if(inode->data.indx_block != NULL) 
          {
            //free the direct blocks within the index block
            for(j = 0; j < INDEX_DIRECT_BLOCKS; j++)
            {
              if(inode->data.indx_block->direct_blocks[j] != -1)
                free_map_release(inode->data.indx_block->direct_blocks[j], 1);
            }
            free_map_release(inode->data.index_block, 1);
          }
        
          //free the direct blocks on inode_disk
          for(j = 0; j < INODE_DIRECT_BLOCKS; j++)
          {
            if(inode->data.direct_blocks[j] != -1)
              free_map_release(inode->data.direct_blocks[j], 1);
          }
        }


      //free all the pointers
      //start with indirect block pointers
      if(inode->data.indir_block != NULL)
      {
        for(i = 0; i < INDIRECT_INDEX_BLOCKS; i++)
        {
          free(inode->data.indir_block->index_blocks[i]);
          inode->data.indir_block->index_blocks[i] = NULL;
        }
        free(inode->data.indir_block);
        inode->data.indir_block = NULL;
      }

      if(inode->data.indx_block != NULL) 
      {
        free(inode->data.indx_block);
        inode->data.indx_block = NULL;
      }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  ASSERT(inode->data.magic == INODE_MAGIC);

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
  {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector (inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    //printf("inode size: %d\n", inode_length (inode));
    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      {
        /* Write full sector directly to disk. */
        block_write (fs_device, sector_idx, buffer + bytes_written);
      }
    else 
      {
        /* We need a bounce buffer. */
        if (bounce == NULL) 
          {
            bounce = malloc (BLOCK_SECTOR_SIZE);
            if (bounce == NULL)
              break;
          }

        /* If the sector contains data before or after the chunk
           we're writing, then we need to read in the sector
           first.  Otherwise we start with a sector of all zeros. */
        if (sector_ofs > 0 || chunk_size < sector_left) 
          block_read (fs_device, sector_idx, bounce);
        else
          memset (bounce, 0, BLOCK_SECTOR_SIZE);
        memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
        block_write (fs_device, sector_idx, bounce);
      }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free (bounce);
  ASSERT(inode->data.magic == INODE_MAGIC);

  printf("inode.c#inode_write_at:bytes_written = %d\n", bytes_written);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  return inode->data.size;
}
