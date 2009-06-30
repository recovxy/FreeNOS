/*
 * Copyright (C) 2009 Niek Linnenbank
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <BitMap.h>
#include <List.h>
#include <String.h>
#include "Ext2SuperBlock.h"
#include "Ext2Inode.h"
#include "Ext2Group.h"
#include "Ext2FileSystem.h"
#include "Ext2Create.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

Ext2Create::Ext2Create()
{
    prog  = ZERO;
    image = ZERO;
    super = ZERO;
    blockSize    = EXT2_MIN_BLOCK_SIZE;
    totalInodes  = 1024 * 64;
    freeInodes   = totalInodes;
    totalBlocks  = 128;
    freeBlocks   = totalBlocks;
    fragmentSize = EXT2_MIN_FRAG_SIZE;
}

int Ext2Create::create()
{
    assert(image != ZERO);
    assert(prog != ZERO);

    /* Allocate and initialize the superblock. */
    super = initSuperBlock();
    
    /* Write the final image. */
    return writeImage();
}    

Ext2InputFile * Ext2Create::addInputFile(char *inputFile, Ext2InputFile *parent)
{
    Ext2InputFile *file;
    struct stat st;
    
    /* Stat the input file. */
    if (stat(inputFile, &st) != 0)
    {
	printf("%s: failed to stat() `%s': %s\r\n",
		prog, inputFile, strerror(errno));
	exit(EXIT_FAILURE);
    }
    /* Create new input file. */
    file = new Ext2InputFile;
    file->mode    = st.st_mode;
    file->size    = st.st_size;
    file->userId  = st.st_uid;
    file->groupId = st.st_gid;
    strncpy(file->name, inputFile, EXT2_NAME_LEN);
    file->name[EXT2_NAME_LEN - 1] = 0;
    
    /* Debug out. */
    printf("%s mode=%x size=%lu userId=%u groupId=%u\r\n",
	    file->name, file->mode, file->size,
	    file->userId, file->groupId);
    
    /* Add it to the parent, or make it root. */
    if (parent)
	parent->childs.insertTail(file);
    else
	inputRoot = file;
    
    /* All done. */
    return file;
}

int Ext2Create::readInput(char *directory, Ext2InputFile *parent)
{
    DIR *dir;
    Ext2InputFile *file;
    struct dirent *entry;
    char path[EXT2_NAME_LEN];
    
    /* Open the local directory. */
    if ((dir = opendir(directory)) == NULL)
    {
	return EXIT_FAILURE;
    }
    /* Add all entries. */
    while ((entry = readdir(dir)) != NULL)
    {
	/* Skip hidden. */
	if (entry->d_name[0] != '.')
	{
	    snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
	    file = addInputFile(path, parent);
	    
	    /* Traverse it in case of a directory. */
	    if (S_ISDIR(file->mode))
	    {
		readInput(path, file);
	    }
	}
    }
    /* Cleanup. */
    closedir(dir);

    /* Done. */
    return EXIT_SUCCESS;
}

int Ext2Create::writeImage()
{
    FILE *fp;

    assert(image != ZERO);
    assert(prog != ZERO);
    
    /* Construct the input root. */
    addInputFile(input, ZERO);

    /* Add the input directory contents. */
    readInput(input, inputRoot);

    /* Open output image file. */
    if ((fp = fopen(image, "w")) == NULL)
    {
	printf("%s: failed to fopen `%s': %s\r\n",
		prog, image, strerror(errno));
	return EXIT_FAILURE;
    }
    /* Seek to second block. */
    if (fseek(fp, blockSize, SEEK_SET) != 0)
    {
	printf("%s: failed to seek `%s' to %x: %s\r\n",
		prog, image, blockSize, strerror(errno));
	return EXIT_FAILURE;
    }
    /* Write superblock. */
    if (fwrite(super, sizeof(*super), 1, fp) != 1)
    {
	printf("%s: failed to fwrite `%s': %s\r\n",
		prog, image, strerror(errno));
	return EXIT_FAILURE;
    }
    /* Cleanup. */
    fclose(fp);
    delete super;
    
    /* All done. */
    return EXIT_SUCCESS;
}

void Ext2Create::setProgram(char *progName)
{
    this->prog = progName;
}

void Ext2Create::setImage(char *imageName)
{
    this->image = imageName;
}

void Ext2Create::setInput(char *inputName)
{
    this->input = inputName;
}

Ext2SuperBlock * Ext2Create::initSuperBlock()
{
    Ext2SuperBlock *sb = new Ext2SuperBlock;
    sb->inodesCount         = totalInodes;
    sb->blocksCount         = totalBlocks;
    sb->reservedBlocksCount = ZERO;
    sb->freeBlocksCount     = freeBlocks;
    sb->freeInodesCount     = freeInodes;
    sb->firstDataBlock      = 1;
    sb->log2BlockSize       = blockSize >> 10;
    sb->log2FragmentSize    = fragmentSize >> 10;
    sb->blocksPerGroup      = 256;
    sb->fragmentsPerGroup   = 128;
    sb->inodesPerGroup      = 2048;
    sb->mountTime	    = ZERO;
    sb->writeTime	    = ZERO;
    sb->mountCount	    = ZERO;
    sb->maximumMountCount   = 32;
    sb->magic               = EXT2_SUPER_MAGIC;
    sb->state               = EXT2_VALID_FS;
    sb->errors              = EXT2_ERRORS_CONTINUE;
    sb->minorRevision       = ZERO;
    sb->lastCheck           = ZERO;
    sb->checkInterval       = 3600 * 24 * 7;
    sb->creatorOS           = EXT2_OS_FREENOS;
    sb->majorRevision       = EXT2_CURRENT_REV;
    sb->defaultReservedUid  = ZERO;
    sb->defaultReservedGid  = ZERO;
    return sb;
}

int main(int argc, char **argv)
{
    Ext2Create fs;

    /* Verify command-line arguments. */
    if (argc < 3)
    {
	printf("usage: %s IMAGE DIRECTORY [OPTIONS...]\r\n"
	       "Creates a new Extended 2 FileSystem\r\n"
	       "\r\n"
	       "-h          Show this help message.\r\n"
	       "-e PATTERN  Exclude matching files from the created filesystem\r\n"
	       "-b SIZE     Block size.\r\n"
	       "-n INODES   Number of inodes.\r\n",
		argv[0]);
	return EXIT_FAILURE;
    }
    /* Process command-line arguments. */
    fs.setProgram(argv[0]);
    fs.setImage(argv[1]);
    fs.setInput(argv[2]);
    
    /* Create a new Extended 2 FileSystem. */
    return fs.create();
}