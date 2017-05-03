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
						union fs_block indirectblock;
						disk_read(oldBlock.inode[currinode].indirect, indirectblock.data);
						oldBlock.inode[currinode].indirect = 0;
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
	}
	else if(ismounted == 1){
		printf("Disk already mounted\n");
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
		freeblockbitmap = calloc(block.super.nblocks, sizeof(int));
		// go through each inode block and check every inode 1 if in use 0 otherwise
		// if data block is 0 then it is not being used, anything else and it is being used
		int currblock;
		for(currblock = 1; currblock <= block.super.ninodeblocks; currblock++){
			union fs_block tempBlock;
			// printf("blocks: %d\n",  block.super.ninodeblocks);
			// printf("InodeBlock = %d\n", currblock);
			freeblockbitmap[currblock] = 1;
			disk_read(currblock, tempBlock.data);
			int currinode;
			for(currinode = 1; currinode < INODES_PER_BLOCK; currinode++){
				// check if inode is actually created
				if(tempBlock.inode[currinode].isvalid == 1){
					int currinodeblock;
					for(currinodeblock = 0; currinodeblock < POINTERS_PER_INODE; currinodeblock++){
						// if the current data block is not being used 
						if(tempBlock.inode[currinode].direct[currinodeblock] == 0){
							freeblockbitmap[tempBlock.inode[currinode].direct[currinodeblock]] = 0;
							continue;
						}
						freeblockbitmap[tempBlock.inode[currinode].direct[currinodeblock]] = 1;
					}
					if(tempBlock.inode[currinode].indirect > 0){
						union fs_block indirectblock;
						freeblockbitmap[tempBlock.inode[currinode].indirect] = 1;
						disk_read(tempBlock.inode[currinode].indirect, indirectblock.data);
						int currpointer;
						for(currpointer = 0; currpointer < POINTERS_PER_BLOCK; currpointer++){
							if(indirectblock.pointers[currpointer] == 0){
								freeblockbitmap[indirectblock.pointers[currpointer]] = 0;
								continue;
							}
							freeblockbitmap[indirectblock.pointers[currpointer]] = 1;
						}
					}
				}
			}
		// printf("currblock = %d ninodeblocks = %d\n", currblock, block.super.ninodeblocks);
		}
	}
	ismounted = 1;
	/*
	int i;
	for(i = 0; i < block.super.nblocks; i++){
		printf("Inodenumber %d: %d\n", i,freeblockbitmap[i]);
	}*/
	return ismounted;
}

// to run from here on out you must first mount the disk
int fs_create()
{
	// check to see if it ismounted
	if(ismounted){
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
					/* zeroing out direct blocks and indirect blocks */
					int directblock;
					for(directblock = 0; directblock < POINTERS_PER_INODE; directblock++){
						block.inode[currinode].direct[directblock] = 0;
					}
					block.inode[currinode].indirect = 0;
					disk_write(currblock, block.data);
					return currinode;
				}
			}
		}
	}
	else{
		printf("Error: Disk not mounted\n");
	}
	return 0;
}


int fs_delete( int inumber )
{
	if(ismounted){
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
								freeblockbitmap[block.inode[currinode].direct[currinodeblock]] = 0; // free bitmap
								block.inode[currinode].direct[currinodeblock] = 0; // free the direct block
							}
							// free the indirect blocks
							// deleting indirect data blocks
							if(block.inode[currinode].indirect > 0){
								union fs_block indirectblock;
								disk_read(block.inode[currinode].indirect, indirectblock.data);
								// block.inode[currinode].indirect = 0;
								int currpointer;
								for(currpointer = 0; currpointer < POINTERS_PER_BLOCK; currpointer++){
									freeblockbitmap[indirectblock.pointers[currpointer]] = 0;
									indirectblock.pointers[currpointer] = 0;
								}
								disk_write(block.inode[currinode].indirect, indirectblock.data);
								block.inode[currinode].indirect = 0;
							}
						}
					}
				}
				disk_write(currblock, block.data);
				return 1;
			}
		}
	}
	else{
		printf("Error: Disk not mounted\n");
	}
	// check to see if it ismounted
	return 0;
}

int fs_getsize( int inumber )
{
	// check to if ismounted
	if(ismounted){
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
	else{
		printf("Error: disk not mounted\n");
	}
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	if(ismounted){
		// overall inode
		struct fs_inode masterinode;

		// resetting the data
		data[0] = '\0';

		/* Loading the iNode */
		int inodeblock = inumber/INODES_PER_BLOCK + 1;
		int inodenum = inumber%INODES_PER_BLOCK;
		// printf("%d\n", inodenum);
		union fs_block tempBlock;
		disk_read(inodeblock, tempBlock.data);
		// printf("Disk read done\n");
		memcpy(&masterinode, &tempBlock.inode[inodenum], sizeof(struct fs_inode));
		// printf("Mem copy done\n");

		// check if the offset is greater than the size of the inode if so then break
		if(offset > masterinode.size){
			return 0;
		}
		// printf("past check\n");

		/* Computing Length of Bytes We are Going to Read */
		int actlength;
		// iNode size greater than what we want to read
		if(length + offset > masterinode.size){
			actlength = masterinode.size - offset; 
		}
		else{
			actlength = length; 
		}
		int bytesleft = actlength;

		// check if offset greater than size of master inode
		int currblock = offset / DISK_BLOCK_SIZE; // current block
		int curroffset = offset % DISK_BLOCK_SIZE; // offset within the given block
		int currblocknum; // block number that is pointed to

		// loop through the data
		while(bytesleft > 0){
			if(currblock > POINTERS_PER_INODE + POINTERS_PER_BLOCK){
				break;
			}
			if(strlen(data) > length){
				return 0;
			}
			if(currblock >= POINTERS_PER_INODE){
				// printf("in if\n");
				/* taking care of indirect block */
				int currindirectblock = masterinode.indirect;
				union fs_block indirectBufferBlock;
				disk_read(currindirectblock, indirectBufferBlock.data);
				/* find the offset */
				currblocknum = indirectBufferBlock.pointers[currblock - POINTERS_PER_INODE];
			}
			else{
				currblocknum = masterinode.direct[currblock];
			}

			/* Reading Data */
			// printf("reading data\n");
			union fs_block bufferBlock;
			int bufferLength = DISK_BLOCK_SIZE - curroffset;
			int lengthToCopy;
			if(bytesleft > bufferLength){
				lengthToCopy = bufferLength;
			}
			else{
				lengthToCopy = bytesleft;
			}
			if(strlen(data) + lengthToCopy == length){
				lengthToCopy-=1;
			}
			// printf("before diskread\n");
			disk_read(currblocknum, bufferBlock.data);
			// printf("before strncat\n");
			// printf("%d %d", curroffset, lengthToCopy);
			// printf("Sum: %d\n", (curroffset + lengthToCopy));
			// printf("poop\n");
			// printf("LENGTH TO COPY %d", lengthToCopy);
			// printf("%lu %lu %d", strlen(data), strlen(bufferBlock.data + curroffset), lengthToCopy);
			strncat(data, bufferBlock.data + curroffset, lengthToCopy);
			// printf("after strncat\n");
			if(lengthToCopy < bufferLength){
				bytesleft = 0;
			}
			else{
				bytesleft = bytesleft - bufferLength;
			}
			// printf("past getting bytesleft\n");

			curroffset = 0;
			currblock+=1;
		}
		return actlength - bytesleft;
	}
	else{
		printf("Error: disk not mounted\n");
	}
	return 0;
}

int findfreeblock(){
	union fs_block block;
	disk_read(0, block.data);
	int i;
	for(i = 1; i < block.super.nblocks; i++){
		if(freeblockbitmap[i] == 0){
			// printf("free = %d\n", i);
			/* updating the bitmap */
			freeblockbitmap[i] = 1;
			return i;
		}
	}
	/* no free block found */
	return -1;
}

int findfreeindirectblock(struct fs_inode *inode){
	int currblocknum = findfreeblock();
	if(currblocknum == -1){
		return -1;
	}
	inode->indirect = currblocknum;
	union fs_block block;
	disk_read(inode->indirect, block.data);
	int currpointer;
	for(currpointer = 0; currpointer < POINTERS_PER_BLOCK; currpointer++){
		/* make sure no garbage values contained */
		block.pointers[currpointer] = 0;
	}
	disk_write(inode->indirect, block.data);
	return inode->indirect;
}

int fs_write( int inumber, const char *data, int length, int offset )
{	
	if(ismounted){
		// overall inode
		struct fs_inode masterinode;

		/* Loading the iNode */
		int inodeblock = inumber/INODES_PER_BLOCK + 1;
		int inodenum = inumber%INODES_PER_BLOCK;
		// printf("%d\n", inodenum);
		union fs_block tempBlock;
		disk_read(inodeblock, tempBlock.data);
		// printf("Disk read done\n");
		memcpy(&masterinode, &tempBlock.inode[inodenum], sizeof(struct fs_inode));
		// printf("Mem copy done\n");

		// check if the offset is greater than the size of the inode if so then break
		if(offset > masterinode.size){
			return 0;
		}
		// printf("past check\n");

		/* Computing Length of Bytes We are Going to Read */
		int actlength;
		// iNode size greater than what we want to read
		if(length + offset > masterinode.size){
			actlength = masterinode.size - offset;
			// printf("actlength1: %d\n", masterinode.size-offset);
		}
		else{
			actlength = length; 
			// printf("actlength2: %d\n", length);
		}
		int bytesleft = length;

		// check if offset greater than size of master inode
		int currblock = offset / DISK_BLOCK_SIZE; // current block
		int curroffset = offset % DISK_BLOCK_SIZE; // offset within the given block
		int currblocknum; // block number that is pointed to

		int currindirectblock;

		while(bytesleft > 0){
			if(currblock > POINTERS_PER_INODE + POINTERS_PER_BLOCK){
				break;
			}/*
			if(strlen(data) > length){
				return 0;
			}*/
			if(currblock >= POINTERS_PER_INODE){
				// printf("in if\n");
				/* taking care of indirect block */
				if(masterinode.indirect == 0){
					currindirectblock = findfreeindirectblock(&masterinode);
				}
				else{
					currindirectblock = masterinode.indirect;
				}
				if(currindirectblock == -1){
					printf("No free indirect blocks\n");
					return 0;
				}

				union fs_block indirectBufferBlock;
				disk_read(currindirectblock, indirectBufferBlock.data);
				/* find the offset */
				currblocknum = indirectBufferBlock.pointers[currblock - POINTERS_PER_INODE];

				/* check for free block for indirect*/
				if(currblocknum == 0){
					currblocknum = findfreeblock();
					if(currblocknum != -1){
						indirectBufferBlock.pointers[currblock - POINTERS_PER_INODE] = currblocknum;
						disk_write(currindirectblock, indirectBufferBlock.data);
					}
				}
			}
			/* Direct Blocks */
			else{
				currblocknum = masterinode.direct[currblock];
				/* check for free block */
				if(currblocknum == 0){
					currblocknum = findfreeblock();
					if(currblocknum != -1){
						masterinode.direct[currblock] = currblocknum;
					}
				}
				// currblocknum = masterinode.direct[currblock];
			}
			if(currblocknum == -1){
				printf("Error: No Valid Block Available\n");
				exit(1);
			}

			/* Reading Data */
			// printf("reading data\n");
			union fs_block bufferBlock;
			int bufferLength = DISK_BLOCK_SIZE - curroffset;
			int lengthToCopy;
			if(bytesleft > bufferLength){
				lengthToCopy = bufferLength;
			}
			else{
				lengthToCopy = bytesleft;
			}
			if(strlen(data) + lengthToCopy == length){
				lengthToCopy-=1;
			}
			// printf("before diskread\n");
			disk_read(currblocknum, bufferBlock.data);
			// printf("lengthToCopy: %d\n", lengthToCopy);
			strncpy(bufferBlock.data + curroffset, data, lengthToCopy);
			data+=lengthToCopy;
			// printf("Currblocknum: %d\n", currblocknum);
			disk_write(currblocknum, bufferBlock.data);

			// printf("after strncat\n");
			if(lengthToCopy < bufferLength){
				bytesleft = 0;
			}
			else{
				bytesleft = bytesleft - bufferLength;
			}
			// printf("past getting bytesleft\n");

			curroffset = 0;
			currblock+=1;
		}
		// end of loop should be here
		masterinode.size += length - bytesleft;
		disk_read(inodeblock, tempBlock.data);
		// printf("Disk read done\n");
		memcpy(&tempBlock.inode[inodenum], &masterinode, sizeof(struct fs_inode));
		disk_write(inodeblock, tempBlock.data);
		return length - bytesleft;
	}
	else{
		printf("Error Disk not Mounted\n");
	}
	return 0;
}
