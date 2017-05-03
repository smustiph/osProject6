
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

int *bitmap;
int sizeofBitmap;
int *inodeTable;
int totalinodes;
int mounted = 0;   // 1 if mounted, 0 if not yet mounted

int fs_format()
{
    union fs_block block;
    int i, j;
    
    disk_read(0, block.data);

    // check if the disk has been mounted  
    if (mounted) {
        return 0;
    };

    // write the superblock
    block.super.magic = FS_MAGIC;
    block.super.nblocks = disk_size();
    block.super.ninodeblocks = (int)((block.super.nblocks*0.1)+1);
    block.super.ninodes = block.super.ninodeblocks*INODES_PER_BLOCK;
    disk_write(0, block.data);

    // clear inode table
    // inodeTable = (int) malloc(block.super.ninodeblocks*INODES_PER_BLOCK*sizeof(int));

    // create new file system and destroy present data by making inodes invalid
    int ninodeblocks = block.super.ninodeblocks;
    for (i=1; i<=ninodeblocks; i++) {
        disk_read(i, block.data);
        for (j=0; j<INODES_PER_BLOCK; j++) {
            block.inode[j].isvalid = 0;
        }
        disk_write(i, block.data);
    }

	return 1;
}

void fs_debug()
{
	union fs_block block;

	disk_read(0,block.data); 

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

    int ninodeblocks = block.super.ninodeblocks;
    
    int i, j, k, l;
    for (i=1; i<=ninodeblocks; i++) {
        for (j=0; j<INODES_PER_BLOCK; j++) {
            disk_read(i, block.data);
            
            // Check if the block is valid and is not empty
            if (block.inode[j].isvalid) {
                printf("inode %d:\n", j);
                printf("    size: %d bytes\n", block.inode[j].size);
                printf("    direct blocks:");
                
                // Print the direct blocks in the valid inodes
                for(k = 0; k < POINTERS_PER_INODE; k++){
                    if(block.inode[j].direct[k]){
                        printf(" %d",block.inode[j].direct[k]);
                    }
                }
                printf("\n");
                
                // Print the indirect blocks in the valid inodes if available
                if(block.inode[j].indirect){
                    printf("    indirect block: %d\n", block.inode[j].indirect);  
                    disk_read(block.inode[j].indirect, block.data);
                    printf("    indirect data blocks:");
                    for (l=0; l<POINTERS_PER_BLOCK; l++) {
                        if(block.pointers[l]){
                            printf(" %d", block.pointers[l]);
                        }
                    }
                    printf("\n");
                }
            }
        }
    }
}

// initially mounts the disk
int fs_mount()
{
    // if disk was already mounted, return with error
    if (mounted == 1) {
        return 0;
    }
	
    int i, j, k, l, m;
    union fs_block block;
    disk_read(0, block.data);

    // initialize and fill free block bitmap 
    sizeofBitmap = block.super.nblocks;
    bitmap = malloc(sizeofBitmap*sizeof(int));
    for (m=0; m<sizeofBitmap; m++) {
        bitmap[m] = 0;
    }
    
    // also initialize and fill inode table
    totalinodes = (block.super.ninodeblocks*INODES_PER_BLOCK);
    inodeTable = malloc(totalinodes*sizeof(int));


    for (m=0; m<totalinodes; m++) {

        inodeTable[m] = 0;
    }

    // set 0th item in bitmap to 1 because it is used as the super block
    bitmap[0] = 1;

    int ninodeblocks = block.super.ninodeblocks;
    
    for (i=1; i<=ninodeblocks; i++) {
        bitmap[i] = 1;
        for (j=0; j<INODES_PER_BLOCK; j++) {
            disk_read(i, block.data);
            
            // Check if the block is valid and is not empty
            if (block.inode[j].isvalid) {
                
                inodeTable[((i-1)*INODES_PER_BLOCK) + j] = 1;
                
                // Print the direct blocks in the valid inodes
                for(k = 0; k < POINTERS_PER_INODE; k++){
                    if(block.inode[j].direct[k]){
                        bitmap[block.inode[j].direct[k]] = 1;
                    }
                }
                
                // Print the indirect blocks in the valid inodes if available
                if(block.inode[j].indirect){
                    bitmap[block.inode[j].indirect] = 1;
                    disk_read(block.inode[j].indirect, block.data);
                    for (l=0; l<POINTERS_PER_BLOCK; l++) {
                        if(block.pointers[l]){
                            bitmap[block.pointers[l]] = 1;
                        }
                    }
                }
            }
        }
    }
    
    /*for (m=0; m<totalinodes; m++) {
        if (inodeTable[m]) {
            printf("%d\n", m);
        }
    }*/
    mounted = 1;
	return 1;
}

// loads the data from the inode at number inumber
void inode_load(int inumber, struct fs_inode *inode) {
    
    union fs_block block;
    disk_read((int) (inumber/INODES_PER_BLOCK) + 1, block.data);

    *inode = block.inode[inumber%INODES_PER_BLOCK];
}

// saves the data in the passed in inode to the inode with the provided inumber
void inode_save(int inumber, struct fs_inode *inode) {
    
    union fs_block block;
    disk_read((int) (inumber/INODES_PER_BLOCK) + 1, block.data);


    block.inode[inumber%INODES_PER_BLOCK] = *inode;
    disk_write((int) (inumber/INODES_PER_BLOCK) + 1, block.data);

}

// creates a new inode of 0 length. On success, return inumber, otherwise return 0
int fs_create()
{
    int i, j;
    struct fs_inode inode;

    if (!mounted) {
        printf("Not yet mounted!\n");
        return 0;
    }

    // search for an empty inode 
    // 0th inumber is disregarded because we cannot return 0 unless failure happens
    for (i=1; i<totalinodes; i++) {
        if (inodeTable[i]==0) {
            inodeTable[i] = 1;
            inode_load(i, &inode);
            inode.isvalid=1;
            inode.size=0;
            for (j = 0; j < POINTERS_PER_INODE; j++) {
                inode.direct[j] = 0;
            }
            inode.indirect=0;
            inode_save(i, &inode);
            return i; 
        }
    }

    // if inodeTable is full, if so then return fail
	return 0;
}

// delete the inode inidcate by inumber. release all data and indirect blocks assigned to this inode and return them to free block bitmap. on success, return 1, else 0
int fs_delete( int inumber )
{
    int i;
    union fs_block block;
    struct fs_inode inode;  
    
    if (!mounted) {
        printf("Not yet mounted!\n");
        return 0;
    }

    disk_read(0, block.data);
    
    if(inumber > block.super.ninodes || inumber < 0){
        return 0;
    }

    inode_load(inumber, &inode);

    if (inode.isvalid == 0) {
        return 1;
    }

    inode.isvalid = 0;
    inode.size = 0;

    inode_save(inumber, &inode);

    // Go through the direct pointers and free them from the bitmap
    for (i=0; i<POINTERS_PER_INODE; i++) {
        if (inode.direct[i]){
            bitmap[inode.direct[i]] = 0;
        }
    }
    // Go through the indirect pointers and free them from the bitmap
    if(inode.indirect){
        disk_read(inode.indirect, block.data);
        for(i = 0; i < POINTERS_PER_BLOCK; i++){
            if(block.pointers[i]){
                bitmap[block.pointers[i]] = 0;
            }
        }
    }
    return 1;
}

int fs_getsize( int inumber )
{
    if (!mounted) {
        printf("Not yet mounted!\n");
        return 0;
    }

    union fs_block block;
    struct fs_inode inode;

    disk_read(0, block.data);
    
    if(inumber > block.super.ninodes || inumber < 0){
        return -1;
    }

    inode_load(inumber, &inode);
    if(inode.isvalid){
        return inode.size;
    }
	return -1;
}

// reads length bits of data from a valid inode by starting at offset in the inode
// returns the number of bytes read
int fs_read( int inumber, char *data, int length, int offset )
{
    if (!mounted) {
        printf("Not yet mounted!\n");
        return 0;
    }

    union fs_block block;
    struct fs_inode inode;

    disk_read(0, block.data);
    
    if(inumber > block.super.ninodes || inumber < 0){
        return 0;
    }
    totalinodes = (block.super.ninodeblocks*INODES_PER_BLOCK);
 
    int i, j;
    int currbyte = 0, first = 0;
    inode_load(inumber, &inode);
    if (inode.isvalid == 0) {
        return 0;
    }
    if(offset >= inode.size){
        return 0;
    }

    int startBlock = (int)(offset/DISK_BLOCK_SIZE);
    int curroffset = offset%4096;
    for(i = startBlock; i < POINTERS_PER_INODE; i++){
        if(inode.direct[i]){
            if (first == 0) {
                disk_read(inode.direct[i], block.data);
                for(j = 0; j+curroffset < DISK_BLOCK_SIZE; j++){
                    if(block.data[j+curroffset]){
                        data[currbyte] = block.data[j+curroffset];
                        currbyte++;
                        if(currbyte+offset >= inode.size){
                            return currbyte;
                        }
                    }
                    else{
                        return currbyte;
                    }
                    if (currbyte == length){
                        return currbyte;
                    }
                } 
                first = 1;
            }
            else{
                disk_read(inode.direct[i], block.data);
                for(j = 0; j < DISK_BLOCK_SIZE; j++){
                    if(block.data[j]){
                        data[currbyte] = block.data[j];
                        currbyte++;
                        if(currbyte+offset >= inode.size){
                            return currbyte;
                        }

                    }
                    else{
                        return currbyte;
                    }
                    if (currbyte == length){
                        return currbyte;
                    }
                } 
            }
        }
    }

    //Indirect nodes here
    union fs_block indirectBlock;
    printf("%d\n", startBlock);
    int startIndirect = startBlock - 5;
    if(inode.indirect){
        disk_read(inode.indirect, indirectBlock.data);
        for(i = startIndirect; i < POINTERS_PER_BLOCK; i++){
            if (indirectBlock.pointers[i]){
                disk_read(indirectBlock.pointers[i], block.data);
                for(j = 0; j < DISK_BLOCK_SIZE; j++){
                    if(block.data[j]){
                        data[currbyte] = block.data[j];
                        currbyte++;
                        if(currbyte+offset >= inode.size){
                            return currbyte;
                        }

                    }
                    else{
                        return currbyte;
                    }
                    if (currbyte == length){
                        return currbyte;
                    }
                } 
            }
        }
    }
    return currbyte;
}

// write to valid inode 
// copy length bytes from pointer data into the inode starting at offset bytes
// return num bytes written 
int fs_write( int inumber, const char *data, int length, int offset )
{
    if (!mounted) {
        printf("Not yet mounted!\n");
        return 0;
    }

    union fs_block block;
    struct fs_inode inode;
    
    // read the superblock and do checks
    disk_read(0, block.data);
    
    if(inumber > block.super.ninodes || inumber < 0){
        return 0;
    }
    totalinodes = (block.super.ninodeblocks*INODES_PER_BLOCK);
    int nonDiskBlocks = (block.super.ninodeblocks+1);


    int i, j, k;
    int currbyte = 0, first = 0;
    inode_load(inumber, &inode);
    if (inode.isvalid == 0) {
        return 0;
    }
    if (inode.isvalid == 1 && inode.size > 0){
        for (i = 0; i < POINTERS_PER_INODE; i++){
            if(inode.direct[i]){
                bitmap[inode.direct[i]] = 0;
            }
        }
        if(inode.indirect){
            disk_read(inode.indirect, block.data);
            bitmap[inode.indirect] = 0;
            for(j = 0; j < POINTERS_PER_BLOCK; j++){
                if(block.pointers[j]){
                    bitmap[block.pointers[j]] = 0;
                }
            }
        }
    }

    int startBlock = (int)(offset/DISK_BLOCK_SIZE);
    int curroffset = offset%4096;
    for(i = startBlock; i < POINTERS_PER_INODE; i++){
        // go through bitmap to look for empty block
        for (k = nonDiskBlocks; k < sizeofBitmap; k++) {
            if (bitmap[k]==0) {
                inode.direct[i] = k;
                bitmap[k] = 1;
                break;
            }
        }
        if (first == 0) {
            disk_read(inode.direct[i], block.data);
            for(j = 0; j+curroffset < DISK_BLOCK_SIZE; j++){
                block.data[j+curroffset] = data[currbyte];
                currbyte++;
                if (currbyte == length){
                    disk_write(inode.direct[i], block.data);
                    inode.size = currbyte + offset;
                    inode_save(inumber, &inode);
                    return currbyte;
                }
            } 
            first = 1;
            disk_write(inode.direct[i], block.data);
        }
        else{
            disk_read(inode.direct[i], block.data);
            for(j = 0; j < DISK_BLOCK_SIZE; j++){
                block.data[j] = data[currbyte]; 
                currbyte++;
                if (currbyte == length){
                    disk_write(inode.direct[i], block.data);
                    inode.size = currbyte + offset;
                    inode_save(inumber, &inode);
                    return currbyte;
                }
            } 
            disk_write(inode.direct[i], block.data);
        }
    }

    //Indirect nodes here
    union fs_block indirectBlock;
    // go through bitmap to look for empty block
    for (k = nonDiskBlocks; k < sizeofBitmap; k++) {
        if (!bitmap[k]) {
            inode.indirect = k;
            bitmap[k] = 1;
            break;
        }
    }

    int startIndirect = startBlock - 5;

    if(inode.indirect){
        disk_read(inode.indirect, indirectBlock.data);
        for(i = startIndirect; i < POINTERS_PER_BLOCK; i++){
            // go through bitmap to look for empty block
            for (k = nonDiskBlocks; k < sizeofBitmap; k++) {
                if (!bitmap[k]) {
                    indirectBlock.pointers[i] = k;
                    bitmap[k] = 1;
                    break;
                }
            }

            disk_read(indirectBlock.pointers[i], block.data);
            for(j = 0; j < DISK_BLOCK_SIZE; j++){
                block.data[j] = data[currbyte]; 
                currbyte++;
                if (currbyte == length){
                    disk_write(indirectBlock.pointers[i], block.data);
                    inode.size = currbyte + offset;
                    inode_save(inumber, &inode);
                    return currbyte;
                }
            } 
            disk_write(indirectBlock.pointers[i], block.data);
        }
        disk_write(inode.indirect, indirectBlock.data);
    }

    // update inode's size
    inode.size = currbyte + offset;
    inode_save(inumber, &inode);
    return currbyte;
}

