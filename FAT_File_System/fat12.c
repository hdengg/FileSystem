#include "fat12.h"

#include <fuse.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/fsuid.h>
#include <libgen.h>

/* read_unsigned_le: Reads a little-endian unsigned integer number
   from buffer, starting at position.
   
   Parameters:
     buffer: memory position of the buffer that contains the number to
             be read.
     position: index of the initial position within the buffer where
               the number is to be found.
     num_bytes: number of bytes used by the integer number within the
                buffer. Cannot exceed the size of an int.
   Returns:
     The unsigned integer read from the buffer.
 */
unsigned int read_unsigned_le(const char *buffer, int position, int num_bytes) {
  long number = 0;
  while (num_bytes-- > 0) {
    number = (number << 8) | (buffer[num_bytes + position] & 0xff);
  }
  return number;
}

/*
 * Removes white space from a char
 * Parameter: pointer to char
 */
void remove_spaces(char* source)
{
	char* i = source;
	char* j = source;
	while(*j != 0)
	{
		*i = *j++;
		if(*i != ' ')
			i++;
	}
	*i = 0;
}

/*
 * Adds a period between file and extension
 * Parameter: pointer to char
 */
void add_period(char* source)
{

    char strC[11];
    char * strB = ".";
    int x = 8;

    strncpy(strC, source, x);
    strC[x] = '\0';
    strcat(strC, strB);
    strcat(strC, source + x);
    strcpy(source, strC);
}

/*
 * Counts number of tokens in a path
 * Parameter: pathfile name
 */
int count_tokens(const char* path) {
    int count = 0;
    while ((path = strchr(path, '/')) != NULL) {
        count++;
        path++;
    }
    return count;
}


/* open_volume_file: Opens the specified file and reads the initial
   FAT12 data contained in the file, including the boot sector, file
   allocation table and root directory.

   Parameters:
     filename: Name of the file containing the volume data.
   Returns:
     A pointer to a newly allocated fat12volume data structure with
     all fields initialized according to the data in the volume file,
     or NULL if the file is invalid, data is missing, or the file is
     smaller than necessary.
 */
fat12volume *open_volume_file(const char *filename) {
	printf("==========================="); 
    FILE * fileptr;
	char * buffer;
	long filelen;

	char * fat;
	char * rootdir;

	// Get the size of the file
	fileptr = fopen(filename, "rb");
	fseek(fileptr, 0, SEEK_END);
	filelen = ftell(fileptr);

	// rewind the file
	rewind(fileptr);

	// if file did not get read or too small, return null
	if (filelen == 0) {
	    return NULL;
	}

	// malloc buffer
	buffer = (char *)malloc((filelen+1) * sizeof(char));
	fread(buffer, filelen, 1, fileptr);

	// malloc data structure to hold all fields
	fat12volume *fat12ptr = (fat12volume*) malloc(sizeof(fat12volume));

	fat12ptr->volume_file = fileptr;
	fat12ptr->sector_size = read_unsigned_le(buffer, 11, 2);
	fat12ptr->cluster_size = read_unsigned_le(buffer, 13, 1);
	fat12ptr->reserved_sectors= read_unsigned_le(buffer, 14, 2);
	fat12ptr->hidden_sectors= read_unsigned_le(buffer, 28, 2);
	fat12ptr->fat_offset=  fat12ptr->reserved_sectors;
	fat12ptr->fat_num_sectors=read_unsigned_le(buffer, 22, 2);
	fat12ptr->fat_copies= read_unsigned_le(buffer, 16, 1);

	// if able to read sectors, store the FAT
	if (read_sectors(fat12ptr, fat12ptr->fat_offset, fat12ptr->fat_num_sectors, &fat)) {
	    fat12ptr->fat_array = fat;
    } else {
	    // else missing data
	    return NULL;
	}


	fat12ptr->rootdir_offset= fat12ptr->fat_offset + (fat12ptr->fat_num_sectors * fat12ptr->fat_copies);
	fat12ptr->rootdir_entries= read_unsigned_le(buffer, 17, 2);
	fat12ptr->rootdir_num_sectors = (fat12ptr->rootdir_entries * DIR_ENTRY_SIZE) / fat12ptr->sector_size;

	// if able to read root dir, store root dir
	if (read_sectors(fat12ptr, fat12ptr->rootdir_offset, fat12ptr->rootdir_num_sectors, &rootdir)) {
	    fat12ptr->rootdir_array = rootdir;
	} else {
	    // else missing data
	    return NULL;
	}

	fat12ptr->rootdir_array = rootdir;
	fat12ptr->cluster_offset= fat12ptr->rootdir_offset + fat12ptr->rootdir_num_sectors - (2 * fat12ptr->cluster_size);


	return fat12ptr;
}

/* close_volume_file: Frees and closes all resources used by a FAT12 volume.
   
   Parameters:
     volume: pointer to volume to be freed.
 */
void close_volume_file(fat12volume *volume) {

  free(volume->rootdir_array);
  free(volume->fat_array);
  fclose(volume->volume_file);
  free(volume);
  volume = NULL;
}

/* read_sectors: Reads one or more contiguous sectors from the volume
   file, saving the data in a newly allocated memory space. The caller
   is responsible for freeing the buffer space returned by this
   function.
   
   Parameters:
     volume: pointer to FAT12 volume data structure.
     first_sector: number of the first sector to be read.
     num_sectors: number of sectors to read.
     buffer: address of a pointer variable that will store the
             allocated memory space.
   Returns:
     In case of success, it returns the number of bytes that were read
     from the set of sectors. In that case *buffer will point to a
     malloc'ed space containing the actual data read. If there is no
     data to read (e.g., num_sectors is zero, or the sector is at the
     end of the volume file, or read failed), it returns zero, and
     *buffer will be undefined.
 */
int read_sectors(fat12volume *volume, unsigned int first_sector, unsigned int num_sectors, char **buffer) {

	int sector_size, array_size;

	sector_size = volume->sector_size;
	array_size = num_sectors*sector_size;

	char *temp;

	// checking if sector is at end of volume file and whether num_sector is 0
	fseek(volume->volume_file, 0, SEEK_END);
	long filelen = ftell(volume->volume_file);
	rewind(volume->volume_file);
	if (((first_sector*sector_size) > filelen) || (num_sectors == 0)) {
		return 0;
	}

	// allocate a new buffer that will store the data read
	temp = (char *)malloc((array_size + 1) * sizeof(char));

	// move file pointer to offset
	fseek(volume->volume_file, first_sector*sector_size, SEEK_SET);

	// read the contents into temp buffer
	int bytes_read = fread(temp, 1, array_size, volume->volume_file);

	// point *buffer to the malloc'd space
	*buffer = temp;

	return bytes_read;
	
}

/* read_cluster: Reads a specific data cluster from the volume file,
   saving the data in a newly allocated memory space. The caller is
   responsible for freeing the buffer space returned by this
   function. Note that, in most cases, the implementation of this
   function involves a single call to read_sectors with appropriate
   arguments.
   
   Parameters:
     volume: pointer to FAT12 volume data structure.
     cluster: number of the cluster to be read (the first data cluster
              is numbered two).
     buffer: address of a pointer variable that will store the
             allocated memory space.
   Returns:
     In case of success, it returns the number of bytes that were read
     from the cluster. In that case *buffer will point to a malloc'ed
     space containing the actual data read. If there is no data to
     read (e.g., the cluster is at the end of the volume file), it
     returns zero, and *buffer will be undefined.
 */
int read_cluster(fat12volume *volume, unsigned int cluster, char **buffer) {
	unsigned int firstSector = cluster * volume->cluster_size  + 33;
	unsigned int bytes_read = 0;
	unsigned int clusterSize =  volume->cluster_size;
	bytes_read = read_sectors(volume, firstSector, clusterSize, buffer);
	return bytes_read;
}

/* get_next_cluster: Finds, in the file allocation table, the number
   of the cluster that follows the given cluster.
   
   Parameters:
     volume: pointer to FAT12 volume data structure.
     cluster: number of the cluster to seek.
   Returns:
     Number of the cluster that follows the given cluster (i.e., whose
     data is the sequence to the data of the current cluster). Returns
     0 if the given cluster is not in use, or a number larger than or
     equal to 0xff8 if the given cluster is the last cluster in a
     file.
 */
unsigned int get_next_cluster(fat12volume *volume, unsigned int cluster) {
	int even = cluster % 2 ==0;
	int position;
	if (even) {
		position = (cluster *3) / 2;
	}else if (cluster % 2 != 0) {		
		position = ((cluster - 1) * 3 ) / 2;
	}
	int bytes = read_unsigned_le(volume->fat_array, position, 3);
	return (cluster % 2 == 0) ? bytes % 4096 : bytes / 4096;
}

/* fill_directory_entry: Reads the directory entry from a
   FAT12-formatted directory and assigns its attributes to a dir_entry
   data structure.
   
   Parameters:
     data: pointer to the beginning of the directory entry in FAT12
           format. This function assumes that this pointer is at least
           DIR_ENTRY_SIZE long.
     entry: pointer to a dir_entry structure where the data will be
            stored.
 */
void fill_directory_entry(const char *data, dir_entry *entry) {

	char buffer[13];
	memcpy(buffer, data, 11);
	
	unsigned int date = read_unsigned_le(data, 24 , 2);
	unsigned int time = read_unsigned_le(data, 22 , 2);
	unsigned int directoryBit = read_unsigned_le(data, 11 , 1);
	directoryBit = !!(directoryBit & 0x10);
	entry->is_directory = directoryBit;


	stpcpy(entry->filename, buffer);
	
	entry->ctime.tm_hour   = (((1 << 5) - 1) & (time >> (16 - 5)));
	entry->ctime.tm_min    = (((1 << 6) - 1)  & (time >> (6 - 1)));
	entry->ctime.tm_sec    = 2* (((1 << 5) - 1) & (time));

	entry->ctime.tm_year   = (0x7f & (date >> 9)) +80;
	entry->ctime.tm_mon    =  0x0f & date >> (6 - 1);
	entry->ctime.tm_mday   = ((1 << 5) - 1) & (date);
	
	entry->ctime.tm_isdst	= -1;
	entry->size = read_unsigned_le(data, 28 , 4);
	entry->first_cluster = read_unsigned_le(data, 26 , 2);



  /* TO BE COMPLETED BY THE STUDENT */
  /* OBS: Note that the way that FAT12 represents a year is different
     than the way used by mktime and 'struct tm' to represent a
     year. In particular, both represent it as a number of years from
     a starting year, but the starting year is different between
     them. Make sure to take this into account when saving data into
     the entry. */
}

/* find_directory_entry: finds the directory entry associated to a
   specific path.
   
   Parameters:
     volume: Pointer to FAT12 volume data structure.
     path: Path of the file to be found. Will always start with a
           forward slash (/). Path components (e.g., subdirectories)
           will be delimited with "/". A path containing only "/"
           refers to the root directory of the FAT12 volume.
     entry: pointer to a dir_entry structure where the data associated
            to the path will be stored.
   Returns:
     In case of success (the provided path corresponds to a valid
     file/directory in the volume), the function will fill the data
     structure entry with the data associated to the path and return
     0. If the path is not a valid file/directory in the volume, the
     function will return -ENOENT, and the data in entry will be
     undefined. If the path contains a component (except the last one)
     that is not a directory, it will return -ENOTDIR, and the data in
     entry will be undefined.
 */

int find_directory_entry(fat12volume *volume, const char *path, dir_entry *entry) {

    // convert path to uppercase
    char* ts1 = strdup(path);
    char* token;
    char* buffer = NULL;

    // get the number of tokens in our path
    int token_count = count_tokens(path);
    int token_counter = 1;

    // get the first token
    token = strtok(ts1, "/");

    // create a temporary entry to hold our data
    dir_entry temp;

    while (token != NULL) {

        for (int i = 0; i < volume->rootdir_entries * 32; i = i + 32) { //looping through the directories
            fill_directory_entry((volume->rootdir_array) + i, &temp);
            char * name = temp.filename;
            
            // if the name is part of a file, we need to add a period
            if (temp.is_directory == 0) {
                add_period(name);
            }

            // remove excess whitespace from name
            remove_spaces(name);

            // remove extra whitespace
            remove_spaces(token);

            // if there's just the file in the root directory then we found it, return 0
            if(strcmp(name, token) == 0 && token_counter == token_count) {
                fill_directory_entry((volume->rootdir_array) + i, entry);
                stpcpy(entry->filename, name);
                return 0;
            }

            nextCluster:
            if(strcmp(name, token) == 0) { //found one that matches our directory entry
                // move the token forward
                token = strtok(NULL, "/");

                // increment the number of tokens
                token_counter++;

                // remove token whitespace
                remove_spaces(token);

                read_cluster(volume, temp.first_cluster, &buffer);

                // set parameters to store the number of times we've entered the while loop, next cluster number
                // and current cluster number
                int while_counter = 0;
                int next_cluster_number = 0;
                int curr_cluster_number = 0;

                while(1){ //enters while loop so that we can loop through the clusters until we run out

                    // use the first cluster we find in the first pass
                    if (while_counter == 0) {
                        curr_cluster_number = temp.first_cluster;
                    } else {
                        // subsequent passes uses the next cluster number
                        curr_cluster_number = next_cluster_number;
                    }

                    // increment the while loop counter
                    while_counter++;

                    //looping through the cluster
                    for (int i = 0; i < volume->cluster_size * volume->sector_size; i = i + 32) {
                        fill_directory_entry((buffer) + i, &temp);
                        char *name2 = temp.filename;

                        // add period to file
                        if (temp.is_directory == 0) {
                            add_period(name2);
                        }

                        // remove excess whitespace from name
                        remove_spaces(name2);

                        // if name matches and we've reached our token count, done
                        if (strcmp(token, name2) == 0 && token_counter == token_count) {
                            fill_directory_entry((buffer) + i, entry);
                            stpcpy(entry->filename, name2);
                            return 0;
                        }

                        // if name matches but token count hasn't been reached, re-loop to get a new directory cluster
                        if (strcmp(token, name2) == 0) {
                            goto nextCluster;
                        }
                    }

                    // retrieve the next cluster number from our current cluster number
                    next_cluster_number = get_next_cluster(volume, curr_cluster_number);
                    // if next cluster number is ff8, error
                    if (next_cluster_number == 4088) {
						if (token_counter == token_count){
							return -ENOENT;
						}else {
							return -ENOTDIR;
						}
					}
					// we need to keep searching through the next clusters if there's more clusters
                    read_cluster(volume, next_cluster_number, &buffer);
                    }
                }
            }
            // reached end of root directory, did not find file
            return -ENOENT;
        }
    // reached end of tokens, did not find file
    return -ENOENT;

}

