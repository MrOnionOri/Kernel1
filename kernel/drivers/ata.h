#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include <stdint.h>

struct ata_device_info {
    int present;
    uint32_t sectors;
    char serial[21];
    char model[41];
};

void ata_print_data_disk_info(void);
int ata_identify_data_disk(struct ata_device_info* info);
int ata_read_data_sector(uint32_t lba, uint8_t* buffer);
int ata_write_data_sector(uint32_t lba, const uint8_t* buffer);
int ata_print_data_sector(uint32_t lba);
int ata_write_text_sector(uint32_t lba, const char* text);

#endif
