#include "kfs.h"

#include "ata.h"
#include "terminal.h"

#include <stdint.h>

#define KFS_MAGIC 0x3153464B
#define KFS_VERSION 1
#define KFS_SECTOR_SIZE 512
#define KFS_SUPERBLOCK_LBA 0
#define KFS_DIR_START_LBA 1
#define KFS_DIR_SECTORS 4
#define KFS_ENTRY_SIZE 32
#define KFS_ENTRY_NAME_SIZE 20
#define KFS_ENTRY_FLAG_USED 1
#define KFS_MAX_FILE_BYTES 512

struct kfs_superblock {
    uint32_t version;
    uint32_t sector_size;
    uint32_t total_sectors;
    uint32_t dir_start;
    uint32_t dir_sectors;
    uint32_t data_start;
};

static void zero_sector(uint8_t* sector) {
    for (uint32_t i = 0; i < KFS_SECTOR_SIZE; i++) {
        sector[i] = 0;
    }
}

static void write_u32(uint8_t* sector, uint32_t offset, uint32_t value) {
    sector[offset] = (uint8_t)(value & 0xFF);
    sector[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    sector[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    sector[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static int string_equals(const char* left, const char* right) {
    uint32_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return 0;
        }

        index++;
    }

    return left[index] == right[index];
}

static uint32_t string_length(const char* text) {
    uint32_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    return length;
}

static int valid_name(const char* name) {
    uint32_t length = string_length(name);

    if (length == 0 || length >= KFS_ENTRY_NAME_SIZE) {
        return 0;
    }

    for (uint32_t i = 0; i < length; i++) {
        char c = name[i];
        int alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        int digit = c >= '0' && c <= '9';

        if (!alpha && !digit && c != '.' && c != '_' && c != '-') {
            return 0;
        }
    }

    return 1;
}

static uint32_t read_u32(const uint8_t* sector, uint32_t offset) {
    return (uint32_t)sector[offset] |
        ((uint32_t)sector[offset + 1] << 8) |
        ((uint32_t)sector[offset + 2] << 16) |
        ((uint32_t)sector[offset + 3] << 24);
}

static int read_superblock(struct kfs_superblock* superblock) {
    uint8_t sector[KFS_SECTOR_SIZE];

    if (!ata_read_data_sector(KFS_SUPERBLOCK_LBA, sector)) {
        return 0;
    }

    if (read_u32(sector, 0) != KFS_MAGIC) {
        return 0;
    }

    superblock->version = read_u32(sector, 4);
    superblock->sector_size = read_u32(sector, 8);
    superblock->total_sectors = read_u32(sector, 12);
    superblock->dir_start = read_u32(sector, 16);
    superblock->dir_sectors = read_u32(sector, 20);
    superblock->data_start = read_u32(sector, 24);
    return superblock->version == KFS_VERSION &&
        superblock->sector_size == KFS_SECTOR_SIZE &&
        superblock->dir_start == KFS_DIR_START_LBA &&
        superblock->dir_sectors == KFS_DIR_SECTORS;
}

static uint32_t max_entries(void) {
    return (KFS_DIR_SECTORS * KFS_SECTOR_SIZE) / KFS_ENTRY_SIZE;
}

static uint32_t entry_sector_lba(uint32_t slot) {
    return KFS_DIR_START_LBA + (slot * KFS_ENTRY_SIZE) / KFS_SECTOR_SIZE;
}

static uint32_t entry_sector_offset(uint32_t slot) {
    return (slot * KFS_ENTRY_SIZE) % KFS_SECTOR_SIZE;
}

static uint32_t entry_data_lba(const struct kfs_superblock* superblock, uint32_t slot) {
    return superblock->data_start + slot;
}

static void entry_read_name(const uint8_t* sector, uint32_t offset, char* name) {
    for (uint32_t i = 0; i < KFS_ENTRY_NAME_SIZE; i++) {
        name[i] = (char)sector[offset + 4 + i];
    }

    name[KFS_ENTRY_NAME_SIZE - 1] = '\0';
}

static void entry_write_name(uint8_t* sector, uint32_t offset, const char* name) {
    for (uint32_t i = 0; i < KFS_ENTRY_NAME_SIZE; i++) {
        sector[offset + 4 + i] = 0;
    }

    for (uint32_t i = 0; name[i] != '\0' && i < KFS_ENTRY_NAME_SIZE - 1; i++) {
        sector[offset + 4 + i] = (uint8_t)name[i];
    }
}

static void print_magic(uint32_t magic) {
    terminal_putchar((char)(magic & 0xFF));
    terminal_putchar((char)((magic >> 8) & 0xFF));
    terminal_putchar((char)((magic >> 16) & 0xFF));
    terminal_putchar((char)((magic >> 24) & 0xFF));
}

int kfs_format(void) {
    struct ata_device_info disk;
    uint8_t sector[KFS_SECTOR_SIZE];

    if (!ata_identify_data_disk(&disk)) {
        terminal_write("kfsformat: data disk not detected\n");
        return 0;
    }

    if (disk.sectors <= KFS_DIR_START_LBA + KFS_DIR_SECTORS) {
        terminal_write("kfsformat: disk is too small\n");
        return 0;
    }

    zero_sector(sector);
    write_u32(sector, 0, KFS_MAGIC);
    write_u32(sector, 4, KFS_VERSION);
    write_u32(sector, 8, KFS_SECTOR_SIZE);
    write_u32(sector, 12, disk.sectors);
    write_u32(sector, 16, KFS_DIR_START_LBA);
    write_u32(sector, 20, KFS_DIR_SECTORS);
    write_u32(sector, 24, KFS_DIR_START_LBA + KFS_DIR_SECTORS);

    if (!ata_write_data_sector(KFS_SUPERBLOCK_LBA, sector)) {
        terminal_write("kfsformat: failed to write superblock\n");
        return 0;
    }

    zero_sector(sector);
    for (uint32_t lba = KFS_DIR_START_LBA;
            lba < KFS_DIR_START_LBA + KFS_DIR_SECTORS; lba++) {
        if (!ata_write_data_sector(lba, sector)) {
            terminal_write("kfsformat: failed to clear directory\n");
            return 0;
        }
    }

    terminal_write("KFS formatted data disk\n");
    terminal_write("Data starts at sector ");
    terminal_write_dec(KFS_DIR_START_LBA + KFS_DIR_SECTORS);
    terminal_write("\n");
    return 1;
}

int kfs_list(void) {
    struct kfs_superblock superblock;
    uint8_t sector[KFS_SECTOR_SIZE];
    uint32_t used = 0;

    if (!read_superblock(&superblock)) {
        terminal_write("kfsls: disk is not formatted\n");
        return 0;
    }

    terminal_write("KFS files:\n");
    for (uint32_t slot = 0; slot < max_entries(); slot++) {
        uint32_t lba = entry_sector_lba(slot);
        uint32_t offset = entry_sector_offset(slot);

        if (!ata_read_data_sector(lba, sector)) {
            terminal_write("kfsls: unable to read directory\n");
            return 0;
        }

        if (sector[offset] != KFS_ENTRY_FLAG_USED) {
            continue;
        }

        char name[KFS_ENTRY_NAME_SIZE];
        entry_read_name(sector, offset, name);

        terminal_write("  ");
        terminal_write(name);
        terminal_write("  ");
        terminal_write_dec(read_u32(sector, offset + 28));
        terminal_write(" bytes  sector ");
        terminal_write_dec(read_u32(sector, offset + 24));
        terminal_write("\n");
        used++;
    }

    if (used == 0) {
        terminal_write("  <empty>\n");
    }

    return 1;
}

static int find_slot(const struct kfs_superblock* superblock, const char* name,
        uint32_t* slot_out, int* found_out) {
    uint8_t sector[KFS_SECTOR_SIZE];
    uint32_t first_free = max_entries();

    for (uint32_t slot = 0; slot < max_entries(); slot++) {
        uint32_t data_lba = entry_data_lba(superblock, slot);
        if (data_lba >= superblock->total_sectors) {
            break;
        }

        uint32_t lba = entry_sector_lba(slot);
        uint32_t offset = entry_sector_offset(slot);

        if (!ata_read_data_sector(lba, sector)) {
            return 0;
        }

        if (sector[offset] != KFS_ENTRY_FLAG_USED) {
            if (first_free == max_entries()) {
                first_free = slot;
            }
            continue;
        }

        char existing[KFS_ENTRY_NAME_SIZE];
        entry_read_name(sector, offset, existing);
        if (string_equals(existing, name)) {
            *slot_out = slot;
            *found_out = 1;
            return 1;
        }
    }

    if (first_free == max_entries()) {
        return 0;
    }

    *slot_out = first_free;
    *found_out = 0;
    return 1;
}

int kfs_save_text(const char* name, const char* text) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint8_t data_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;

    if (!valid_name(name)) {
        terminal_write("kfssave: invalid name\n");
        return 0;
    }

    if (!read_superblock(&superblock)) {
        terminal_write("kfssave: disk is not formatted\n");
        return 0;
    }

    if (!find_slot(&superblock, name, &slot, &found)) {
        terminal_write("kfssave: no free directory entry\n");
        return 0;
    }

    uint32_t size = string_length(text);
    if (size > KFS_MAX_FILE_BYTES) {
        size = KFS_MAX_FILE_BYTES;
    }

    zero_sector(data_sector);
    for (uint32_t i = 0; i < size; i++) {
        data_sector[i] = (uint8_t)text[i];
    }

    uint32_t data_lba = entry_data_lba(&superblock, slot);
    if (!ata_write_data_sector(data_lba, data_sector)) {
        terminal_write("kfssave: failed to write data\n");
        return 0;
    }

    uint32_t dir_lba = entry_sector_lba(slot);
    uint32_t offset = entry_sector_offset(slot);
    if (!ata_read_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfssave: failed to read directory\n");
        return 0;
    }

    dir_sector[offset] = KFS_ENTRY_FLAG_USED;
    write_u32(dir_sector, offset + 24, data_lba);
    write_u32(dir_sector, offset + 28, size);
    entry_write_name(dir_sector, offset, name);

    if (!ata_write_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfssave: failed to update directory\n");
        return 0;
    }

    terminal_write(found ? "Updated " : "Saved ");
    terminal_write(name);
    terminal_write(" (");
    terminal_write_dec(size);
    terminal_write(" bytes)\n");
    return 1;
}

int kfs_cat(const char* name) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint8_t data_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;

    if (!valid_name(name)) {
        terminal_write("kfscat: invalid name\n");
        return 0;
    }

    if (!read_superblock(&superblock)) {
        terminal_write("kfscat: disk is not formatted\n");
        return 0;
    }

    if (!find_slot(&superblock, name, &slot, &found) || !found) {
        terminal_write("kfscat: file not found\n");
        return 0;
    }

    uint32_t dir_lba = entry_sector_lba(slot);
    uint32_t offset = entry_sector_offset(slot);
    if (!ata_read_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfscat: failed to read directory\n");
        return 0;
    }

    uint32_t data_lba = read_u32(dir_sector, offset + 24);
    uint32_t size = read_u32(dir_sector, offset + 28);
    if (size > KFS_MAX_FILE_BYTES) {
        size = KFS_MAX_FILE_BYTES;
    }

    if (!ata_read_data_sector(data_lba, data_sector)) {
        terminal_write("kfscat: failed to read data\n");
        return 0;
    }

    for (uint32_t i = 0; i < size; i++) {
        terminal_putchar((char)data_sector[i]);
    }
    terminal_putchar('\n');
    return 1;
}

int kfs_stat(const char* name) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;

    if (!valid_name(name)) {
        terminal_write("kfsstat: invalid name\n");
        return 0;
    }

    if (!read_superblock(&superblock)) {
        terminal_write("kfsstat: disk is not formatted\n");
        return 0;
    }

    if (!find_slot(&superblock, name, &slot, &found) || !found) {
        terminal_write("kfsstat: file not found\n");
        return 0;
    }

    uint32_t dir_lba = entry_sector_lba(slot);
    uint32_t offset = entry_sector_offset(slot);
    if (!ata_read_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfsstat: failed to read directory\n");
        return 0;
    }

    terminal_write("KFS file: ");
    terminal_write(name);
    terminal_write("\nSlot: ");
    terminal_write_dec(slot);
    terminal_write("\nDirectory sector: ");
    terminal_write_dec(dir_lba);
    terminal_write("\nData sector: ");
    terminal_write_dec(read_u32(dir_sector, offset + 24));
    terminal_write("\nSize: ");
    terminal_write_dec(read_u32(dir_sector, offset + 28));
    terminal_write(" bytes\n");
    return 1;
}

int kfs_remove(const char* name) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint8_t data_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;

    if (!valid_name(name)) {
        terminal_write("kfsrm: invalid name\n");
        return 0;
    }

    if (!read_superblock(&superblock)) {
        terminal_write("kfsrm: disk is not formatted\n");
        return 0;
    }

    if (!find_slot(&superblock, name, &slot, &found) || !found) {
        terminal_write("kfsrm: file not found\n");
        return 0;
    }

    uint32_t dir_lba = entry_sector_lba(slot);
    uint32_t offset = entry_sector_offset(slot);
    if (!ata_read_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfsrm: failed to read directory\n");
        return 0;
    }

    uint32_t data_lba = read_u32(dir_sector, offset + 24);
    zero_sector(data_sector);
    if (!ata_write_data_sector(data_lba, data_sector)) {
        terminal_write("kfsrm: failed to clear data\n");
        return 0;
    }

    for (uint32_t i = 0; i < KFS_ENTRY_SIZE; i++) {
        dir_sector[offset + i] = 0;
    }

    if (!ata_write_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfsrm: failed to update directory\n");
        return 0;
    }

    terminal_write("Removed ");
    terminal_write(name);
    terminal_write("\n");
    return 1;
}

int kfs_print_info(void) {
    uint8_t sector[KFS_SECTOR_SIZE];

    if (!ata_read_data_sector(KFS_SUPERBLOCK_LBA, sector)) {
        terminal_write("kfsinfo: unable to read superblock\n");
        return 0;
    }

    uint32_t magic = read_u32(sector, 0);
    if (magic != KFS_MAGIC) {
        terminal_write("KFS: not formatted\n");
        return 0;
    }

    terminal_write("KFS filesystem\n");
    terminal_write("Magic: ");
    print_magic(magic);
    terminal_write("\nVersion: ");
    terminal_write_dec(read_u32(sector, 4));
    terminal_write("\nSector size: ");
    terminal_write_dec(read_u32(sector, 8));
    terminal_write("\nTotal sectors: ");
    terminal_write_dec(read_u32(sector, 12));
    terminal_write("\nDirectory start: ");
    terminal_write_dec(read_u32(sector, 16));
    terminal_write("\nDirectory sectors: ");
    terminal_write_dec(read_u32(sector, 20));
    terminal_write("\nData start: ");
    terminal_write_dec(read_u32(sector, 24));
    terminal_write("\n");
    return 1;
}
