/*
 * PlayStation 2 Master Disc Boot Patcher by Bucanero
 * --------------------------------------------------
 *
 * this tool is based on the notes and sample source code about the PS2 boot sectors by loser:
 * https://github.com/mlafeldt/ps2logo/blob/master/Documentation/ps2boot.txt
 *
 * The code to regenerate EDC/ECC for CD images is based on the PSXtract implementation:
 * https://github.com/xdotnano/PSXtract/blob/master/Windows/cdrom.cpp
 *
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "wildcard.h"
#include "lzari.h"
#include "cdrom.h"

#include "logo_ntsc.h"
#include "logo_pal.h"


#pragma pack(push, 1)
typedef struct {
    // Header section
    char disc_name[32];
    char producer_name[32];
    char copyright_holder[32];
    uint32_t year_bcd;
    uint16_t month_bcd;
    uint16_t day_bcd;
    char master_disc_text[24];  // "PlayStation Master Disc "
    uint8_t playstation_version;
    uint8_t region;
    uint8_t reserved1;
    uint8_t disc_type;
    
    union {
        // CD format
        struct {
            char cd_filler[124];
        } cd;
        
        // DVD format
        struct {
            uint8_t dvd_byte1;
            uint8_t dvd_byte2;
            uint32_t sector_count_adjusted;
            uint32_t dvd_reserved;
            char dvd_filler[114];
        } dvd;
    } media_specific;
    
    // Common section
    uint8_t common_byte1;
    uint64_t common_ff;
    uint32_t magic2_first;
    uint8_t magic1_first;
    uint16_t reserved2;
    uint8_t section1_byte1;
    uint32_t section1_value1;
    uint32_t section1_value2;
    uint32_t magic2_second;
    uint8_t magic1_second;
    uint16_t reserved3;
    uint8_t section2_byte1;
    uint32_t section2_value1;
    uint32_t section2_value2;
    uint32_t zeros1;
    uint8_t magic3;
    uint16_t reserved4;
    uint32_t zeros2;
    uint32_t zeros3;
    uint32_t zeros4;
    uint32_t zeros5;
    char zeros_large[448];
    char spaces1[48];
    char cdvdgen_text[16];
    char spaces2[1216];
} MasterDiscSector;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    // Disc identification section
    char disc_name[32];           // Offset 0
    char producer_name[32];       // Offset 32
    char copyright_holder[32];    // Offset 64
    
    // Date information (BCD format)
    uint32_t year_bcd;            // Offset 96
    uint16_t month_bcd;           // Offset 100
    uint16_t day_bcd;             // Offset 102
    
    // Master disc identifier
    char master_disc[24];         // Offset 104
    
    // System information
    uint8_t playstation_version;  // Offset 128
    uint8_t region;               // Offset 129
    uint8_t reserved1;            // Offset 130
    uint8_t disc_type;            // Offset 131
    
    // Disc-specific data (union for CD/DVD)
    union {
        struct {
            char cd_filler[124]; // Offset 132
        } cd;
        struct {
            uint8_t dvd_byte1; // Offset 132
            uint8_t dvd_byte2; // Offset 133
            uint32_t sector_count_adjusted; // Offset 134
            uint32_t dvd_reserved; // Offset 138
            char dvd_filler[114];   // Offset 142
        } dvd;
    } media_specific;
    
    // Various system fields
    uint8_t common_byte1;         // Offset 256
    uint64_t common_ff1;          // Offset 257
    uint32_t magic2_first;        // Offset 265
    uint8_t magic1_first;         // Offset 269
    uint16_t field_270;           // Offset 270
    uint8_t field_272;            // Offset 272
    uint64_t common_ff2;          // Offset 273
    uint32_t field_281;           // Offset 281
    uint8_t field_285;            // Offset 285
    uint16_t field_286;           // Offset 286
    uint8_t field_288;            // Offset 288
    uint32_t field_289;           // Offset 289
    uint32_t field_293;           // Offset 293
    uint32_t magic2_second;       // Offset 297
    uint8_t magic1_second;        // Offset 301
    uint16_t field_302;           // Offset 302
    uint8_t field_304;            // Offset 304
    uint32_t field_305;           // Offset 305
    uint32_t field_309;           // Offset 309
    uint32_t field_313;           // Offset 313
    uint8_t magic3;               // Offset 317
    uint8_t field_318;            // Offset 318
    uint8_t field_319;            // Offset 319
    
    // Padding sections
    uint8_t zeros_large[448];     // Offset 320
    char spaces1[48];             // Offset 768
    char cdvdgen_version[16];     // Offset 816
    char spaces2[1216];           // Offset 828
} JapanMasterDiscSector;
#pragma pack(pop)

enum {
    REGION_NONE = 0x00,
    REGION_JAPAN = 0x01,
    REGION_USA = 0x02,
    REGION_EUROPE = 0x04,
    REGION_WORLD = 0x07,
};

enum {
    DISC_NONE = 0,
    DISC_CD   = 1,
    DISC_DVD  = 2,
};

// The notes mention BCD format for date fields, but samples show a different encoding (text)
uint16_t int_to_bcd(int value)
{
    return (((value % 10) + 0x30) << 8) | ((value / 10) + 0x30);
//    return ((value / 10) << 4) | (value % 10);
}

// Helper function to pad string with spaces
void pad_string(char *dest, const char *src, size_t length)
{
    strncpy(dest, src, length);
    for (size_t i = strlen(src); i < length; i++) {
        dest[i] = ' ';
    }
}

/* This is the basic CRC-32 calculation with some optimization but no table lookup. */
uint32_t crc32b(uint8_t *data, size_t size)
{
	uint32_t crc = 0xFFFFFFFF;

	while (size--){
		crc ^= *data++;          // XOR with next byte.
		for (int j=0; j<8; j++){     // Do eight times.
			crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
		}
	}
	return ~crc;
}

bool fixMode2Form1Sector(FILE* inputfile, int sector_index)
{
    //Sync pattern
    unsigned char sync[SYNC_SIZE] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    unsigned char sector[SECTOR_SIZE];

    //Read sector
    fseek(inputfile, sector_index * SECTOR_SIZE, SEEK_SET);
    if(fread(sector, 1, SECTOR_SIZE, inputfile) != SECTOR_SIZE)
        return false;

    //Find mode
    unsigned char minutes = sector[HEADER_OFFSET + 0];
    unsigned char seconds = sector[HEADER_OFFSET + 1];
    unsigned char blocks  = sector[HEADER_OFFSET + 2];
    unsigned char mode    = sector[HEADER_OFFSET + 3];
    if (mode != MODE_2)
    {
        printf("Error: Only CD-XA Mode 2 sectors are supported. Found mode %d sector at %02X:%02X:%02X\n", mode, minutes, seconds, blocks);
        return false;
    }

    //Process sector based on mode 2 form 1
    //Write sync field
    memcpy(sector, sync, sizeof(sync));

    //Read subheader
    unsigned char submode           = sector[CDROMXA_SUBHEADER_OFFSET + 2];

    //Check that the two copies of the subheader data are equivalent
    if(memcmp(&sector[CDROMXA_SUBHEADER_OFFSET + 0], &sector[CDROMXA_SUBHEADER_OFFSET + 4], 4) != 0)
    {
        printf("[!] Warning: CD-ROM XA subheader mismatch at %02X:%02X:%02X\n", minutes, seconds, blocks);
    }

    //Determine CD ROM XA Mode 2 form
    if ((submode & 0x20) == 0x20)
    {
        printf("[!] Error: Only CD-XA Mode 2 Form 1 sectors are supported. Found CD-XA Mode 2 Form 2 sector at %02X:%02X:%02X\n", minutes, seconds, blocks);
        return false;
    }

    //Compute and write EDC
    //Compute form 1 EDC
    unsigned int EDC = 0x00000000;
    for(int i = CDROMXA_SUBHEADER_OFFSET; i < CDROMXA_FORM1_EDC_OFFSET; ++i)
    {
        EDC = EDC ^ sector[i];
        EDC = (EDC >> 8) ^ EDCTable[EDC & 0x000000FF];
    }

    //Write EDC
    sector[CDROMXA_FORM1_EDC_OFFSET + 0] = (EDC & 0x000000FF) >> 0;
    sector[CDROMXA_FORM1_EDC_OFFSET + 1] = (EDC & 0x0000FF00) >> 8;
    sector[CDROMXA_FORM1_EDC_OFFSET + 2] = (EDC & 0x00FF0000) >> 16;
    sector[CDROMXA_FORM1_EDC_OFFSET + 3] = (EDC & 0xFF000000) >> 24;

    //Write error-correction data

    //Temporarily clear header
    memset(&sector[HEADER_OFFSET], 0, 4);

    //Calculate P parity
    unsigned char* src = sector + HEADER_OFFSET;
    unsigned char* dst = sector + CDROMXA_FORM1_PARITY_P_OFFSET;
    for(int i = 0; i < 43; ++i)
    {
        unsigned short x = 0x0000;
        unsigned short y = 0x0000;
        for(int j = 19; j < 43; ++j)
        {
            x ^= RSPCTable[j][src[0]]; //LSB
            y ^= RSPCTable[j][src[1]]; //MSB
            src += 2 * 43;
        }
        dst[         0] = x >> 8;
        dst[2 * 43 + 0] = x & 0xFF;
        dst[         1] = y >> 8;
        dst[2 * 43 + 1] = y & 0xFF;
        dst += 2;
        src -= (43 - 19) * 2 * 43; //Restore src to the state before the inner loop
        src += 2;
    }

    //Calculate Q parity
    src = sector + HEADER_OFFSET;
    dst = sector + CDROMXA_FORM1_PARITY_Q_OFFSET;
    unsigned char* src_end = sector + CDROMXA_FORM1_PARITY_Q_OFFSET;
    for(int i = 0; i < 26; ++i)
    {
        unsigned char* src_backup = src;
        unsigned short x = 0x0000;
        unsigned short y = 0x0000;
        for(int j = 0; j < 43; ++j)
        {
            x ^= RSPCTable[j][src[0]]; //LSB
            y ^= RSPCTable[j][src[1]]; //MSB
            src += 2 * 44;
            if(src >= src_end)
            {
                src = src - (HEADER_SIZE + CDROMXA_SUBHEADER_SIZE + CDROMXA_FORM1_USER_DATA_SIZE + EDC_SIZE + CDROMXA_FORM1_PARITY_P_SIZE);
            }
        }

        dst[         0] = x >> 8;
        dst[2 * 26 + 0] = x & 0xFF;
        dst[         1] = y >> 8;
        dst[2 * 26 + 1] = y & 0xFF;
        dst += 2;
        src = src_backup;
        src += 2 * 43;
    }

    //Restore header
    sector[HEADER_OFFSET + 0] = minutes;
    sector[HEADER_OFFSET + 1] = seconds;
    sector[HEADER_OFFSET + 2] = blocks;
    sector[HEADER_OFFSET + 3] = mode;

    fseek(inputfile, -SECTOR_SIZE, SEEK_CUR);
    //Write fixed sector to output file
    if(fwrite(sector, 1, SECTOR_SIZE, inputfile) != SECTOR_SIZE)
        return false;

    //Signal successful operation and return status
    return true;
}

///////////////////////////////////////////////////////////
// this calculates the 3 magic numbers mentioned above
// 
// args:    discNameLetters: 4 letters from the discname (eg SLES)
//              (the letters must be between A and Z, capital letters only)
//          discNameNumbers: the disc number (eg 12345)
//              (the disc number must be between 0 and 99999)
//          magic1: placeholder for magic number 1
//          magic2: placeholder for magic number 2
//          magic3: placeholder for magic number 3
// returns: true  if ok
//          false if error
bool calcMagicNums(const char discNameLetters[4], int discNameNumbers, unsigned char *magic1, unsigned int *magic2, unsigned char *magic3)
{
    unsigned int letters=0;
    unsigned int numbers=0;

    // check discname letters to make sure they are valid
    for(int i=0; i<4; i++)
        if(discNameLetters[i] < 'A' || discNameLetters[i] > 'Z')
            return false;
    // check discname numbers to make sure they are valid
    if(discNameNumbers < 0 || discNameNumbers > 99999)
        return false;
    
    // make letters fit into a single u_int
    letters =   (unsigned int)(discNameLetters[3]<< 0) | (unsigned int)(discNameLetters[2]<< 7) |
                (unsigned int)(discNameLetters[1]<<14) | (unsigned int)(discNameLetters[0]<<21);
    // number already fits into a u_int
    numbers = discNameNumbers;

    // calculate magic numbers
    *magic1 = ((numbers &  0x1F) <<  3) | ((0x0FFFFFFF & letters) >> 25);
    *magic2 = ( numbers          >> 10) | ((0x0FFFFFFF & letters) <<  7);
    *magic3 = ((numbers & 0x3E0) >>  2) | 0x04;
    
    return true;
}

///////////////////////////////////////////////////////////
// encrypts the raw ps2 logo
//
// args:    logo: pointer to raw logo in memory (12*2048bytes)
//          discNameLetters: 4 letters from the discname (eg SLES)
//              (the letters must be between A and Z, capital letters only)
//          discNameNumbers: the disc number (eg 12345)
//              (the disc number must be between 0 and 99999)
// returns: true  if ok
//          false if error
bool EncryptLogo(unsigned char *logo, char discNameLetters[4], int discNameNumbers)
{
    unsigned char magicNum=0, magic3=0;
    unsigned int i;
    
    // calculate the magic number needed for XORing
    if(!calcMagicNums(discNameLetters, discNameNumbers, &magicNum, &i, &magic3))
        return false;
    
    // individually encrypt each pixel in the logo
    // (even the extra bytes at the end of the pal logo)
    for(i=0; i<12*2048; i++)
        logo[i] = ((logo[i]<<5)|(logo[i]>>3)) ^ magicNum;

    return true;
}

///////////////////////////////////////////////////////////
// decrypts the raw ps2 logo
// 
// args:    logo: pointer to raw logo in memory (12*2048bytes)
//          discNameLetters: 4 letters from the discname (eg SLES)
//              (the letters must be between A and Z, capital letters only)
//          discNameNumbers: the disc number (eg 12345)
//              (the disc number must be between 0 and 99999)
// returns: true  if ok
//          false if error
bool DecryptLogo(unsigned char *logo, const char discNameLetters[4], int discNameNumbers)
{
    unsigned char magicNum=0, magic3=0;
    unsigned int i;
    
    // calculate the magic number needed for XORing
    if(!calcMagicNums(discNameLetters, discNameNumbers, &magicNum, &i, &magic3))
        return false;
    
    // individually decrypt each pixel in the logo
    // (even the extra bytes at the end of the pal logo)
    for(i=0; i<12*2048; i++)
        logo[i] = ((logo[i]^magicNum)>>5)|((logo[i]^magicNum)<<3);
    
    return true;
}

int write_master_disc_sector(FILE *file, 
                            const char *disc_name, int disc_id,
                            const char *producer_name,
                            const char *copyright_holder,
                            int year, int month, int day,
                            uint8_t region,
                            uint8_t disc_type,
                            uint32_t num_image_sectors,
                            const char *cdvdgen_version)
{
    char tmp_formatted[16];
    MasterDiscSector sector;
    
    if (!file) {
        perror("Failed to open file");
        return -1;
    }
    
    memset(&sector, 0, sizeof(sector));

    // Magic numbers
    if (!calcMagicNums(disc_name, disc_id,  &sector.magic1_first, &sector.magic2_first, &sector.magic3))
    {
        fprintf(stderr, "Error calculating magic numbers\n");
        fclose(file);
        return -1;
    }
    sector.magic1_second = sector.magic1_first;
    sector.magic2_second = sector.magic2_first;
    
    // Fill basic information
    snprintf(tmp_formatted, sizeof(tmp_formatted), "%s-%05d", disc_name, disc_id);
    pad_string(sector.disc_name, tmp_formatted, sizeof(sector.disc_name));
    pad_string(sector.producer_name, producer_name, sizeof(sector.producer_name));
    pad_string(sector.copyright_holder, copyright_holder, sizeof(sector.copyright_holder));
    
    sector.year_bcd = int_to_bcd(year % 100) << 16 | int_to_bcd(year / 100);
    sector.month_bcd = int_to_bcd(month);
    sector.day_bcd = int_to_bcd(day);
    
    memcpy(sector.master_disc_text, "PlayStation Master Disc ", sizeof(sector.master_disc_text));
    sector.playstation_version = 0x32;
    sector.region = region;
    sector.reserved1 = 0x00;
    sector.disc_type = disc_type;
    
    // Handle media-specific section
    if (disc_type == DISC_CD)
    { // CD
        memset(sector.media_specific.cd.cd_filler, ' ', sizeof(sector.media_specific.cd.cd_filler));
    } else if (disc_type == DISC_DVD)
    { // DVD
        sector.media_specific.dvd.dvd_byte1 = 0x01;
        sector.media_specific.dvd.dvd_byte2 = 0x00;
        sector.media_specific.dvd.sector_count_adjusted = ((((num_image_sectors + 15) / 16) * 16) - 1);
        sector.media_specific.dvd.dvd_reserved = 0x00000000;
        memset(sector.media_specific.dvd.dvd_filler, ' ', sizeof(sector.media_specific.dvd.dvd_filler));
    }
    
    // Common section
    sector.common_byte1 = 0x01;
    sector.common_ff = 0xFFFFFFFFFFFFFFFF;
    
    sector.reserved2 = 0x0000;
    sector.section1_byte1 = 0x01;
    sector.section1_value1 = 0x0000004B;
    sector.section1_value2 = 0x0000104A;
        
    sector.reserved3 = 0x0000;
    sector.section2_byte1 = 0x03;
    sector.section2_value1 = 0x0000004B;
    sector.section2_value2 = 0x0000104A;
    
    sector.zeros1 = 0x00000000;
    sector.reserved4 = 0x0000;
    sector.zeros2 = 0x00000000;
    sector.zeros3 = 0x00000000;
    sector.zeros4 = 0x00000000;
    sector.zeros5 = 0x00000000;
    
    memset(sector.zeros_large, 0x00, sizeof(sector.zeros_large));
    memset(sector.spaces1, ' ', sizeof(sector.spaces1));
    
    // Format CDVDGEN text
    snprintf(tmp_formatted, sizeof(tmp_formatted), "CDVDGEN %s", cdvdgen_version);
    pad_string(sector.cdvdgen_text, tmp_formatted, sizeof(sector.cdvdgen_text));
    
    memset(sector.spaces2, ' ', sizeof(sector.spaces2));

    if (region == 1) // Japan region
    {
        JapanMasterDiscSector *header = (JapanMasterDiscSector *)&sector;

        header->magic3 = sector.magic3;
        header->magic2_second = sector.magic2_first;
        header->magic1_second = sector.magic1_first;

        header->reserved1 = 0x30;
        header->field_270 = 0x0000;
        header->field_272 = 0x02;
        header->common_ff2 = 0xFFFFFFFFFFFFFFFF;
        header->field_281 = 0x00000000;
        header->field_285 = 0x80;
        header->field_286 = 0x0000;
        header->field_288 = 0x01;
        header->field_289 = 0x0000004B;
        header->field_293 = 0x0000104A;
        header->field_302 = 0x0000;
        header->field_304 = 0x03;
        header->field_305 = 0x0000004B;
        header->field_309 = 0x0000104A;
        header->field_313 = 0x00000000;
        header->field_318 = 0x00;
        header->field_319 = 0x80;
    }

    // Write the sector twice as specified
    for (int i = 0; i < 2; i++)
    {
        if (disc_type == DISC_CD) // CD-XA Mode 2
            fseek(file, 0x18, SEEK_CUR);

        if (fwrite(&sector, sizeof(sector), 1, file) != 1) {
            perror("Failed to write sector");
            fclose(file);
            return -1;
        }

        // Update CD sector EDC/ECC data
        if (disc_type == DISC_CD)
            fixMode2Form1Sector(file, 14 + i);
    }
    
    return 0;
}

void usage(const char* app_bin)
{
    puts("This program accepts PS2 DVD (.ISO) and PS2 CD (.BIN) images\n");
    printf("Usage :\n%s <input.ISO/input.BIN> [region]\n\n", app_bin);
    puts("Information :");
    puts(" - region   : J/U/E/W (Japan/USA/Europe/World - optional, default=USA)\n");
    return;
}

off_t get_file_size(FILE *file)
{
    fseek(file, 0, SEEK_END);
    off_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    return size;
}

int main(int argc, char *argv[])
{
    uint8_t buffer[12*2048];
    uint8_t backup[2*0x930];
    char tmp[0x40];
    char prod_code[5];
    int pal, prod_num = -1;
    off_t file_size;
    FILE *fp, *fout;
    uint32_t sector_size = 0;
    uint8_t disc_type = DISC_NONE;
    uint8_t region = REGION_USA;
 
    printf("\n\tPlayStation 2 Master Disc Boot Patcher by Bucanero\n\n");

    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }

    if (argc == 3)
    {
        switch (argv[2][0]) {
            case 'J':
            case 'j':
                printf("[i] Forcing Japan region\n");
                region = REGION_JAPAN;
                break;
            case 'U':
            case 'u':
                printf("[i] Forcing USA region\n");
                region = REGION_USA;
                break;
            case 'E':
            case 'e':
                printf("[i] Forcing Europe region\n");
                region = REGION_EUROPE;
                break;
            case 'W':
            case 'w':
                printf("[i] Forcing World region\n");
                region = REGION_WORLD;
                break;
            default:
                usage(argv[0]);
                printf("[!] Unknown region code '%s'\n\n", argv[2]);
                return -1;
        }
    }

    printf("[i] Reading '%s'...\n", argv[1]);
    fp = fopen(argv[1], "r+b");
    if (!fp) {
        perror("Failed to open file!");
        return -1;
    }

    file_size = get_file_size(fp);
    printf("    + Image size: %" PRId64 " bytes\n", (int64_t)file_size);

    if (file_size % 0x800 == 0) {
        printf("    + Detected DVD-ROM Image\n");
        disc_type = DISC_DVD;
        sector_size = 0x800;
    }
    else if (file_size % 0x930 == 0) {
        printf("    + Detected CD-ROM Image\n");
        disc_type = DISC_CD;
        sector_size = 0x930;
        fseek(fp, 0x18, SEEK_SET); // CD-XA Mode 2 Form 1 offset
    }
    else {
        printf("\n[!] Error! File doesn't seems to be a CD or DVD Image file.\n");
        fclose(fp);
        return -1;
    }

    // Read first 12 sectors (PS2 logo)
    for (int i = 0; i < 12; i++)
    {
        fread(buffer + i * 0x800, 0x800, 1, fp);
        if (disc_type == DISC_CD)
            fseek(fp, 0x130, SEEK_CUR); // Skip CD sector extra data
    }

    // Backup Master disc sectors (2 sectors: 14 & 15)
    fseek(fp, sector_size * 14, SEEK_SET);
    fread(backup, sector_size, 2, fp);

    if (disc_type == DISC_CD)
        fseek(fp, 0x18, SEEK_CUR);

    printf("[i] Searching for Disc ID in the image...\n");
    while (fread(tmp, 1, sizeof(tmp), fp) == sizeof(tmp))
    {
        tmp[sizeof(tmp)-1] = 0;
        if (wildcard_match(tmp, "BOOT2*cdrom0:\\*_*.*"))
        {
            printf("    + Found SYSTEM.CNF data at offset 0x%lX\n", ftell(fp) - sizeof(tmp));
            strncpy(prod_code, strchr(tmp, '\\') + 1, 4);
            prod_code[4] = 0;
            sscanf(strchr(tmp, '\\') + 5, "_%d.%d", &pal, &prod_num);
            prod_num += pal * 100;
            pal = wildcard_match(tmp, "*VMODE*PAL*");
            printf("    + Detected Disc ID: %s-%d (%s)\n", prod_code, prod_num, pal ? "PAL" : "NTSC");
            break;
        }
        fseek(fp, sector_size-(sizeof(tmp)), SEEK_CUR);
    }

    if (prod_num < 0) {
        fclose(fp);
        printf("\n[!] Error! Could not detect Disc ID in the image.\n");
        return -1;
    }

    // Check if boot sector is empty
    if (crc32b(buffer, sizeof(buffer)) == 0x6EBED2EE)
    {
        printf("[!] Disc image has an empty boot sector.\n");
        printf("    + Adding Encrypted PS2 logo (%s) to boot sector...\n", pal ? "PAL" : "NTSC");

        if (pal)
            unlzari(lz_pal_bin, sizeof(lz_pal_bin), buffer, sizeof(buffer));
        else
            unlzari(lz_ntsc_bin, sizeof(lz_ntsc_bin), buffer, sizeof(buffer));

        EncryptLogo(buffer, prod_code, prod_num);

        fseek(fp, (disc_type == DISC_CD) ? 0x18 : 0, SEEK_SET);
        for (int i = 0; i < 12; i++)
        {
            fwrite(buffer + i * 0x800, 0x800, 1, fp);
            if (disc_type == DISC_CD)
            {
                fixMode2Form1Sector(fp, i);
                fseek(fp, 0x18, SEEK_CUR);
            }
        }
    }

    DecryptLogo(buffer, prod_code, prod_num);

    switch (crc32b(buffer, sizeof(buffer)))
    {
    case 0x9F1AEE24:    // NTSC
    case 0x87B50222:    // PAL
        printf("[i] Encrypted PS2 logo matches %s-%d\n", prod_code, prod_num);
        break;
    
    default:
        printf("[!] Warning! Disc doesn't seems to have a valid PS2 logo at the start.\n");
        break;
    }

    printf("[i] Backing up disc sectors...\n");
    fout = fopen(disc_type == DISC_DVD ? "DVD_SECTORS.BIN" : "CD_SECTORS.BIN", "wb");
    if (!fout) {
        perror("Failed to open file");
        return -1;
    }

    fwrite(backup, sector_size, 2, fout);
    fclose(fout);
    printf("    + %s_SECTORS.BIN saved OK!\n", disc_type == DISC_DVD ? "DVD" : "CD");

    printf("[i] Writing master disc sectors...\n");
    fseek(fp, sector_size * 14, SEEK_SET);
    // Create a PS2 DVD master disc sector
    int result = write_master_disc_sector(
        fp,                        // file pointer
        prod_code, prod_num,       // disc name
        "PS2 PATCHER",             // producer name
        "SCE",                     // copyright holder
        2009, 10, 3,               // year, month, day (BCD)
        region,                    // region (USA)
        disc_type,                 // disc type (CD or DVD)
        (file_size / sector_size), // number of image sectors
        "2.00"                     // CDVDGEN version
    );

    if(result < 0)
        printf("\n[!] Error writing master disc sectors!\n\n");
    else
        printf("    + Master disc sectors written to '%s'\n\n", argv[1]);

    fseek(fp, 0, SEEK_END);
    fclose(fp);

    return result;
}
