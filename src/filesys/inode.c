#include "filesys/inode.h"
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}


bool direct_block_allocation (struct inode_disk *, char *, size_t numsectors);
bool index_block_allocation(block_sector_t *, char *, size_t numsectors);
bool indirect_block_allocation(struct inode_disk *, char *zeros, size_t numsectors);
bool allocate_single_sector(block_sector_t *);
void * bring_in_sector(bool is_for_write, block_sector_t * target_sector, unsigned num_sub_blocks);

/* Lock for the list of open inodes */
struct lock open_inodes_lock;



/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
//eddy drove here
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos, bool must_grow) 
{
  ASSERT (inode != NULL);

  off_t sector_index = pos / BLOCK_SECTOR_SIZE;

  if (sector_index < INODE_DIRECT_BLOCKS)
    {
      if(must_grow && inode->data.direct_blocks[sector_index] == -1) 
        {
          if(!allocate_single_sector(&inode->data.direct_blocks[sector_index]))
            PANIC("Not enough sectors");
        }
      return inode->data.direct_blocks[sector_index];
    }

  else if (sector_index < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS)
    {
      if (inode->index_block_pointer == NULL)
        { 
          inode->index_block_pointer = (struct index_block *) bring_in_sector(must_grow, &inode->data.index_block, INDEX_DIRECT_BLOCKS);
          if (inode->index_block_pointer == NULL)  // if accessing in a read, don't do anything
            return -1;
        }  
      //andrew drove here
      if(must_grow && inode->index_block_pointer->direct_blocks[sector_index-INODE_DIRECT_BLOCKS] == -1)
        {
          if(!allocate_single_sector(&inode->index_block_pointer->direct_blocks[sector_index-INODE_DIRECT_BLOCKS]))
            PANIC("Not Enough Sectors (index)");
        }
      return inode->index_block_pointer->direct_blocks[sector_index-INODE_DIRECT_BLOCKS];
    }

  else if (sector_index < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS + INDIRECT_INDEX_BLOCKS * INDEX_DIRECT_BLOCKS)
    {
      unsigned index_block_index = (sector_index - (INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS))/INDEX_DIRECT_BLOCKS;
      unsigned direct_block_index = (sector_index - (INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS))%INDEX_DIRECT_BLOCKS;
      if (inode->indirect_index_blocks[index_block_index] == NULL)
        {
          if (inode->indirect_block_pointer == NULL)
            {
              inode->indirect_block_pointer = (struct indirect_block *) bring_in_sector(must_grow, &inode->data.doubly_indirect_block, INDIRECT_INDEX_BLOCKS);
              if (inode->indirect_block_pointer == NULL) // if accessing in a read, don't do anything
                return -1;
            }

          inode->indirect_index_blocks[index_block_index] = (struct index_block *) bring_in_sector(must_grow, &inode->indirect_block_pointer->index_sectors[index_block_index], INDEX_DIRECT_BLOCKS);
          if (inode->indirect_index_blocks[index_block_index] == NULL) // if accessing in a read, don't do anything
            return -1;
        
        }
      if(must_grow && inode->indirect_index_blocks[index_block_index]->direct_blocks[direct_block_index] == -1)
        {
          if(!allocate_single_sector(&inode->indirect_index_blocks[index_block_index]->direct_blocks[direct_block_index]))
            PANIC("Not Enough Sectors (indirect)");
        }
      return inode->indirect_index_blocks[index_block_index]->direct_blocks[direct_block_index];
    }
  else
    PANIC("Above limit of max file size");
  
}
//nick drove here
void *
bring_in_sector(bool must_grow, block_sector_t * target_sector, unsigned num_sub_blocks){
  void * storage_location;
  int i;
  if(must_grow && *target_sector == -1)
    {//when writing, and trying to grow, grow indirect block 
      if(!allocate_single_sector(target_sector))
        PANIC("Not Enough Sectors");
      else
        {
          storage_location =  calloc(1, BLOCK_SECTOR_SIZE);
          for(i = 0; i < num_sub_blocks; i++)
            *( ((block_sector_t *) storage_location) + i) = -1;
        }
    }

  else if(!must_grow && *target_sector == -1) //when reading, and trying to grow, don't grow
    return NULL;

  else
    {// lazy loading of indirect sector
      storage_location = calloc(1, BLOCK_SECTOR_SIZE);
      block_read(fs_device, *target_sector, storage_location);
    }

  return storage_location;
}

bool
allocate_single_sector(block_sector_t * sector)
{
  static char zeros[BLOCK_SECTOR_SIZE];
  bool success = true;
  if(free_map_allocate(1, sector))
    block_write(fs_device, *sector, zeros);
  else
    success = false;

  return success;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  lock_init (&open_inodes_lock);
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  //andre drove here
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  ASSERT (length >= 0);
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if(disk_inode != NULL)
    {
      disk_inode->index_block = -1;
      disk_inode->doubly_indirect_block = -1;

      size_t sectors = bytes_to_sectors (length);

      disk_inode->length = length;
      disk_inode->is_dir = is_dir;
      disk_inode->magic = INODE_MAGIC;
      static char zeros[BLOCK_SECTOR_SIZE];

      bool direct_success;
      bool index_success;
      bool indir_success;
      //radu drove here
      int i;
      for(i = 0; i < INODE_DIRECT_BLOCKS; i++)
        disk_inode->direct_blocks[i] = -1;
      

      if (sectors < INODE_DIRECT_BLOCKS)
        success = direct_block_allocation(disk_inode, zeros, sectors);
      
      else if(sectors < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS)
        {
          direct_success = direct_block_allocation(disk_inode, zeros, INODE_DIRECT_BLOCKS);
          index_success = index_block_allocation(&disk_inode->index_block, zeros, sectors-INODE_DIRECT_BLOCKS);
          success = direct_success && index_success;
        }

      else if(sectors < INODE_DIRECT_BLOCKS + INDEX_DIRECT_BLOCKS + INDIRECT_INDEX_BLOCKS * INDEX_DIRECT_BLOCKS)
        {
          direct_success = direct_block_allocation(disk_inode, zeros, INODE_DIRECT_BLOCKS);
          index_success = index_block_allocation(&disk_inode->index_block, zeros, INDEX_DIRECT_BLOCKS);
          indir_success = indirect_block_allocation(disk_inode, zeros, sectors - (INODE_DIRECT_BLOCKS+INDEX_DIRECT_BLOCKS));
          success = direct_success && index_success && indir_success;
        }
      else
        success = false;

      block_write (fs_device, sector, disk_inode);
      free(disk_inode);

      return success;
    }
}
  //andrew drove here
  bool
  direct_block_allocation (struct inode_disk *disk_inode, char* zeros, size_t numsectors)
  {
    int i;
    bool success = true;

    for(i = 0; i < INODE_DIRECT_BLOCKS; i++) 
      disk_inode->direct_blocks[i] = -1;

    for (i = 0; i < numsectors; ++i)
      {
        if(free_map_allocate(1, &disk_inode->direct_blocks[i]))
          block_write (fs_device, disk_inode->direct_blocks[i], zeros);
        else
          success = false;
      }

    return success;
  }
  //nick and eddy drove here
  bool
  index_block_allocation (block_sector_t * index_allocate, char *zeros, size_t numsectors)
  {
    int i;
    bool success = true;
    block_sector_t direct_blocks[INDEX_DIRECT_BLOCKS];

    for(i = 0; i < INDEX_DIRECT_BLOCKS; i++) 
      direct_blocks[i] = -1;

    //allocate space for direct blocks
    for(i = 0; i < numsectors; i++) 
      {
        if (free_map_allocate(1, &direct_blocks[i]))
          block_write(fs_device, direct_blocks[i], zeros);
        else
          success = false;
      }
    //allocate space for the index block
    block_sector_t index_block;
    if (free_map_allocate(1, &index_block))
      block_write(fs_device, index_block, direct_blocks);
    else
      success = false;

    *index_allocate = index_block;
    return success;
  }
  //radu and andrew drove here
  bool
  indirect_block_allocation (struct inode_disk * disk_inode, char * zeros, size_t numsectors)
  {
    int i;
    bool success = true;
    size_t num_index_blocks_minus_one = numsectors/INDEX_DIRECT_BLOCKS;
    size_t direct_blocks_left = numsectors%INDEX_DIRECT_BLOCKS;
    block_sector_t indirect_index_blocks[INDIRECT_INDEX_BLOCKS];

    for (i = 0; i < INDIRECT_INDEX_BLOCKS; ++i)
      indirect_index_blocks[i] = -1;

    //allocate all the direct blocks within 
    for(i = 0; i < num_index_blocks_minus_one; i++)
      index_block_allocation(&indirect_index_blocks[i], zeros, INDEX_DIRECT_BLOCKS);
      
    if (direct_blocks_left != 0)      
      index_block_allocation(&indirect_index_blocks[i], zeros, direct_blocks_left);
      
    if(free_map_allocate(1, &disk_inode->doubly_indirect_block))
      block_write(fs_device, disk_inode->doubly_indirect_block, indirect_index_blocks);
    else
      success = false;
    return success;
  }

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  //printf("############## %s inode_open\n", thread_current()->name);
  //lock_acquire(&master_inode_lock);

  struct list_elem *e;
  struct inode *inode;
  //eddy drove here
  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
        e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      lock_acquire(&inode->inode_lock);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          lock_release(&open_inodes_lock);//inode_lock released in inode_reopen
          return inode; 
        }
      lock_release(&inode->inode_lock);
    }
  
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;
  //nick drove here
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->index_block_pointer = NULL;
  inode->indirect_block_pointer = NULL;
  lock_init(&inode->inode_lock);
  int i;
  for (i = 0; i < INDIRECT_INDEX_BLOCKS; ++i)
    inode->indirect_index_blocks[i] = NULL;
    
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}
 //radu drove here
/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
 
  if(!lock_held_by_current_thread (&inode->inode_lock))//because inode_reopen is also called in inode_open 
    lock_acquire(&inode->inode_lock);

  if (inode != NULL)
    inode->open_cnt++;
  lock_release(&inode->inode_lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  lock_acquire(&inode->inode_lock);
  block_sector_t inode_sector = inode->sector;
  lock_release(&inode->inode_lock);

  return inode_sector;
}

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  int i,j;
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire(&inode->inode_lock);
  //andrew drove here
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire(&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release(&open_inodes_lock);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          for (i = 0; i < INODE_DIRECT_BLOCKS; ++i)
            {
              if (inode->data.direct_blocks[i] != -1)
                free_map_release (inode->data.direct_blocks[i], 1);
            }
          if(inode->data.index_block != -1)
            {
              if (inode->index_block_pointer == NULL)
                { 
                  inode->index_block_pointer =  calloc(1, sizeof (struct index_block));
                  block_read(fs_device, inode->data.index_block, inode->index_block_pointer);
                }
              for (i = 0; i < INDEX_DIRECT_BLOCKS; ++i)
                {
                  if (inode->index_block_pointer->direct_blocks[i] != -1)
                    free_map_release (inode->index_block_pointer->direct_blocks[i], 1);
                }
            }
          if(inode->data.doubly_indirect_block != -1) 
            {
              if (inode->indirect_block_pointer == NULL)
                {
                  inode->indirect_block_pointer = calloc(1, sizeof (struct indirect_block));
                  block_read(fs_device, inode->data.doubly_indirect_block, inode->indirect_block_pointer);
                }
              for (i = 0; i < INDIRECT_INDEX_BLOCKS; ++i)
                {
                  if (inode->indirect_block_pointer->index_sectors[i] != -1)
                    {
                      if (inode->indirect_index_blocks[i] == NULL)
                        {
                          inode->indirect_index_blocks[i] = calloc(1, sizeof (struct index_block));
                          block_read(fs_device, inode->indirect_block_pointer->index_sectors[i], inode->indirect_index_blocks[i]);
                        }
                      for (j = 0; j < INDEX_DIRECT_BLOCKS; ++j)
                        {
                            if(inode->indirect_index_blocks[i]->direct_blocks[j] != -1)
                              free_map_release(inode->indirect_index_blocks[i]->direct_blocks[j], 1);
                        }
                      free_map_release (inode->indirect_block_pointer->index_sectors[i], 1);
                    }
                }
            }
        }
        //eddy drove here
        else
          {
            if(inode->index_block_pointer != NULL) 
              block_write(fs_device, inode->data.index_block, inode->index_block_pointer);

            if(inode->indirect_block_pointer != NULL) 
              block_write(fs_device, inode->data.doubly_indirect_block, inode->indirect_block_pointer);

            int i;
            for(i = 0; i < INDIRECT_INDEX_BLOCKS; i++)
              {
                if(inode->indirect_index_blocks[i] != NULL)
                  block_write(fs_device, inode->indirect_block_pointer->index_sectors[i], inode->indirect_index_blocks[i]);      
              }
              block_write(fs_device, inode->sector, &inode->data);
          }
      //radu drove here
      if (inode->index_block_pointer != NULL)
        free(inode->index_block_pointer);

      if (inode->indirect_block_pointer != NULL)
        free(inode->indirect_block_pointer);

      for (i = 0; i < INDIRECT_INDEX_BLOCKS; ++i)
        {
          if (inode->indirect_index_blocks[i] != NULL)
            free(inode->indirect_index_blocks[i]);
        }
      lock_release(&inode->inode_lock);
      free (inode);
      return;
    }
  lock_release(&inode->inode_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  lock_acquire(&inode->inode_lock);
  ASSERT (inode != NULL);
  inode->removed = true;
  lock_release(&inode->inode_lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
//nick drove here
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  if (inode->data.is_dir)
    lock_acquire(&inode->inode_lock);

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  bool have_to_grow = false;

  if(size+offset < inode->data.length)
    have_to_grow = true;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, have_to_grow);
      if(sector_idx == -1) //Trying to read an unallocated sector
        {
          if (inode->data.is_dir)
            lock_release(&inode->inode_lock);

          return bytes_read; 
        }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->data.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      //andrew drove here
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

  if (inode->data.is_dir)
    lock_release(&inode->inode_lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
    //radu drove here
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  bool lock_held_outside = true;
  off_t size_required = size;
  off_t inode_curr_length;

  if(!lock_held_by_current_thread (&inode->inode_lock))
    {
      lock_acquire(&inode->inode_lock);
      inode_curr_length = inode->data.length;
      lock_held_outside = false;

      if (!(inode->data.is_dir || size+offset > inode->data.length))
        lock_release(&inode->inode_lock);      

      else if (size+offset > inode->data.length)
        inode_curr_length = size+offset;
    }
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  //eddy drove here
  if (inode->deny_write_cnt)
    {
      if(!lock_held_outside && lock_held_by_current_thread (&inode->inode_lock))
        lock_release(&inode->inode_lock);
      return 0;
    }

  while (size > 0) 
    {
      block_sector_t sector_idx = byte_to_sector (inode, offset, true);
  
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_curr_length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      //andrew drove here
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
             //nick drove here
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

  inode->data.length = inode_curr_length;

  if(!lock_held_outside && lock_held_by_current_thread (&inode->inode_lock))
    lock_release(&inode->inode_lock);
  return bytes_written;
}


/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&inode->inode_lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->inode_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire(&inode->inode_lock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->inode_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  lock_acquire(&inode->inode_lock);
  int inode_data_length = inode->data.length;
  lock_release(&inode->inode_lock);
  return inode_data_length;
}
