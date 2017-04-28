#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410 // lets know that there is a file system
#define INODES_PER_BLOCK   128 // inodes per block
#define POINTERS_PER_INODE 5 // number of direct pointers in inode
#define POINTERS_PER_BLOCK 1024 // number of pointers to be found in an indirect block

/*
	Questions for Jermaine:
	1. How do we test to make sure things are working the way we think they are, few examples, have him mess with it
		- check debug, do we need a check to see if direct block array exists
	2. Check over format: are we doing this right?
		- think it works, run debug then format then debug again to check
		- Specifically destroying all of the data
		- run ./getfiles.sh to get the new disk images from the web
	3. Check over mount:
		- are we creating the free block bitmap correctly
			1 for in use 0 for empty
		- what does it mean by prepare the file system?
			we have the super block which designates the given blocks and numbers so what are we in charge of doing?
	4. Check over create:
		- Think it works the way we want it to

*/

// Globals
int ismounted =  0;
int *freeblockbitmap;

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

/*
	Creates a new filesystem on the disk, destroying any data already present. 
	Sets aside ten percent of the blocks for inodes, clears the inode table, and writes the superblock. 
	Returns one on success, zero otherwise. 
	Note that formatting a filesystem does not cause it to be mounted. 
	Also, an attempt to format an already-mounted disk should do nothing and return failure.
*/
int fs_format()
{
	if(ismounted == 0){
		union fs_block oldBlock;
		disk_read(0,oldBlock.data);

		if(oldBlock.super.magic == FS_MAGIC){
			int currblock;
			for(currblock = 1; currblock < oldBlock.super.nblocks; currblock++){
				disk_read(currblock, oldBlock.data);
				int currinode;
				// change all of the inodes to not created destroying the data
				for(currinode = 0; currinode < INODES_PER_BLOCK; currinode++){
					oldBlock.inode[currinode].isvalid = 0;
					int currinodeblock;
					// destroying direct data blocks
					for(currinodeblock = 0; currinodeblock < POINTERS_PER_INODE; currinodeblock++){
						oldBlock.inode[currinode].direct[currinodeblock] = 0;
					}
					// deleting indirect data blocks
					if(oldBlock.inode[currinode].indirect > 0){
						oldBlock.inode[currinode].indirect = 0;
						union fs_block indirectblock;
						disk_read(oldBlock.inode[currinode].indirect, indirectblock.data);
						int currpointer;
						for(currpointer = 0; currpointer < POINTERS_PER_BLOCK; currpointer++){
							indirectblock.pointers[currpointer] = 0;
						}
					}
				}
				disk_write(currblock, oldBlock.data);
			}
		}

		int numBlocks = disk_size();
		int percentage = numBlocks/10; 

		union fs_block newBlock;

		newBlock.super.magic = FS_MAGIC;
		newBlock.super.nblocks = numBlocks;
		newBlock.super.ninodeblocks = percentage;
		newBlock.super.ninodes = percentage*INODES_PER_BLOCK;

		// write the superblock to disk, will be the initial block
		disk_write(0, newBlock.data);

		// on success return 1
		return 1;
	}
	else if(ismounted == 1){
		printf("Failure: disk already mounted\n");
		exit(1);
	}
	return 0;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data);

	printf("superblock:\n");
	printf("\t%d blocks\n",block.super.nblocks);
	printf("\t%d inode blocks\n",block.super.ninodeblocks);
	printf("\t%d inodes\n",block.super.ninodes);

	if(block.super.magic == FS_MAGIC){
		// Set to 1 because we already read the super block
		int currblock;
		for(currblock = 1; currblock < block.super.nblocks; currblock++){
			// must check that data points to 4KB of memory
			disk_read(currblock, block.data);
			int currinode;
			for(currinode = 1; currinode < INODES_PER_BLOCK; currinode++){
				// check if the inode is actually created
				if(block.inode[currinode].isvalid == 1){
					printf("inode: %d\n", currinode);
					printf("\tsize: %d bytes\n", block.inode[currinode].size);
					int size;
					size = block.inode[currinode].size;
					// place check here to see if the array is empty
					printf("\tdirect blocks: ");
					int currinodeblock;
					for(currinodeblock = 0; currinodeblock < POINTERS_PER_INODE; currinodeblock++){
						if(block.inode[currinode].direct[currinodeblock] == 0){
							continue;
						}
						printf("%d ", block.inode[currinode].direct[currinodeblock]);
					}
					printf("\n");
					if(block.inode[currinode].indirect > 0){
						printf("\tindirect block: %d\n", block.inode[currinode].indirect);
						printf("\tindirect data blocks: ");
						union fs_block indirectblock;
						disk_read(block.inode[currinode].indirect, indirectblock.data);
						int currpointer;
						for(currpointer = 0; currpointer < POINTERS_PER_BLOCK; currpointer++){
							if(indirectblock.pointers[currpointer] == 0){
								continue;
							}
							printf("%d ", indirectblock.pointers[currpointer]);
						}
						printf("\n");
					}
				}
			}
		}
	}	
}

int fs_mount()
{
	union fs_block block;

	disk_read(0, block.data);

	// check if the filesystem is present
	if(block.super.magic == FS_MAGIC){
		freeblockbitmap = malloc(sizeof(int *)*block.super.ninodes);
		// go through each inode block and check every inode 1 if in use 0 otherwise
		// if data block is 0 then it is not being used, anything else and it is being used
		int currblock;
		for(currblock = 1; currblock < block.super.nblocks; currblock++){
			disk_read(currblock, block.data);
			int currinode;
			for(currinode = 1; currinode < INODES_PER_BLOCK; currinode++){
				// check if inode is actually created
				if(block.inode[currinode].isvalid == 1){
					int currinodeblock;
					for(currinodeblock = 0; currinodeblock < POINTERS_PER_INODE; currinodeblock++){
						// if the current data block is not being used 
						if(block.inode[currinode].direct[currinodeblock] == 0){
							freeblockbitmap[block.inode[currinode].direct[currinodeblock]] = 0;
							continue;
						}
						freeblockbitmap[block.inode[currinode].direct[currinodeblock]] = 1;
					}
				}
			}
		}

		ismounted = 1;
		return ismounted;
	}
	return 0;
}

// to run from here on out you must first mount the disk
int fs_create()
{
	// check to see if it ismounted
	if(ismounted==1){
		union fs_block block;
		disk_read(0,block.data);
		if(block.super.magic == FS_MAGIC){
			int currblock;
			for(currblock = 1; currblock < block.super.nblocks; currblock++){
				disk_read(currblock, block.data);
				int currinode;
				for(currinode = 1; currinode < INODES_PER_BLOCK; currinode++){
					// inode already created
					if(block.inode[currinode].isvalid == 1){
						continue;
					}
					// inode not created, so create it
					block.inode[currinode].isvalid = 1;
					block.inode[currinode].size = 0; // set the length to be 0
					disk_write(currblock, block.data);
					return currinode;
				}
			}
		}
	}
	return 0;
}


int fs_delete( int inumber )
{
	if(ismounted == 1){
		union fs_block block;
		disk_read(0,block.data);
		if(block.super.magic == FS_MAGIC){
			int currblock;
			for(currblock = 1; currblock < block.super.nblocks; currblock++){
				disk_read(currblock, block.data);
				int currinode;
				for(currinode = 1; currinode < INODES_PER_BLOCK; currinode++){
					// check if inumber is same as the given inode
					if(currinode == inumber){
						// check if valid 
						if(block.inode[currinode].isvalid == 1){
							block.inode[currinode].isvalid = 0;
							int currinodeblock;
							for(currinodeblock = 0; currinodeblock < POINTERS_PER_INODE; currinodeblock++){
								block.inode[currinode].direct[currinodeblock] = 0; // free the direct block
								freeblockbitmap[block.inode[currinode].direct[currinodeblock]] = 0; // free bitmap
							}
							// free the indirect blocks
							// deleting indirect data blocks
							if(block.inode[currinode].indirect > 0){
								block.inode[currinode].indirect = 0;
								union fs_block indirectblock;
								disk_read(block.inode[currinode].indirect, indirectblock.data);
								int currpointer;
								for(currpointer = 0; currpointer < POINTERS_PER_BLOCK; currpointer++){
									indirectblock.pointers[currpointer] = 0;
								}
							}
						}
					}
				}
				disk_write(currblock, block.data);
				return 1;
			}
		}
	}
	// check to see if it ismounted
	return 0;
}

int fs_getsize( int inumber )
{
	// check to if ismounted
	if(ismounted == 1){
		union fs_block block;
		disk_read(0, block.data);
		if(block.super.magic == FS_MAGIC){
			int currblock;
			for(currblock = 1; currblock < block.super.nblocks; currblock++){
				disk_read(currblock, block.data);
				int currinode;
				for(currinode = 0; currinode < INODES_PER_BLOCK; currinode++){
					// check that current inode numbeer is equal to our inumber
					if(currinode == inumber){
						if(block.inode[currinode].isvalid == 1){
							return block.inode[currinode].size;
						}
					}
				}
			}
		}
	}
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	int totbytes = 0;
	if(ismounted == 1){
		union fs_block block;
		disk_read(0, block.data);
		if(block.super.magic == FS_MAGIC){
			int currblock;
			for(currblock = 1; currblock < block.super.nblocks; currblock++){
				disk_read(currblock, block.data);
				int currinode;
				for(currinode = 0; currinode < INODES_PER_BLOCK; currinode++){
					if(currinode == inumber){
						if(block.inode[currinode].isvalid == 1){
							int currinodeblock;
							for(currinodeblock = 0; currinodeblock < POINTERS_PER_INODE; currinodeblock++){
								union fs_block bufferBlock;
								disk_read(block.inode[currinode].direct[currinodeblock], bufferBlock.data);
								int currbyte;
								for(currbyte = offset; currbyte < 1000; currbyte++){
									data[totbytes] = bufferBlock.data[currbyte];
									totbytes+=1;
								}
							}
						}
					}
				}
			}
		}
		return totbytes;

	}
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
