#include "kfs.h"

#include "ata.h"
#include "terminal.h"

#include <stdint.h>

#define KFS_MAGIC 0x3153464B
#define KFS_VERSION 2
#define KFS_SECTOR_SIZE 512
#define KFS_SUPERBLOCK_LBA 0
#define KFS_DIR_START_LBA 1
#define KFS_DIR_SECTORS 4
#define KFS_BITMAP_LBA (KFS_DIR_START_LBA + KFS_DIR_SECTORS)
#define KFS_BITMAP_MAX_SECTORS (KFS_SECTOR_SIZE * 8)
#define KFS_ENTRY_SIZE 32
#define KFS_ENTRY_NAME_SIZE 20
#define KFS_ENTRY_FLAG_FILE 1
#define KFS_ENTRY_FLAG_DIRECTORY 2
#define KFS_MAX_FILE_BYTES 4096
#define KFS_NEXT_NONE 0
#define KFS_BLOCK_DATA_OFFSET 4
#define KFS_BLOCK_DATA_BYTES (KFS_SECTOR_SIZE - KFS_BLOCK_DATA_OFFSET)
#define KFS_MAX_FILE_SECTORS ((KFS_MAX_FILE_BYTES + KFS_BLOCK_DATA_BYTES - 1) / KFS_BLOCK_DATA_BYTES)

struct kfs_superblock {
    uint32_t version;
    uint32_t sector_size;
    uint32_t total_sectors;
    uint32_t dir_start;
    uint32_t dir_sectors;
    uint32_t data_start;
};

static char append_buffer[KFS_MAX_FILE_BYTES + 1];
static uint8_t check_referenced[KFS_BITMAP_MAX_SECTORS];

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
    int previous_slash = 0;

    if (length == 0 || length >= KFS_ENTRY_NAME_SIZE) {
        return 0;
    }

    if (name[0] == '/' || name[length - 1] == '/') {
        return 0;
    }

    for (uint32_t i = 0; i < length; i++) {
        char c = name[i];
        int alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        int digit = c >= '0' && c <= '9';

        if (c == '/') {
            if (previous_slash) {
                return 0;
            }
            previous_slash = 1;
            continue;
        }

        previous_slash = 0;
        if (!alpha && !digit && c != '.' && c != '_' && c != '-') {
            return 0;
        }
    }

    return 1;
}

static int parent_name(const char* name, char* output, uint32_t size) {
    uint32_t last_slash = 0;
    int has_slash = 0;
    uint32_t index = 0;

    if (size == 0) {
        return 0;
    }

    for (uint32_t i = 0; name[i] != '\0'; i++) {
        if (name[i] == '/') {
            has_slash = 1;
            last_slash = i;
        }
    }

    if (!has_slash) {
        output[0] = '\0';
        return 1;
    }

    while (index < size - 1 && index < last_slash) {
        output[index] = name[index];
        index++;
    }

    output[index] = '\0';
    return index == last_slash;
}

static uint32_t read_u32(const uint8_t* sector, uint32_t offset) {
    return (uint32_t)sector[offset] |
        ((uint32_t)sector[offset + 1] << 8) |
        ((uint32_t)sector[offset + 2] << 16) |
        ((uint32_t)sector[offset + 3] << 24);
}

static int bitmap_get(const uint8_t* bitmap, uint32_t lba) {
    return (bitmap[lba / 8] & (uint8_t)(1 << (lba % 8))) != 0;
}

static void bitmap_set(uint8_t* bitmap, uint32_t lba, int used) {
    uint8_t mask = (uint8_t)(1 << (lba % 8));

    if (used) {
        bitmap[lba / 8] |= mask;
    } else {
        bitmap[lba / 8] &= (uint8_t)~mask;
    }
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

static uint32_t sectors_for_size(uint32_t size) {
    if (size == 0) {
        return 1;
    }

    return (size + KFS_BLOCK_DATA_BYTES - 1) / KFS_BLOCK_DATA_BYTES;
}

static int allocate_data_blocks(const struct kfs_superblock* superblock,
        uint32_t sector_count, uint32_t* blocks) {
    uint8_t bitmap[KFS_SECTOR_SIZE];
    uint32_t found = 0;

    if (sector_count == 0 || sector_count > KFS_MAX_FILE_SECTORS) {
        return 0;
    }

    if (!ata_read_data_sector(KFS_BITMAP_LBA, bitmap)) {
        return 0;
    }

    for (uint32_t lba = superblock->data_start;
            lba < superblock->total_sectors && found < sector_count; lba++) {
        if (!bitmap_get(bitmap, lba)) {
            blocks[found++] = lba;
            bitmap_set(bitmap, lba, 1);
        }
    }

    if (found != sector_count) {
        return 0;
    }

    return ata_write_data_sector(KFS_BITMAP_LBA, bitmap);
}

static int free_data_block(uint32_t lba) {
    uint8_t bitmap[KFS_SECTOR_SIZE];

    if (!ata_read_data_sector(KFS_BITMAP_LBA, bitmap)) {
        return 0;
    }

    bitmap_set(bitmap, lba, 0);
    return ata_write_data_sector(KFS_BITMAP_LBA, bitmap);
}

static int clear_data_chain(uint32_t first_lba, uint32_t expected_blocks,
        const struct kfs_superblock* superblock) {
    uint8_t sector[KFS_SECTOR_SIZE];
    uint32_t lba = first_lba;

    for (uint32_t i = 0; i < expected_blocks && lba != KFS_NEXT_NONE; i++) {
        if (lba < superblock->data_start || lba >= superblock->total_sectors) {
            return 0;
        }

        if (!ata_read_data_sector(lba, sector)) {
            return 0;
        }

        uint32_t next_lba = read_u32(sector, 0);
        zero_sector(sector);
        if (!ata_write_data_sector(lba, sector)) {
            return 0;
        }

        if (!free_data_block(lba)) {
            return 0;
        }

        lba = next_lba;
    }

    return 1;
}

static int clear_allocated_blocks(const uint32_t* blocks, uint32_t block_count) {
    uint8_t sector[KFS_SECTOR_SIZE];
    zero_sector(sector);

    for (uint32_t i = 0; i < block_count; i++) {
        ata_write_data_sector(blocks[i], sector);
        free_data_block(blocks[i]);
    }

    return 1;
}

static int write_text_chain(const uint32_t* blocks, uint32_t block_count,
        const char* text, uint32_t size) {
    uint8_t sector[KFS_SECTOR_SIZE];
    uint32_t written = 0;

    for (uint32_t block_index = 0; block_index < block_count; block_index++) {
        zero_sector(sector);
        uint32_t next_lba = block_index + 1 < block_count ?
            blocks[block_index + 1] : KFS_NEXT_NONE;
        write_u32(sector, 0, next_lba);

        for (uint32_t i = KFS_BLOCK_DATA_OFFSET; i < KFS_SECTOR_SIZE && written < size; i++) {
            sector[i] = (uint8_t)text[written++];
        }

        if (!ata_write_data_sector(blocks[block_index], sector)) {
            return 0;
        }
    }

    return 1;
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

static void kfs_check_error(uint32_t* errors, const char* text) {
    terminal_write("  error: ");
    terminal_write(text);
    terminal_write("\n");
    (*errors)++;
}

static void kfs_check_error_name(uint32_t* errors, const char* text,
        const char* name) {
    terminal_write("  error: ");
    terminal_write(text);
    terminal_write(": ");
    terminal_write(name);
    terminal_write("\n");
    (*errors)++;
}

static int kfs_check_seen_block(const uint32_t* seen, uint32_t count,
        uint32_t lba) {
    for (uint32_t i = 0; i < count; i++) {
        if (seen[i] == lba) {
            return 1;
        }
    }

    return 0;
}

int kfs_format(void) {
    struct ata_device_info disk;
    uint8_t sector[KFS_SECTOR_SIZE];

    if (!ata_identify_data_disk(&disk)) {
        terminal_write("kfsformat: data disk not detected\n");
        return 0;
    }

    if (disk.sectors > KFS_BITMAP_MAX_SECTORS) {
        terminal_write("kfsformat: disk is too large for bitmap\n");
        return 0;
    }

    if (disk.sectors <= KFS_BITMAP_LBA + 1) {
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
    write_u32(sector, 24, KFS_BITMAP_LBA + 1);

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

    zero_sector(sector);
    for (uint32_t lba = 0; lba <= KFS_BITMAP_LBA; lba++) {
        bitmap_set(sector, lba, 1);
    }

    if (!ata_write_data_sector(KFS_BITMAP_LBA, sector)) {
        terminal_write("kfsformat: failed to write bitmap\n");
        return 0;
    }

    terminal_write("KFS formatted data disk\n");
    terminal_write("Bitmap sector ");
    terminal_write_dec(KFS_BITMAP_LBA);
    terminal_write("\n");
    terminal_write("Data starts at sector ");
    terminal_write_dec(KFS_BITMAP_LBA + 1);
    terminal_write("\n");
    return 1;
}

int kfs_check(int verbose) {
    struct kfs_superblock superblock;
    uint8_t bitmap[KFS_SECTOR_SIZE];
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint8_t data_sector[KFS_SECTOR_SIZE];
    uint32_t errors = 0;
    uint32_t file_count = 0;
    uint32_t dir_count = 0;
    uint32_t referenced_blocks = 0;
    uint32_t leaked_blocks = 0;

    terminal_write("KFS check:\n");
    if (!read_superblock(&superblock)) {
        kfs_check_error(&errors, "invalid or missing superblock");
        terminal_write("  result: problems found\n");
        return 0;
    }

    if (superblock.total_sectors > KFS_BITMAP_MAX_SECTORS) {
        kfs_check_error(&errors, "disk exceeds KFS bitmap capacity");
    }

    if (superblock.data_start != KFS_BITMAP_LBA + 1) {
        kfs_check_error(&errors, "unexpected data start sector");
    }

    if (!ata_read_data_sector(KFS_BITMAP_LBA, bitmap)) {
        kfs_check_error(&errors, "unable to read bitmap");
        terminal_write("  result: problems found\n");
        return 0;
    }

    uint32_t scan_limit = superblock.total_sectors;
    if (scan_limit > KFS_BITMAP_MAX_SECTORS) {
        scan_limit = KFS_BITMAP_MAX_SECTORS;
    }

    for (uint32_t lba = 0; lba < scan_limit; lba++) {
        check_referenced[lba] = 0;
    }

    for (uint32_t lba = 0; lba < superblock.data_start && lba < scan_limit; lba++) {
        if (!bitmap_get(bitmap, lba)) {
            kfs_check_error(&errors, "reserved sector is marked free");
            break;
        }
    }

    for (uint32_t slot = 0; slot < max_entries(); slot++) {
        uint32_t entry_lba = entry_sector_lba(slot);
        uint32_t offset = entry_sector_offset(slot);

        if (!ata_read_data_sector(entry_lba, dir_sector)) {
            kfs_check_error(&errors, "unable to read directory sector");
            break;
        }

        uint8_t flag = dir_sector[offset];
        if (flag == 0) {
            continue;
        }

        if (flag != KFS_ENTRY_FLAG_FILE && flag != KFS_ENTRY_FLAG_DIRECTORY) {
            kfs_check_error(&errors, "invalid directory entry flag");
            continue;
        }

        char name[KFS_ENTRY_NAME_SIZE];
        entry_read_name(dir_sector, offset, name);
        if (!valid_name(name)) {
            kfs_check_error(&errors, "invalid directory entry name");
            continue;
        }

        uint32_t data_lba = read_u32(dir_sector, offset + 24);
        uint32_t size = read_u32(dir_sector, offset + 28);

        if (flag == KFS_ENTRY_FLAG_DIRECTORY) {
            dir_count++;
            if (verbose) {
                terminal_write("  dir  slot=");
                terminal_write_dec(slot);
                terminal_write(" name=");
                terminal_write(name);
                terminal_write("\n");
            }

            if (data_lba != 0 || size != 0) {
                kfs_check_error_name(&errors, "directory has file metadata", name);
            }
            continue;
        }

        file_count++;
        if (verbose) {
            terminal_write("  file slot=");
            terminal_write_dec(slot);
            terminal_write(" name=");
            terminal_write(name);
            terminal_write(" size=");
            terminal_write_dec(size);
            terminal_write(" blocks=");
        }

        if (size > KFS_MAX_FILE_BYTES) {
            if (verbose) {
                terminal_write("<invalid>\n");
            }
            kfs_check_error_name(&errors, "file exceeds max size", name);
            continue;
        }

        uint32_t expected_blocks = sectors_for_size(size);
        uint32_t current_lba = data_lba;
        uint32_t seen[KFS_MAX_FILE_SECTORS];
        uint32_t seen_count = 0;

        for (uint32_t block = 0; block < expected_blocks; block++) {
            if (current_lba < superblock.data_start ||
                    current_lba >= superblock.total_sectors ||
                    current_lba >= KFS_BITMAP_MAX_SECTORS) {
                if (verbose) {
                    terminal_write("<invalid>");
                }
                kfs_check_error_name(&errors, "file has invalid block", name);
                break;
            }

            if (kfs_check_seen_block(seen, seen_count, current_lba)) {
                if (verbose) {
                    terminal_write("<loop:");
                    terminal_write_dec(current_lba);
                    terminal_write(">");
                }
                kfs_check_error_name(&errors, "file block chain loops", name);
                break;
            }

            seen[seen_count++] = current_lba;
            if (verbose) {
                if (block != 0) {
                    terminal_write("->");
                }
                terminal_write_dec(current_lba);
            }

            if (check_referenced[current_lba]) {
                kfs_check_error_name(&errors, "block is shared by multiple files", name);
            } else {
                check_referenced[current_lba] = 1;
                referenced_blocks++;
            }

            if (!bitmap_get(bitmap, current_lba)) {
                kfs_check_error_name(&errors, "file block is marked free", name);
            }

            if (!ata_read_data_sector(current_lba, data_sector)) {
                kfs_check_error_name(&errors, "unable to read file block", name);
                break;
            }

            uint32_t next_lba = read_u32(data_sector, 0);
            if (block + 1 == expected_blocks && next_lba != KFS_NEXT_NONE) {
                kfs_check_error_name(&errors, "file chain has extra blocks", name);
            }
            current_lba = next_lba;
        }

        if (verbose) {
            terminal_write("\n");
        }
    }

    for (uint32_t lba = superblock.data_start; lba < scan_limit; lba++) {
        if (bitmap_get(bitmap, lba) && !check_referenced[lba]) {
            leaked_blocks++;
            if (verbose) {
                terminal_write("  leaked block=");
                terminal_write_dec(lba);
                terminal_write("\n");
            }
        }
    }

    if (leaked_blocks != 0) {
        kfs_check_error(&errors, "bitmap has leaked data blocks");
    }

    terminal_write("  entries: ");
    terminal_write_dec(file_count);
    terminal_write(" files, ");
    terminal_write_dec(dir_count);
    terminal_write(" dirs\n  blocks: ");
    terminal_write_dec(referenced_blocks);
    terminal_write(" referenced, ");
    terminal_write_dec(leaked_blocks);
    terminal_write(" leaked\n  errors: ");
    terminal_write_dec(errors);
    terminal_write("\n  result: ");
    terminal_write(errors == 0 ? "clean\n" : "problems found\n");
    return errors == 0;
}

int kfs_print_usage(void) {
    struct kfs_superblock superblock;
    uint8_t bitmap[KFS_SECTOR_SIZE];
    uint32_t used_sectors = 0;

    if (!read_superblock(&superblock)) {
        terminal_write("df: disk is not formatted\n");
        return 0;
    }

    if (superblock.total_sectors > KFS_BITMAP_MAX_SECTORS) {
        terminal_write("df: disk exceeds KFS bitmap capacity\n");
        return 0;
    }

    if (!ata_read_data_sector(KFS_BITMAP_LBA, bitmap)) {
        terminal_write("df: unable to read bitmap\n");
        return 0;
    }

    for (uint32_t lba = 0; lba < superblock.total_sectors; lba++) {
        if (bitmap_get(bitmap, lba)) {
            used_sectors++;
        }
    }

    uint32_t reserved_sectors = superblock.data_start;
    uint32_t data_sectors = superblock.total_sectors - reserved_sectors;
    uint32_t used_data_sectors = 0;
    if (used_sectors > reserved_sectors) {
        used_data_sectors = used_sectors - reserved_sectors;
    }

    uint32_t free_sectors = superblock.total_sectors - used_sectors;
    uint32_t percent = 0;
    if (superblock.total_sectors != 0) {
        percent = (used_sectors * 100) / superblock.total_sectors;
    }

    terminal_write("Disk usage for /disk:\n");
    terminal_write("  Sectors total: ");
    terminal_write_dec(superblock.total_sectors);
    terminal_write("\n  Sectors used: ");
    terminal_write_dec(used_sectors);
    terminal_write("\n  Sectors free: ");
    terminal_write_dec(free_sectors);
    terminal_write("\n  Use: ");
    terminal_write_dec(percent);
    terminal_write("%\n  Reserved sectors: ");
    terminal_write_dec(reserved_sectors);
    terminal_write("\n  Data sectors: ");
    terminal_write_dec(data_sectors);
    terminal_write("\n  Data used/free: ");
    terminal_write_dec(used_data_sectors);
    terminal_write("/");
    terminal_write_dec(data_sectors - used_data_sectors);
    terminal_write("\n  Bytes used/free: ");
    terminal_write_dec(used_sectors * KFS_SECTOR_SIZE);
    terminal_write("/");
    terminal_write_dec(free_sectors * KFS_SECTOR_SIZE);
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

        if (sector[offset] == 0) {
            continue;
        }

        char name[KFS_ENTRY_NAME_SIZE];
        entry_read_name(sector, offset, name);

        terminal_write("  ");
        terminal_write(name);
        terminal_write("  ");
        if (sector[offset] == KFS_ENTRY_FLAG_DIRECTORY) {
            terminal_write("<dir>");
        } else {
            terminal_write_dec(read_u32(sector, offset + 28));
            terminal_write(" bytes  sector ");
            terminal_write_dec(read_u32(sector, offset + 24));
            terminal_write("  sectors ");
            terminal_write_dec(sectors_for_size(read_u32(sector, offset + 28)));
        }
        terminal_write("\n");
        used++;
    }

    if (used == 0) {
        terminal_write("  <empty>\n");
    }

    return 1;
}

int kfs_for_each(kfs_visit_callback callback, void* context) {
    struct kfs_superblock superblock;
    uint8_t sector[KFS_SECTOR_SIZE];

    if (callback == 0) {
        return 0;
    }

    if (!read_superblock(&superblock)) {
        return 0;
    }

    for (uint32_t slot = 0; slot < max_entries(); slot++) {
        uint32_t data_lba = entry_data_lba(&superblock, slot);
        if (data_lba >= superblock.total_sectors) {
            break;
        }

        uint32_t lba = entry_sector_lba(slot);
        uint32_t offset = entry_sector_offset(slot);

        if (!ata_read_data_sector(lba, sector)) {
            return 0;
        }

        if (sector[offset] == 0) {
            continue;
        }

        char name[KFS_ENTRY_NAME_SIZE];
        entry_read_name(sector, offset, name);
        callback(name, read_u32(sector, offset + 28),
            read_u32(sector, offset + 24),
            sector[offset] == KFS_ENTRY_FLAG_DIRECTORY, context);
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

        if (sector[offset] == 0) {
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

int kfs_is_directory(const char* name) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;

    if (name[0] == '\0') {
        return 1;
    }

    if (!valid_name(name) || !read_superblock(&superblock)) {
        return 0;
    }

    if (!find_slot(&superblock, name, &slot, &found) || !found) {
        return 0;
    }

    uint32_t dir_lba = entry_sector_lba(slot);
    uint32_t offset = entry_sector_offset(slot);
    if (!ata_read_data_sector(dir_lba, dir_sector)) {
        return 0;
    }

    return dir_sector[offset] == KFS_ENTRY_FLAG_DIRECTORY;
}

int kfs_mkdir(const char* name) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;
    char parent[KFS_ENTRY_NAME_SIZE];

    if (!valid_name(name)) {
        terminal_write("mkdir: invalid disk path\n");
        return 0;
    }

    if (!read_superblock(&superblock)) {
        terminal_write("mkdir: disk is not formatted\n");
        return 0;
    }

    if (!parent_name(name, parent, sizeof(parent))) {
        terminal_write("mkdir: path too long\n");
        return 0;
    }

    if (parent[0] != '\0' && !kfs_is_directory(parent)) {
        terminal_write("mkdir: parent not found: disk/");
        terminal_write(parent);
        terminal_write("\n");
        return 0;
    }

    if (!find_slot(&superblock, name, &slot, &found)) {
        terminal_write("mkdir: no free disk directory entry\n");
        return 0;
    }

    if (found) {
        terminal_write("mkdir: already exists: disk/");
        terminal_write(name);
        terminal_write("\n");
        return 0;
    }

    uint32_t dir_lba = entry_sector_lba(slot);
    uint32_t offset = entry_sector_offset(slot);
    if (!ata_read_data_sector(dir_lba, dir_sector)) {
        terminal_write("mkdir: failed to read disk directory\n");
        return 0;
    }

    dir_sector[offset] = KFS_ENTRY_FLAG_DIRECTORY;
    write_u32(dir_sector, offset + 24, 0);
    write_u32(dir_sector, offset + 28, 0);
    entry_write_name(dir_sector, offset, name);

    if (!ata_write_data_sector(dir_lba, dir_sector)) {
        terminal_write("mkdir: failed to update disk directory\n");
        return 0;
    }

    return 1;
}

int kfs_save_text(const char* name, const char* text) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;
    char parent[KFS_ENTRY_NAME_SIZE];

    if (!valid_name(name)) {
        terminal_write("kfssave: invalid name\n");
        return 0;
    }

    if (!read_superblock(&superblock)) {
        terminal_write("kfssave: disk is not formatted\n");
        return 0;
    }

    if (!parent_name(name, parent, sizeof(parent))) {
        terminal_write("kfssave: path too long\n");
        return 0;
    }

    if (parent[0] != '\0' && !kfs_is_directory(parent)) {
        terminal_write("kfssave: parent not found: ");
        terminal_write(parent);
        terminal_write("\n");
        return 0;
    }

    if (!find_slot(&superblock, name, &slot, &found)) {
        terminal_write("kfssave: no free directory entry\n");
        return 0;
    }

    uint32_t dir_lba = entry_sector_lba(slot);
    uint32_t offset = entry_sector_offset(slot);
    if (!ata_read_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfssave: failed to read directory\n");
        return 0;
    }

    if (found && dir_sector[offset] == KFS_ENTRY_FLAG_DIRECTORY) {
        terminal_write("kfssave: is a directory\n");
        return 0;
    }

    uint32_t size = string_length(text);
    if (size > KFS_MAX_FILE_BYTES) {
        size = KFS_MAX_FILE_BYTES;
    }

    uint32_t needed_sectors = sectors_for_size(size);
    uint32_t old_data_lba = found ? read_u32(dir_sector, offset + 24) : 0;
    uint32_t old_size = found ? read_u32(dir_sector, offset + 28) : 0;
    uint32_t old_sectors = found ? sectors_for_size(old_size) : 0;
    uint32_t blocks[KFS_MAX_FILE_SECTORS];

    if (!allocate_data_blocks(&superblock, needed_sectors, blocks)) {
        terminal_write("kfssave: no free data blocks\n");
        return 0;
    }

    if (!write_text_chain(blocks, needed_sectors, text, size)) {
        terminal_write("kfssave: failed to write data\n");
        clear_allocated_blocks(blocks, needed_sectors);
        return 0;
    }

    dir_sector[offset] = KFS_ENTRY_FLAG_FILE;
    write_u32(dir_sector, offset + 24, blocks[0]);
    write_u32(dir_sector, offset + 28, size);
    entry_write_name(dir_sector, offset, name);

    if (!ata_write_data_sector(dir_lba, dir_sector)) {
        terminal_write("kfssave: failed to update directory\n");
        clear_allocated_blocks(blocks, needed_sectors);
        return 0;
    }

    if (found && old_data_lba >= superblock.data_start) {
        clear_data_chain(old_data_lba, old_sectors, &superblock);
    }

    terminal_write(found ? "Updated " : "Saved ");
    terminal_write(name);
    terminal_write(" (");
    terminal_write_dec(size);
    terminal_write(" bytes)\n");
    return 1;
}

int kfs_read_text(const char* name, char* output, uint32_t output_size,
        uint32_t* size_out) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint8_t data_sector[KFS_SECTOR_SIZE];
    uint32_t slot = 0;
    int found = 0;

    if (output_size == 0) {
        return 0;
    }

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

    if (dir_sector[offset] == KFS_ENTRY_FLAG_DIRECTORY) {
        terminal_write("kfscat: is a directory\n");
        return 0;
    }

    uint32_t data_lba = read_u32(dir_sector, offset + 24);
    uint32_t size = read_u32(dir_sector, offset + 28);
    if (size > KFS_MAX_FILE_BYTES) {
        size = KFS_MAX_FILE_BYTES;
    }

    uint32_t sector_count = sectors_for_size(size);
    if (data_lba < superblock.data_start || data_lba >= superblock.total_sectors) {
        terminal_write("kfscat: invalid data range\n");
        return 0;
    }

    uint32_t copied = 0;
    uint32_t current_lba = data_lba;
    for (uint32_t sector_index = 0; sector_index < sector_count; sector_index++) {
        if (current_lba < superblock.data_start || current_lba >= superblock.total_sectors) {
            terminal_write("kfscat: invalid block chain\n");
            return 0;
        }

        if (!ata_read_data_sector(current_lba, data_sector)) {
            terminal_write("kfscat: failed to read data\n");
            return 0;
        }

        current_lba = read_u32(data_sector, 0);

        for (uint32_t i = KFS_BLOCK_DATA_OFFSET; i < KFS_SECTOR_SIZE && copied < size; i++) {
            if (copied < output_size - 1) {
                output[copied] = (char)data_sector[i];
            }
            copied++;
        }
    }

    uint32_t terminator = copied < output_size ? copied : output_size - 1;
    output[terminator] = '\0';
    if (size_out != 0) {
        *size_out = size;
    }
    return 1;
}

int kfs_append_text(const char* name, const char* text) {
    uint32_t old_size = 0;
    uint32_t index = 0;

    if (!kfs_read_text(name, append_buffer, sizeof(append_buffer), &old_size)) {
        return kfs_save_text(name, text);
    }

    if (old_size > KFS_MAX_FILE_BYTES) {
        old_size = KFS_MAX_FILE_BYTES;
    }

    index = old_size;
    while (index < KFS_MAX_FILE_BYTES && text[index - old_size] != '\0') {
        append_buffer[index] = text[index - old_size];
        index++;
    }

    append_buffer[index] = '\0';
    return kfs_save_text(name, append_buffer);
}

int kfs_cat(const char* name) {
    uint32_t size = 0;

    if (!kfs_read_text(name, append_buffer, sizeof(append_buffer), &size)) {
        return 0;
    }

    for (uint32_t i = 0; i < size; i++) {
        terminal_putchar(append_buffer[i]);
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

    terminal_write(dir_sector[offset] == KFS_ENTRY_FLAG_DIRECTORY ? "KFS directory: " : "KFS file: ");
    terminal_write(name);
    terminal_write("\nSlot: ");
    terminal_write_dec(slot);
    terminal_write("\nDirectory sector: ");
    terminal_write_dec(dir_lba);
    if (dir_sector[offset] == KFS_ENTRY_FLAG_DIRECTORY) {
        terminal_write("\nType: directory\n");
    } else {
        terminal_write("\nData sector: ");
        uint32_t data_lba = read_u32(dir_sector, offset + 24);
        uint32_t size = read_u32(dir_sector, offset + 28);
        terminal_write_dec(data_lba);
        terminal_write("\nSize: ");
        terminal_write_dec(size);
        terminal_write(" bytes\nSectors: ");
        terminal_write_dec(sectors_for_size(size));
        terminal_write("\n");
    }
    return 1;
}

int kfs_remove(const char* name) {
    struct kfs_superblock superblock;
    uint8_t dir_sector[KFS_SECTOR_SIZE];
    uint8_t scan_sector[KFS_SECTOR_SIZE];
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

    if (dir_sector[offset] == KFS_ENTRY_FLAG_DIRECTORY) {
        uint32_t length = string_length(name);

        for (uint32_t scan_slot = 0; scan_slot < max_entries(); scan_slot++) {
            uint32_t scan_lba = entry_sector_lba(scan_slot);
            uint32_t scan_offset = entry_sector_offset(scan_slot);

            if (!ata_read_data_sector(scan_lba, scan_sector)) {
                terminal_write("kfsrm: failed to scan directory\n");
                return 0;
            }

            if (scan_sector[scan_offset] == 0) {
                continue;
            }

            char child[KFS_ENTRY_NAME_SIZE];
            entry_read_name(scan_sector, scan_offset, child);
            if (scan_slot != slot && string_length(child) > length &&
                    child[length] == '/' && string_equals(name, child) == 0) {
                int prefix = 1;
                for (uint32_t i = 0; i < length; i++) {
                    if (child[i] != name[i]) {
                        prefix = 0;
                        break;
                    }
                }

                if (prefix) {
                    terminal_write("kfsrm: directory not empty\n");
                    return 0;
                }
            }
        }
    } else {
        uint32_t data_lba = read_u32(dir_sector, offset + 24);
        uint32_t size = read_u32(dir_sector, offset + 28);
        if (!clear_data_chain(data_lba, sectors_for_size(size), &superblock)) {
            terminal_write("kfsrm: failed to clear data\n");
            return 0;
        }
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
    terminal_write("\nBitmap sector: ");
    terminal_write_dec(KFS_BITMAP_LBA);
    terminal_write("\nData start: ");
    terminal_write_dec(read_u32(sector, 24));
    terminal_write("\n");
    return 1;
}
