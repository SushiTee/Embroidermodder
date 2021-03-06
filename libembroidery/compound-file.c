#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "compound-file.h"
#include "compound-file-difat.h"
#include "compound-file-header.h"
#include "compound-file-fat.h"
#include "compound-file-common.h"

static unsigned int sizeOfDirectoryEntry = 128;

static unsigned int sectorSize(bcf_file *bcfFile)
{
    /* version 3 uses 512 byte */
    if(bcfFile->header.majorVersion == 3)
    {
        return 512;
    }
    else if(bcfFile->header.majorVersion == 4)
    {
        return 4096;
    }
    return 4096;
}

static int haveExtraDIFATSectors(bcf_file *file)
{
    return (int)(numberOfEntriesInDifatSector(file->difat) > 0);
}

static int seekToOffset(FILE *file, const unsigned int offset)
{
    return fseek(file, offset, SEEK_SET);
}

static int seekToSector(bcf_file *bcfFile, FILE *file, const unsigned int sector)
{
    unsigned int offset = sector * sectorSize(bcfFile) + sectorSize(bcfFile);
    return seekToOffset(file, offset);
}

static void parseDIFATSectors(FILE *file, bcf_file *bcfFile)
{
    unsigned int numberOfDifatEntriesStillToRead = bcfFile->header.numberOfFATSectors - NumberOfDifatEntriesInHeader;
    unsigned int difatSectorNumber  = bcfFile->header.firstDifatSectorLocation;
    while((difatSectorNumber != CompoundFileSector_EndOfChain) &&
          (numberOfDifatEntriesStillToRead > 0 ))
    {
        seekToSector(bcfFile, file, difatSectorNumber);
        difatSectorNumber = readFullSector(file, bcfFile->difat, &numberOfDifatEntriesStillToRead);
    }
}

int bcfFile_read(FILE *file, bcf_file *bcfFile)
{
    unsigned int i, numberOfDirectoryEntriesPerSector, directorySectorToReadFrom;

    bcfFile->header = bcfFileHeader_read(file);
    if ( !bcfFileHeader_isValid(bcfFile->header) ) {
        printf( "Failed to parse header\n" );
        return 0;
    }

    bcfFile->difat = bcf_difat_create(file, bcfFile->header.numberOfFATSectors, sectorSize(bcfFile));
    if (haveExtraDIFATSectors(bcfFile))
    {
        parseDIFATSectors(file, bcfFile);
    }

    bcfFile->fat = bcfFileFat_create(sectorSize(bcfFile));
    for (i = 0; i < bcfFile->header.numberOfFATSectors; ++i) {
        unsigned int fatSectorNumber = bcfFile->difat->fatSectorEntries[i];
        seekToSector(bcfFile, file, fatSectorNumber);
        loadFatFromSector(bcfFile->fat, file);
    }

    numberOfDirectoryEntriesPerSector = sectorSize(bcfFile) / sizeOfDirectoryEntry;
    bcfFile->directory = CompoundFileDirectory(numberOfDirectoryEntriesPerSector);
    directorySectorToReadFrom = bcfFile->header.firstDirectorySectorLocation;
    while ( directorySectorToReadFrom != CompoundFileSector_EndOfChain )
   {
        seekToSector(bcfFile, file, directorySectorToReadFrom );
        readNextSector(file, bcfFile->directory);
        directorySectorToReadFrom = bcfFile->fat->fatEntries[directorySectorToReadFrom];
    }

    return 1;
}

FILE *GetFile(bcf_file *bcfFile, FILE *file, char *fileToFind)
{
    int filesize, sectorSize, currentSector, sizeToWrite, currentSize, totalSectors, i;
    char *input;
    FILE *fileOut = tmpfile();
    bcf_directory_entry *pointer = bcfFile->directory->dirEntries;
    while(pointer)
    {
        if(strcmp(fileToFind, pointer->directoryEntryName) == 0)
        break;
        pointer = pointer->next;
    }
    filesize = pointer->streamSize;
    sectorSize = bcfFile->difat->sectorSize;
    input = (char *)malloc(sectorSize);
    currentSize = 0;
    currentSector = pointer->startingSectorLocation;
    totalSectors = (int)ceil((float)filesize / sectorSize);
    for(i = 0; i < totalSectors; i++)
    {
        seekToSector(bcfFile, file, currentSector);
        sizeToWrite = filesize - currentSize;
        if(sectorSize < sizeToWrite)
        {
            sizeToWrite = sectorSize;
        }
        fread(input, 1, sizeToWrite, file);
        fwrite(input, 1, sizeToWrite, fileOut);
        currentSize += sizeToWrite;
        currentSector = bcfFile->fat->fatEntries[currentSector];
    }
    free(input);
    return fileOut;
}

void bcf_file_free(bcf_file *bcfFile)
{
    bcf_file_difat_free(bcfFile->difat);
    bcf_file_fat_free(bcfFile->fat);
    bcf_directory_free(bcfFile->directory);
    free(bcfFile);
}

/* kate: bom off; indent-mode cstyle; indent-width 4; replace-trailing-space-save on; */
