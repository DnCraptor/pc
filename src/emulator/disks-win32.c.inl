#pragma once

#include "emulator.h"

int hdcount = 0, fdcount = 0;

static uint8_t sectorbuffer[512];
typedef struct _IO_FILE FILE;
typedef unsigned long DWORD;

extern FILE *fopen(const char *pathname, const char *mode);
extern int fclose(FILE *stream);
extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern int	fflush (FILE *);
extern int fseek(FILE *stream, long offset, int whence);
extern long ftell(FILE *stream);
extern void rewind(FILE *stream);


#define SEEK_CUR    1
#define SEEK_END    2
#define SEEK_SET    0

struct struct_drive {
    FILE *diskfile;
    size_t filesize;
    uint16_t cyls;
    uint16_t sects;
    uint16_t heads;
    uint8_t inserted;
    uint8_t readonly;
} disk[4];


static inline void ejectdisk(uint8_t drivenum) {
    if (drivenum & 0x80) drivenum -= 126;

    if (disk[drivenum].inserted) {
        disk[drivenum].inserted = 0;
        if (drivenum >= 0x80)
            hdcount--;
        else
            fdcount--;
    }
}

uint8_t insertdisk(uint8_t drivenum, const char *pathname) {
    if (drivenum & 0x80) drivenum -= 126;  // Normalize hard drive numbers

    FILE *file = fopen(pathname, "rb+");
    if (!file) {
        printf( "DISK: ERROR: cannot open disk file %s for drive %02Xh\n", pathname, drivenum);
        return 0;
    }

    // Find the file size
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    // Validate size constraints
    if (size < 360 * 1024 || size > 0x1f782000UL || (size & 511)) {
        fclose(file);
//        fprintf(stderr, "DISK: ERROR: invalid disk size for drive %02Xh (%lu bytes)\n", drivenum, (unsigned long) size);
        return 0;
    }

    // Determine geometry (cyls, heads, sects)
    uint16_t cyls = 0, heads = 0, sects = 0;

    if (drivenum >= 2) {  // Hard disk
        sects = 63;
        heads = 16;
        cyls = size / (sects * heads * 512);
    } else {  // Floppy disk
        cyls = 80;
        sects = 18;
        heads = 2;

        if (size <= 368640) {  // 360 KB or lower
            cyls = 40;
            sects = 9;
            heads = 2;
        } else if (size <= 737280) {
            sects = 9;
        } else if (size <= 1228800) {
            sects = 15;
        }
    }

    // Validate geometry
    if (cyls > 1023 || cyls * heads * sects * 512 != size) {
//        fclose(file);
//        fprintf(stderr, "DISK: ERROR: Cannot determine correct CHS geometry for drive %02Xh\n", drivenum);
//        return 0;
    }

    // Eject any existing disk and insert the new one
    ejectdisk(drivenum);

    disk[drivenum].diskfile = file;
    disk[drivenum].filesize = size;
    disk[drivenum].inserted = 1;  // Using 1 instead of true for consistency with uint8_t
    disk[drivenum].readonly = 0;  // Default to read-write
    disk[drivenum].cyls = cyls;
    disk[drivenum].heads = heads;
    disk[drivenum].sects = sects;

    // Update drive counts
    if (drivenum >= 2) {
        hdcount++;
    } else {
        fdcount++;
    }

//    printf("DISK: Disk %02Xh attached from file %s, size=%luK, CHS=%d,%d,%d\n",
//           drivenum, pathname, (unsigned long) (size >> 10), cyls, heads, sects);

    return 1;
}

// Call this ONLY if all parameters are valid! There is no check here!
static inline size_t chs2ofs(int drivenum, int cyl, int head, int sect) {
    return (
                   ((size_t)cyl * (size_t)disk[drivenum].heads + (size_t)head) * (size_t)disk[drivenum].sects + (size_t) sect - 1
           ) * 512UL;
}


static void readdisk(uint8_t drivenum,
              uint16_t dstseg, uint16_t dstoff,
              uint16_t cyl, uint16_t sect, uint16_t head,
              uint16_t sectcount, int is_verify
) {
    uint32_t memdest = ((uint32_t) dstseg << 4) + (uint32_t) dstoff;
    uint32_t cursect = 0;

    // Check if disk is inserted
    if (!disk[drivenum].inserted) {
//        printf("no media %i\r\n", drivenum);
        CPU_AH = 0x31;    // no media in drive
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Check if CHS parameters are valid
    if (sect == 0 || sect > disk[drivenum].sects || cyl >= disk[drivenum].cyls || head >= disk[drivenum].heads) {
//        printf("sector not found\r\n");
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Convert CHS to file offset
    size_t fileoffset = chs2ofs(drivenum, cyl, head, sect);

    // Check if fileoffset is valid
    if (fileoffset > disk[drivenum].filesize) {
//        printf("sector not found\r\n");
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Set file position
    fseek(disk[drivenum].diskfile, fileoffset, SEEK_SET);

    // Process sectors
    for (cursect = 0; cursect < sectcount; cursect++) {
        // Read the sector into buffer
        if (fread(&sectorbuffer[0], 512, 1, disk[drivenum].diskfile) != 1) {
//            printf("Disk read error on drive %i\r\n", drivenum);
            CPU_AH = 0x04;    // sector not found
            CPU_AL = 0;
            cf = 1;
            return;
        }

        if (is_verify) {
            for (int sectoffset = 0; sectoffset < 512; sectoffset++) {
                // Verify sector data
                if (read86(memdest++) != sectorbuffer[sectoffset]) {
                    // Sector verify failed
                    CPU_AL = cursect;
                    cf = 1;
                    CPU_AH = 0xBB;    // sector verify failed error code
                    return;
                }
            }
        } else {
            for (int sectoffset = 0; sectoffset < 512; sectoffset++) {
                // Write sector data
                write86(memdest++, sectorbuffer[sectoffset]);
            }
        }

        // Update file offset for next sector
        fileoffset += 512;
    }

    // If no sectors could be read, handle the error
    if (cursect == 0) {
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Set success flags
    CPU_AL = cursect;
    cf = 0;
    CPU_AH = 0;
}

static void writedisk(uint8_t drivenum,
               uint16_t dstseg, uint16_t dstoff,
               uint16_t cyl, uint16_t sect, uint16_t head,
               uint16_t sectcount
) {
    uint32_t memdest = ((uint32_t) dstseg << 4) + (uint32_t) dstoff;
    uint32_t cursect;

    // Check if disk is inserted
    if (!disk[drivenum].inserted) {
        CPU_AH = 0x31;    // no media in drive
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Convert CHS to file offset
    size_t fileoffset = chs2ofs(drivenum, cyl, head, sect);

    // check if sector can be found
    if (
            ((sect == 0 || sect > disk[drivenum].sects || cyl >= disk[drivenum].cyls || head >= disk[drivenum].heads))
            || fileoffset > disk[drivenum].filesize
            || disk[drivenum].filesize < fileoffset
            ) {
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Check if drive is read-only
    if (disk[drivenum].readonly) {
        CPU_AH = 0x03;    // drive is read-only
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Set file position
    fseek(disk[drivenum].diskfile, fileoffset, SEEK_SET);


    // Write each sector
    for (cursect = 0; cursect < sectcount; cursect++) {
        // Read from memory and store in sector buffer
        for (int sectoffset = 0; sectoffset < 512; sectoffset++) {
            // FIXME: segment overflow condition?
            sectorbuffer[sectoffset] = read86(memdest++);
        }

        // Write the buffer to the file
        fwrite(sectorbuffer, 512, 1, disk[drivenum].diskfile);
    }

    // Handle the case where no sectors were written
    if (sectcount && cursect == 0) {
        CPU_AH = 0x04;    // sector not found
        CPU_AL = 0;
        cf = 1;
        return;
    }

    // Set success flags
    CPU_AL = cursect;
    cf = 0;
    CPU_AH = 0;
}


static INLINE void diskhandler() {
    static uint8_t lastdiskah[4] = { 0 }, lastdiskcf[4] = { 0 };
    uint8_t drivenum = CPU_DL;

    // Normalize drivenum for hard drives
    if (drivenum & 0x80) drivenum -= 126;

    // Handle the interrupt service based on the function requested in AH
    switch (CPU_AH) {
        case 0x00:  // Reset disk system
            if (disk[drivenum].inserted) {
                CPU_AH = 0;
                cf = 0;  // Successful reset (no-op in emulator)
            } else {

                cf = 1;  // Disk not inserted
            }
            break;

        case 0x01:  // Return last status
            CPU_AH = lastdiskah[drivenum];
            cf = lastdiskcf[drivenum];
//            printf("disk not inserted %i", drivenum);
            return;

        case 0x02:  // Read sector(s) into memory
            readdisk(drivenum, CPU_ES, CPU_BX,
                     CPU_CH + (CPU_CL / 64) * 256,  // Cylinder
                     CPU_CL & 63,                    // Sector
                     CPU_DH,                         // Head
                     CPU_AL,                         // Sector count
                     0);                             // Read operation
            break;

        case 0x03:  // Write sector(s) from memory
            writedisk(drivenum, CPU_ES, CPU_BX,
                      CPU_CH + (CPU_CL / 64) * 256,  // Cylinder
                      CPU_CL & 63,                   // Sector
                      CPU_DH,                        // Head
                      CPU_AL);                       // Sector count
            break;

        case 0x04:  // Verify sectors
            readdisk(drivenum, CPU_ES, CPU_BX,
                     CPU_CH + (CPU_CL / 64) * 256,   // Cylinder
                     CPU_CL & 63,                    // Sector
                     CPU_DH,                         // Head
                     CPU_AL,                         // Sector count
                     1);                             // Verify operation
            break;

        case 0x05:  // Format track
            cf = 0;  // Success (no-op for emulator)
            CPU_AH = 0;
            break;

        case 0x08:  // Get drive parameters
            if (disk[drivenum].inserted) {
                cf = 0;
                CPU_AH = 0;
                CPU_CH = disk[drivenum].cyls - 1;
                CPU_CL = (disk[drivenum].sects & 63) + ((disk[drivenum].cyls / 256) * 64);
                CPU_DH = disk[drivenum].heads - 1;

                // Set DL and BL for floppy or hard drive
                if (CPU_DL < 2) {
                    CPU_BL = 4;  // Floppy
                    CPU_DL = 2;
                } else {
                    CPU_DL = hdcount;  // Hard disk
                }
            } else {
                cf = 1;
                CPU_AH = 0xAA;  // Error code for no disk inserted
            }
            break;

        default:  // Unknown function requested
            cf = 1;  // Error
            break;
    }

    // Update last disk status
    lastdiskah[drivenum] = CPU_AH;
    lastdiskcf[drivenum] = cf;

    // Set the last status in BIOS Data Area (for hard drives)
    if (CPU_DL & 0x80) {
        RAM[0x474] = CPU_AH;
    }
}


