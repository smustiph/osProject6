
#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

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

int fs_format()
{
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

	// Set to 1 because we already read the super block
	int currblock;
	for(currblock = 1; currblock < block.super.nblocks; currblock++){
		// must check that data points to 4KB of memory
		disk_read(currblock, block.data);
		int currinode;
		for(currinode = 0; currinode < INODES_PER_BLOCK; currinode++){
			// check if the inode is actually created
			if(block.inode[currinode].isvalid == 1){
				printf("inode %d\n", currinode);
				printf("\tsize: %d bytes\n", block.inode[currinode].size);
				int size;
				size = block.inode[currinode].size;
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

int fs_mount()
{
	return 0;
}

int fs_create()
{
	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
