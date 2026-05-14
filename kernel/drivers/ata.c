#include "ata.h"

#include "io.h"
#include "terminal.h"

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6
#define ATA_REG_DATA 0
#define ATA_REG_SECCOUNT0 2
#define ATA_REG_LBA0 3
#define ATA_REG_LBA1 4
#define ATA_REG_LBA2 5
#define ATA_REG_HDDEVSEL 6
#define ATA_REG_COMMAND 7
#define ATA_REG_STATUS 7
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_BSY 0x80
#define ATA_PRIMARY_SLAVE 0xB0
#define ATA_PRIMARY_SLAVE_LBA 0xF0
#define ATA_WAIT_LIMIT 1000000

static uint8_t ata_read_status(void) {
    return inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
}

static void ata_delay(void) {
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_CTRL);
    }
}

static int ata_wait_not_busy(void) {
    uint8_t status = ata_read_status();

    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        if (!(status & ATA_STATUS_BSY)) {
            return 1;
        }

        status = ata_read_status();
    }

    return 0;
}

static int ata_wait_for_drq(void) {
    uint8_t status = ata_read_status();

    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        if (status & ATA_STATUS_ERR) {
            return 0;
        }

        if ((status & ATA_STATUS_DRQ) && !(status & ATA_STATUS_BSY)) {
            return 1;
        }

        status = ata_read_status();
    }

    return 0;
}

static void ata_copy_swapped_string(char* output, const uint16_t* identify,
        uint32_t start_word, uint32_t word_count, uint32_t output_size) {
    uint32_t index = 0;

    if (output_size == 0) {
        return;
    }

    for (uint32_t word = 0; word < word_count && index < output_size - 1; word++) {
        uint16_t value = identify[start_word + word];
        output[index++] = (char)(value >> 8);
        if (index < output_size - 1) {
            output[index++] = (char)(value & 0xFF);
        }
    }

    while (index > 0 && output[index - 1] == ' ') {
        index--;
    }

    output[index] = '\0';
}

int ata_identify_data_disk(struct ata_device_info* info) {
    uint16_t identify[256];

    if (info == 0) {
        return 0;
    }

    info->present = 0;
    info->sectors = 0;
    info->serial[0] = '\0';
    info->model[0] = '\0';

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, ATA_PRIMARY_SLAVE);
    ata_delay();
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    uint8_t status = ata_read_status();
    if (status == 0) {
        return 0;
    }

    if (!ata_wait_not_busy()) {
        return 0;
    }

    if (inb(ATA_PRIMARY_IO + ATA_REG_LBA1) != 0 || inb(ATA_PRIMARY_IO + ATA_REG_LBA2) != 0) {
        return 0;
    }

    if (!ata_wait_for_drq()) {
        return 0;
    }

    for (uint32_t i = 0; i < 256; i++) {
        identify[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }

    info->present = 1;
    info->sectors = ((uint32_t)identify[61] << 16) | identify[60];
    ata_copy_swapped_string(info->serial, identify, 10, 10, sizeof(info->serial));
    ata_copy_swapped_string(info->model, identify, 27, 20, sizeof(info->model));
    return 1;
}

void ata_print_data_disk_info(void) {
    struct ata_device_info info;

    if (!ata_identify_data_disk(&info)) {
        terminal_write("Data disk: not detected on ATA primary slave\n");
        return;
    }

    terminal_write("Data disk: ATA primary slave\n");
    terminal_write("Model: ");
    terminal_write(info.model[0] == '\0' ? "(unknown)" : info.model);
    terminal_write("\nSerial: ");
    terminal_write(info.serial[0] == '\0' ? "(unknown)" : info.serial);
    terminal_write("\nSectors: ");
    terminal_write_dec(info.sectors);
    terminal_write("\nBytes: ");
    terminal_write_dec(info.sectors * 512);
    terminal_write("\n");
}

int ata_read_data_sector(uint32_t lba, uint8_t* buffer) {
    struct ata_device_info info;

    if (buffer == 0) {
        return 0;
    }

    if (!ata_identify_data_disk(&info)) {
        return 0;
    }

    if (lba >= info.sectors || lba >= 0x10000000) {
        return 0;
    }

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL,
        (uint8_t)(ATA_PRIMARY_SLAVE_LBA | ((lba >> 24) & 0x0F)));
    ata_delay();
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
    ata_delay();

    if (!ata_wait_not_busy() || !ata_wait_for_drq()) {
        return 0;
    }

    for (uint32_t i = 0; i < 256; i++) {
        uint16_t word = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        buffer[i * 2] = (uint8_t)(word & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }

    return 1;
}

int ata_write_data_sector(uint32_t lba, const uint8_t* buffer) {
    struct ata_device_info info;

    if (buffer == 0) {
        return 0;
    }

    if (!ata_identify_data_disk(&info)) {
        return 0;
    }

    if (lba >= info.sectors || lba >= 0x10000000) {
        return 0;
    }

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL,
        (uint8_t)(ATA_PRIMARY_SLAVE_LBA | ((lba >> 24) & 0x0F)));
    ata_delay();
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
    ata_delay();

    if (!ata_wait_not_busy() || !ata_wait_for_drq()) {
        return 0;
    }

    for (uint32_t i = 0; i < 256; i++) {
        uint16_t word = (uint16_t)buffer[i * 2] |
            ((uint16_t)buffer[i * 2 + 1] << 8);
        outw(ATA_PRIMARY_IO + ATA_REG_DATA, word);
    }

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_delay();

    return ata_wait_not_busy();
}

static void ata_print_hex_byte(uint8_t value) {
    const char* digits = "0123456789ABCDEF";

    terminal_putchar(digits[(value >> 4) & 0x0F]);
    terminal_putchar(digits[value & 0x0F]);
}

int ata_print_data_sector(uint32_t lba) {
    uint8_t sector[512];

    if (!ata_read_data_sector(lba, sector)) {
        terminal_write("diskread: unable to read sector\n");
        return 0;
    }

    terminal_write("Sector ");
    terminal_write_dec(lba);
    terminal_write(":\n");

    for (uint32_t row = 0; row < 32; row++) {
        terminal_write_hex(row * 16);
        terminal_write("  ");

        for (uint32_t col = 0; col < 16; col++) {
            ata_print_hex_byte(sector[row * 16 + col]);
            terminal_putchar(' ');
        }

        terminal_putchar(' ');
        for (uint32_t col = 0; col < 16; col++) {
            uint8_t value = sector[row * 16 + col];
            terminal_putchar(value >= 32 && value <= 126 ? (char)value : '.');
        }

        terminal_putchar('\n');
    }

    return 1;
}

int ata_write_text_sector(uint32_t lba, const char* text) {
    uint8_t sector[512];
    uint32_t index = 0;

    for (uint32_t i = 0; i < sizeof(sector); i++) {
        sector[i] = 0;
    }

    while (text[index] != '\0' && index < sizeof(sector)) {
        sector[index] = (uint8_t)text[index];
        index++;
    }

    return ata_write_data_sector(lba, sector);
}
