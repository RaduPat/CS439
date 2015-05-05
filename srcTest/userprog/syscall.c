#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/fsutil.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "userprog/pagedir.h"
#include "filesys/directory.h"
#include "filesys/inode.h"

#define MAX_FILES 128
#define MAX_DIR_DEPTH 20

static void syscall_handler (struct intr_frame *);

/* helper functions to implement system calls
    They make the switch statement cleaner */

void halt_h(void);
void exit_h (int status);
tid_t exec_h (char *cmd_line);
int wait_h (tid_t tid);
bool create_h (char *file, unsigned initial_size);
bool remove_h (char *file);
int open_h (char *file);
int filesize_h (int file_descriptor);
int read_h (int file_descriptor, void *buffer, unsigned size);
int write_h (int file_descriptor, void *buffer, unsigned size);
int seek_h (int file_descriptor, unsigned position);
unsigned tell_h (int file_descriptor);
int close_h (int file_descriptor);
bool mkdir_h (const char *);
bool isdir_h (int fd);
bool chdir_h (const char *);
int inumber_h (int fd);
bool readdir_h (int fd, char*);

// checks the validity of a pointer
void check_pointer (void *pointer);
//parse the path given
char ** parse_path (char * path);

// returns a pointer to the file given the file descriptor
struct file * find_open_file (int fd);
// traverses the directory structure
struct dir * get_target_dir(struct dir *, char **, int *);

/* function to find the file descriptor of a file
    This function takes as input the file for which
    the file descriptor is required, as well as the
    array of files owned by the thread which owns
    the aforementioned file */
int find_fd(struct file * files[], struct file * target);

/* function to assign a file descriptor to a file
    This function takes as input the file which 
    needs a file descriptor, as well as the array
    of files owned by the thread which owns the
    aforementioned file */
void assign_fd(struct file *open_file, struct file * files[]);

// prototype to pintos shutdown function - this was placed here to silence a warning.
void shutdown_power_off(void);

/* lock to synchronize access to the filesystem */
static struct lock syscall_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&syscall_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	/* Eddy drove here */
  check_pointer (f->esp);

  int *esp_int_pointer = (int*) f->esp;
  int syscall_number = *esp_int_pointer;

  switch (syscall_number) 
  	{
  	
  		case SYS_HALT:
  			{
 					halt_h ();
  	  	}
  		break;
			case SYS_EXIT:
				{
					int status;
					check_pointer (esp_int_pointer+1);
					status = (int) *(esp_int_pointer+1);
					exit_h (status);
				}
			break;
  		case SYS_EXEC:
				{
					check_pointer (esp_int_pointer+1);
					const char **cmd_line = (char **) (esp_int_pointer+1);
					f->eax = exec_h (*cmd_line);
				}
  		break;
  		case SYS_WAIT:
  			{	
	  			check_pointer (esp_int_pointer+1);
	  			tid_t tid = (tid_t) *(esp_int_pointer+1);
	  			f->eax = wait_h (tid);
  			}
  		break;
  		case SYS_CREATE:
  			{
	  			check_pointer (esp_int_pointer+1);
	  			check_pointer (esp_int_pointer+2);
	  			const char **file = (char **) (esp_int_pointer+1);
	  			unsigned initial_size = (unsigned) *(esp_int_pointer+2);
	  			f->eax = create_h (*file, initial_size);
  			}
  		break;
  		case SYS_REMOVE:
  			{
	  			check_pointer (esp_int_pointer+1);
	  			const char **file = (char **) (esp_int_pointer+1);
	  			f->eax = remove_h (*file);
  			}
  		break;
  	  case SYS_OPEN:
  			{
	  			check_pointer (esp_int_pointer+1);
	  			const char **file = (char **)(esp_int_pointer+1);
	  			f->eax = open_h (*file);
	  		}
  		break;
  	  case SYS_FILESIZE:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			int file_descriptor = (int) *(esp_int_pointer+1);
	  			f->eax = filesize_h (file_descriptor);
	  		}
  		break;
  		case SYS_READ:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			check_pointer (esp_int_pointer+2);
	  			check_pointer (esp_int_pointer+3);
	  			int file_descriptor = (int) *(esp_int_pointer+1);
	  			void **buffer = (void **) (esp_int_pointer+2);
	  			unsigned size = (unsigned) *(esp_int_pointer+3);
	  			f->eax = read_h (file_descriptor, *buffer, size);
	  		}
  		break;
	  	case SYS_WRITE:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			check_pointer (esp_int_pointer+2);
	  			check_pointer (esp_int_pointer+3);
	  			int file_descriptor = (int) *(esp_int_pointer+1);
	  			const void **buffer = (void **) (esp_int_pointer+2);
	  			unsigned size = (unsigned) *(esp_int_pointer+3);
	  			f->eax = write_h (file_descriptor, *buffer, size);
	  		}
	  	break;
	  	case SYS_SEEK:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			check_pointer (esp_int_pointer+2);
	  			int file_descriptor = (int) *(esp_int_pointer+1);
	  			unsigned pos = (unsigned) *(esp_int_pointer+2);
	  			f->eax = seek_h (file_descriptor, pos);
	  		}
	  	break;
	  	case SYS_TELL:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			int file_descriptor = (int) *(esp_int_pointer+1);
	  			f->eax = tell_h (file_descriptor);
	  		}
	  	break;
	  	case SYS_CLOSE:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			int file_descriptor = (int) *(esp_int_pointer+1);
	  			f->eax = close_h (file_descriptor);
	  		}
	  	break;
	  	case SYS_MKDIR:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			char ** dir_path = (char **) (esp_int_pointer+1);
	  			f->eax = mkdir_h (*dir_path);
	  		}
	  	break;
	  	case SYS_ISDIR:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			int fd = *(esp_int_pointer+1);
	  			f->eax = isdir_h(fd);
	  		}
	  	break;
	  	case SYS_CHDIR:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			char ** dir_path = (char **) (esp_int_pointer + 1);
	  			f->eax = chdir_h (*dir_path);
	  		}
	  	break;
	  	case SYS_INUMBER:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			int file_descriptor = * (esp_int_pointer + 1);
	  			f->eax = inumber_h (file_descriptor);
	  		}
	  	break;
	  	case SYS_READDIR:
	  		{
	  			check_pointer (esp_int_pointer+1);
	  			check_pointer (esp_int_pointer+2);
	  			int file_descriptor = * (esp_int_pointer+1);
	  			char ** name = (char **) (esp_int_pointer+2);
	  			f->eax = readdir_h (file_descriptor, *name);
	  		}
	  	break;
  	}
}

void
halt_h (void)
{
	shutdown_power_off ();
}

/* Nick drove here */
void
exit_h (int status)
{
    if (thread_current ()->stat_holder != NULL) {
        thread_current ()->stat_holder->status = status;
    }
    thread_current ()->status_number = status;
	thread_exit ();
}

tid_t
exec_h (char *cmd_line)
{
	check_pointer (cmd_line);
	lock_acquire (&syscall_lock);
	tid_t tid = process_execute (cmd_line);
	lock_release (&syscall_lock);

	return tid;
}

int
wait_h (tid_t tid)
{
	return process_wait (tid);
}

bool
create_h (char *path2file, unsigned initial_size) 
{
	check_pointer (path2file);

	struct dir * starting_dir;
	struct dir * target_dir;
	if(path2file[0] == '/')
		starting_dir = dir_open_root ();
	else
		starting_dir = dir_reopen (thread_current()->curr_dir);

	char * dir_path;
	dir_path = malloc(strlen(path2file)+1);
	strlcpy (dir_path, path2file, strlen(path2file)+1);
	char ** dir_names = parse_path(dir_path);
	bool success = true;
	int index_to_dir_name = -1;
	struct inode * target_inode = NULL;

	target_dir = get_target_dir(starting_dir, dir_names, &index_to_dir_name);

	if (target_dir == NULL)
	{
		success = false;
	}

	if(dir_names[index_to_dir_name] != NULL && success && !target_dir->inode->removed)
	{
		success = dir_lookup(target_dir, dir_names[index_to_dir_name], &target_inode);

		if(success)
			success = false; //the file is already created
		else
		{
			lock_acquire (&syscall_lock);
			success = filesys_create (dir_names[index_to_dir_name], (off_t) initial_size, target_dir);
			lock_release (&syscall_lock);
		}
	}
	else
		success = false;

	if (target_dir != NULL)
	{
		dir_close(target_dir);
	}
	if (target_inode != NULL)
	{
		inode_close(target_inode);
	}
	free(dir_path);
	free(dir_names);

	return success;
}

/* Andrew drove here */
bool
remove_h (char *path2file)
{
	check_pointer (path2file);
	struct dir * starting_dir;
	struct dir * target_dir;
	if(path2file[0] == '/')
		starting_dir = dir_open_root ();
	else
		starting_dir = dir_reopen (thread_current()->curr_dir);

	char * dir_path;
	dir_path = malloc(strlen(path2file)+1);
	strlcpy (dir_path, path2file, strlen(path2file)+1);
	char ** dir_names = parse_path(dir_path);
	bool success = true;
	int index_to_dir_name = -1;

	target_dir = get_target_dir(starting_dir, dir_names, &index_to_dir_name);

	if (target_dir == NULL)
	{
		success = false;
	}

	if(dir_names[index_to_dir_name] != NULL && success && !target_dir->inode->removed)
	{
		lock_acquire (&syscall_lock);
		success = filesys_remove (dir_names[index_to_dir_name], target_dir);
		lock_release (&syscall_lock);
	}
	else
		success = false;

	if (target_dir != NULL)
		dir_close(target_dir);

	free(dir_path);
	free(dir_names);

	return success;
}

int
open_h (char *path2file)
{
	check_pointer (path2file);
	struct dir * starting_dir;
	struct dir * target_dir;
	if(path2file[0] == '/')
		starting_dir = dir_open_root ();
	else
		starting_dir = dir_reopen (thread_current()->curr_dir);

	char * dir_path;
	dir_path = malloc(strlen(path2file)+1);
	strlcpy (dir_path, path2file, strlen(path2file)+1);
	char ** dir_names = parse_path(dir_path);
	bool success = true;
	struct file *open_file = NULL;
	int index_to_dir_name = -1;

	if (dir_names[0] == NULL && path2file[0] == '/')//accounting for the special case where a path name is nothing but forward slashes
	{
		open_file = file_open(starting_dir->inode);
		if (open_file == NULL)
		{
			success = false;
		}
		assign_fd (open_file, thread_current () -> open_files);

		dir_close(starting_dir);
		free(dir_path);
		free(dir_names);

		if (success)	
		{
			return find_fd (thread_current ()->open_files, open_file);
		}
		else
			return -1;
	}
 	
	target_dir = get_target_dir(starting_dir, dir_names, &index_to_dir_name);

	if (target_dir == NULL)
	{
		success = false;
	}

	if(dir_names[index_to_dir_name] != NULL && success && !target_dir->inode->removed)
	{
		lock_acquire (&syscall_lock);
		open_file = filesys_open (dir_names[index_to_dir_name], target_dir);
		lock_release (&syscall_lock);
		if (open_file == NULL)
			success = false;
		else
			assign_fd (open_file, thread_current () -> open_files);
	}
	else
		success = false;

	if (target_dir != NULL)
		dir_close(target_dir);

	free(dir_path);
	free(dir_names);

	if (success)
		return find_fd (thread_current ()->open_files, open_file);
	else
		return -1;
}

int 
filesize_h (int file_descriptor)
{
	struct file *found_file = find_open_file (file_descriptor);
	int file_size;
	if (found_file != NULL)
		file_size = file_length (found_file);
	else
		file_size = -1;
	return file_size;
}

int
read_h (int file_descriptor, void *buffer, unsigned size)
{
	check_pointer (buffer);
	if (file_descriptor == STDIN_FILENO)
		{
			uint8_t *new_buffer = (uint8_t *) buffer;
			int i;
			for (i = 0; i < (int) size; i++)
				{
					*new_buffer = input_getc ();
					new_buffer++;
				}
			return (int) size;
		}
	else
		{
			int bytes_read = -1;
			struct file *found_file = find_open_file (file_descriptor);
			if (found_file != NULL) 
			{
				lock_acquire (&syscall_lock);
				bytes_read = file_read (found_file, buffer, size);
				lock_release (&syscall_lock);
			}
			return bytes_read;
		}
}

int
write_h (int file_descriptor, void *buffer, unsigned size)
{
	check_pointer (buffer);
	if (file_descriptor == STDOUT_FILENO)
		{
			putbuf ((char *) buffer, (size_t) size);
			return (int) size;
		}
	else
		{
			int bytes_written = -1;
			struct file *found_file = find_open_file (file_descriptor);
			if (found_file != NULL && !found_file->inode->data.is_dir)
				{
					lock_acquire (&syscall_lock);
					bytes_written = file_write (found_file, buffer, size);
					lock_release (&syscall_lock);
				}
			return bytes_written;
		}
}

/* Radu drove here */
int
seek_h (int file_descriptor, unsigned position) 
{
	struct file *found_file = find_open_file (file_descriptor);
	if (found_file != NULL)
		{
			lock_acquire (&syscall_lock);
			file_seek (found_file, (off_t) position);
			lock_release (&syscall_lock);
			return 1; //returning 1 and -1 to signify pushing to EAX
		}
	return -1;
}

unsigned
tell_h (int file_descriptor)
{
	struct file *found_file = find_open_file (file_descriptor);
	if (found_file != NULL)
		{
			lock_acquire (&syscall_lock);
			unsigned ret =  (unsigned) file_tell (found_file);
			lock_release (&syscall_lock);
			return ret;
		}
	return -1;
}

int
close_h (int file_descriptor)
{
	struct file *found_file = find_open_file (file_descriptor);
	if (found_file != NULL) 
		thread_current () -> open_files[file_descriptor] = NULL;
	else
        return -1;
    file_close(found_file);
	return 1;
}

bool
mkdir_h (const char *path)
{
	check_pointer(path);
	struct dir * starting_dir;
	struct dir * target_dir;
	if(path[0] == '/')
		starting_dir = dir_open_root ();
	else
		starting_dir = dir_reopen (thread_current()->curr_dir);

	char * dir_path;
	dir_path = malloc(strlen(path)+1);
	strlcpy (dir_path, path, strlen(path)+1);
	char ** dir_names = parse_path(dir_path);
	bool success = true;
	int index_to_dir_name = -1;
	struct inode * target_inode = NULL;

	target_dir = get_target_dir(starting_dir, dir_names, &index_to_dir_name);

	if (target_dir == NULL)
	{
		success = false;
	}

	//LAST ENTRY
	if(dir_names[index_to_dir_name] != NULL && success && !target_dir->inode->removed)
	{
		success = dir_lookup(target_dir, dir_names[index_to_dir_name], &target_inode);

		if(success)
			success = false; //the directory is already created
		else
		{
			block_sector_t new_dir_sector;
			if(!free_map_allocate(1, &new_dir_sector))
				PANIC("No space for new directory");
			success = dir_create(new_dir_sector, 2) && dir_add(target_dir, dir_names[index_to_dir_name], new_dir_sector);

			//adding "." and ".."
			struct dir * new_dir = dir_open(inode_open(new_dir_sector));
			if (new_dir != NULL)
			{
				success = dir_add(new_dir, ".", new_dir_sector) && dir_add(new_dir, "..", target_dir->inode->sector);
			}
			else{
				success = false;
			}
			dir_close(new_dir);
		}
	}
	else
		success = false;

	if (target_dir != NULL)
		dir_close(target_dir);
	if(target_inode != NULL)
		inode_close(target_inode);

	free(dir_path);
	free(dir_names);
	return success;
}

bool
isdir_h (int file_descriptor)
{
	struct file *found_file = find_open_file (file_descriptor);
	if (found_file != NULL)
	{
		return found_file->inode->data.is_dir;
	}
	else{
		return false;// false is also used to denote that an invalide fd was passed in
	}
}

bool
chdir_h (const char *path){
	check_pointer (path);
	struct dir * starting_dir;
	struct dir * target_dir;
	if(path[0] == '/')
		starting_dir = dir_open_root ();
	else
		starting_dir = dir_reopen (thread_current()->curr_dir);

	char * dir_path;
	dir_path = malloc(strlen(path)+1);
	strlcpy (dir_path, path, strlen(path)+1);
	char ** dir_names = parse_path(dir_path);
	bool success = true;
	int index_to_dir_name = -1;
	struct inode * target_inode = NULL;

	if (dir_names[0] == NULL && path[0] == '/')//accounting for the special case where a path name is nothing but forward slashes
	{
		dir_close(thread_current()->curr_dir);
		thread_current()->curr_dir = starting_dir;

		free(dir_path);
		free(dir_names);

		return success;
	}

	target_dir = get_target_dir(starting_dir, dir_names, &index_to_dir_name);

	if (target_dir == NULL)
	{
		success = false;
	}

	//LAST ENTRY
	if(dir_names[index_to_dir_name] != NULL && success && !target_dir->inode->removed)
	{
		success = dir_lookup(target_dir, dir_names[index_to_dir_name], &target_inode);

		if(success)
			if (target_inode->data.is_dir)
			{
				dir_close(thread_current()->curr_dir);
				thread_current()->curr_dir = dir_open(target_inode);
			}
			else
				success = false;
		else
		{
			success = false;
		}
	}
	else
		success = false;

	if (target_dir != NULL)
	{
		dir_close(target_dir);
	}
	free(dir_path);
	free(dir_names);
	return success;
}

int 
inumber_h (int fd){
	struct file * req_file = find_open_file(fd);
	if (req_file != NULL)
	{
		return req_file->inode->sector;
	}
	else{
		return -1;
	}
}

bool
readdir_h (int fd, char* name) 
{
	check_pointer(name);
	struct file * req_file = find_open_file(fd);
	bool success = false;
	if (req_file != NULL)
	{
		if(req_file->inode->data.is_dir)
		{
			struct dir * directory = dir_open(req_file->inode);
			success = dir_readdir(directory, name);
			free(directory);
		}
	}
	return success;
}

struct file *
find_open_file (int fd) 
{
	if (fd < 0 || fd >= MAX_FILES)
		return NULL;
	struct file *return_file = thread_current ()->open_files[fd];
	if (return_file != NULL)
		return return_file;
	else
		return NULL; 
}

void
check_pointer (void *pointer)
{
	//check above phys base			check within its own page
	if(is_kernel_vaddr (pointer) || pagedir_get_page (thread_current ()->pagedir, pointer) == NULL)
		exit_h (-1);
	//exit_h will handle freeing the page and closing the process
}

/* Function to iterate through an array of files to assign a file descriptor.
Should loop around to the beginning of the array upon reaching the last index*/
void
assign_fd (struct file *new_file, struct file * files[])
{
	int i;
	for(i = thread_current ()->index_fd; i <  MAX_FILES ; i++)
		{
			if (files[i] == NULL)  
				{
					files[i] = new_file;
					thread_current ()->index_fd = i;
					break;
				}
			//wrap around
			if (i == MAX_FILES - 1)
				i = 2;
		}
}

/* Function to find a file descriptor in an array of open files */
int
find_fd(struct file * files[], struct file * target) 
{
	int i;
	for (i = 2; i <  MAX_FILES ; i++)
		{
			if (files[i] == target)
				return i;
		}
	return -1;
}

/* Function to parse a directory path, absolute or relative, in order
to get each directory name */
char **
parse_path (char * path)
{
	char ** dir_names = calloc(MAX_DIR_DEPTH+1, sizeof (char *));
 	char *token, *save_ptr;
 	int dir_depth_counter = 0;

	for (token = strtok_r (path, "/", &save_ptr); token != NULL;
	      token = strtok_r (NULL, "/", &save_ptr))
	  {
	    dir_names[dir_depth_counter] = token;
	    dir_depth_counter++;
	  }

	return dir_names;
}

struct dir *
get_target_dir(struct dir * starting_directory, char ** array_of_dir_names, int * index_to_final_name){
	int i = 0;
	struct inode * target_inode = NULL;
	bool success = true;
	struct dir * target_directory = starting_directory;
	while(array_of_dir_names[i+1] != NULL && target_directory != NULL)
	{
		success = dir_lookup(target_directory, array_of_dir_names[i], &target_inode);
		if (success)
		{
			if (target_inode->data.is_dir && !target_inode->removed)
			{
				dir_close(target_directory);
				target_directory = dir_open(target_inode);
			}
			else
				target_directory = NULL;
		}
		else
			target_directory = NULL;

		i++;
	}

	*index_to_final_name = i;

	return target_directory;
}
