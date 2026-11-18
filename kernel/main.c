#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "limine.h"

#define UART_COM1 0x3f8

struct colour {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[3] = LIMINE_BASE_REVISION(6);
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
        .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
        .revision = 0,
    };
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
        .id = LIMINE_MEMMAP_REQUEST_ID,
        .revision = 0,
    };
__attribute__((used, section(".limine_requests")))
static volatile struct limine_firmware_type_request firmware_request = {
        .id = LIMINE_FIRMWARE_TYPE_REQUEST_ID,
        .revision = 0,
    };
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[4] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[2] = LIMINE_REQUESTS_END_MARKER;

static struct limine_framebuffer *screen;
static bool serial_ready;
static uint64_t usable_memory;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static bool serial_detect(void) {
    outb(UART_COM1 + 7, 0x5a);
    if (inb(UART_COM1 + 7) != 0x5a) {
        return false;
    }

    outb(UART_COM1 + 7, 0xa5);
    return inb(UART_COM1 + 7) == 0xa5;
}

static void serial_init(void) {
    outb(UART_COM1 + 1, 0x00);
    outb(UART_COM1 + 3, 0x80);
    outb(UART_COM1, 0x01);
    outb(UART_COM1 + 1, 0x00);
    outb(UART_COM1 + 3, 0x03);
    outb(UART_COM1 + 2, 0xc7);
    outb(UART_COM1 + 4, 0x0b);
    serial_ready = true;
}

static void serial_putc(char value) {
    if (!serial_ready) {
        return;
    }
    for (uint32_t timeout = 0; timeout < 1000000; timeout++) {
        if ((inb(UART_COM1 + 5) & 0x20) != 0) {
            outb(UART_COM1, (uint8_t)value);
            return;
        }
    }
}

static void serial_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}

static void serial_number(uint64_t value) {
    char digits[21];
    size_t count = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    while (count != 0) {
        serial_putc(digits[--count]);
    }
}

static void serial_line(char *line, size_t capacity) {
    size_t length = 0;

    for (;;) {
        if ((inb(UART_COM1 + 5) & 1) == 0) {
            __asm__ volatile ("pause");
            continue;
        }

        char value = (char)inb(UART_COM1);
        if (value == '\r' || value == '\n') {
            line[length] = '\0';
            serial_write("\n");
            return;
        }
        if ((value == '\b' || value == 0x7f) && length > 0) {
            length--;
            serial_write("\b \b");
            continue;
        }
        if (value >= 32 && value < 127 && length + 1 < capacity) {
            line[length++] = value;
            serial_putc(value);
        }
    }
}

static void uppercase(char *text) {
    while (*text != '\0') {
        if (*text >= 'a' && *text <= 'z') {
            *text = (char)(*text - 'a' + 'A');
        }
        text++;
    }
}

static uint64_t memory_total(void) {
    struct limine_memmap_response *response = memmap_request.response;
    uint64_t total = 0;
    if (response == NULL) {
        return 0;
    }

    for (uint64_t i = 0; i < response->entry_count; i++) {
        struct limine_memmap_entry *entry = response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            total += entry->length;
        }
    }
    return total;
}

static const char *firmware_name(void) {
    struct limine_firmware_type_response *response = firmware_request.response;
    if (response == NULL) {
        return "UNKNOWN";
    }
    if (response->firmware_type == LIMINE_FIRMWARE_TYPE_X86BIOS) {
        return "LEGACY BIOS";
    }
    if (response->firmware_type == LIMINE_FIRMWARE_TYPE_UEFI64) {
        return "UEFI 64 BIT";
    }
    if (response->firmware_type == LIMINE_FIRMWARE_TYPE_UEFI32) {
        return "UEFI 32 BIT";
    }
    return "UNKNOWN";
}

static uint32_t pixel_colour(struct limine_framebuffer *fb, struct colour colour) {
    uint32_t red = (uint32_t)(((uint64_t)colour.red * ((1ull << fb->red_mask_size) - 1) / 255)
        << fb->red_mask_shift);
    uint32_t green = (uint32_t)(((uint64_t)colour.green * ((1ull << fb->green_mask_size) - 1) / 255)
        << fb->green_mask_shift);
    uint32_t blue = (uint32_t)(((uint64_t)colour.blue * ((1ull << fb->blue_mask_size) - 1) / 255)
        << fb->blue_mask_shift);
    return red | green | blue;
}

static void put_pixel(int32_t x, int32_t y, struct colour colour) {
    if (screen == NULL || (uint32_t)x >= screen->width || (uint32_t)y >= screen->height) {
        return;
    }
    volatile uint32_t *pixels = (volatile uint32_t *)screen->address;
    pixels[((uint64_t)y * screen->pitch / 4) + (uint32_t)x] = pixel_colour(screen, colour);
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                      struct colour colour) {
    if (screen == NULL || x >= screen->width || y >= screen->height) {
        return;
    }
    if (width > screen->width - x) {
        width = (uint32_t)screen->width - x;
    }
    if (height > screen->height - y) {
        height = (uint32_t)screen->height - y;
    }
    uint32_t packed = pixel_colour(screen, colour);
    volatile uint32_t *pixels = (volatile uint32_t *)screen->address;
    for (uint32_t row = 0; row < height; row++) {
        uint64_t offset = ((uint64_t)(y + row) * screen->pitch / 4) + x;
        for (uint32_t column = 0; column < width; column++) {
            pixels[offset + column] = packed;
        }
    }
}

static const uint8_t font[36][5] = {
    { 0x7e, 0x11, 0x11, 0x11, 0x7e }, { 0x7f, 0x49, 0x49, 0x49, 0x36 },
    { 0x3e, 0x41, 0x41, 0x41, 0x22 }, { 0x7f, 0x41, 0x41, 0x22, 0x1c },
    { 0x7f, 0x49, 0x49, 0x49, 0x41 }, { 0x7f, 0x09, 0x09, 0x09, 0x01 },
    { 0x3e, 0x41, 0x49, 0x49, 0x7a }, { 0x7f, 0x08, 0x08, 0x08, 0x7f },
    { 0x00, 0x41, 0x7f, 0x41, 0x00 }, { 0x20, 0x40, 0x41, 0x3f, 0x01 },
    { 0x7f, 0x08, 0x14, 0x22, 0x41 }, { 0x7f, 0x40, 0x40, 0x40, 0x40 },
    { 0x7f, 0x02, 0x0c, 0x02, 0x7f }, { 0x7f, 0x04, 0x08, 0x10, 0x7f },
    { 0x3e, 0x41, 0x41, 0x41, 0x3e }, { 0x7f, 0x09, 0x09, 0x09, 0x06 },
    { 0x3e, 0x41, 0x51, 0x21, 0x5e }, { 0x7f, 0x09, 0x19, 0x29, 0x46 },
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, { 0x01, 0x01, 0x7f, 0x01, 0x01 },
    { 0x3f, 0x40, 0x40, 0x40, 0x3f }, { 0x1f, 0x20, 0x40, 0x20, 0x1f },
    { 0x7f, 0x20, 0x18, 0x20, 0x7f }, { 0x63, 0x14, 0x08, 0x14, 0x63 },
    { 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 },
    { 0x3e, 0x51, 0x49, 0x45, 0x3e }, { 0x00, 0x42, 0x7f, 0x40, 0x00 },
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4b, 0x31 },
    { 0x18, 0x14, 0x12, 0x7f, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
    { 0x3c, 0x4a, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1e },
};

static const uint8_t *glyph_for(char value) {
    if (value >= 'a' && value <= 'z') {
        value = (char)(value - 'a' + 'A');
    }
    if (value >= 'A' && value <= 'Z') {
        return font[value - 'A'];
    }
    if (value >= '0' && value <= '9') {
        return font[26 + value - '0'];
    }
    return NULL;
}

static void draw_text(uint32_t x, uint32_t y, const char *text, uint32_t scale,
                      struct colour colour) {
    while (*text != '\0') {
        const uint8_t *glyph = glyph_for(*text);
        if (glyph != NULL) {
            for (uint32_t column = 0; column < 5; column++) {
                for (uint32_t row = 0; row < 7; row++) {
                    if ((glyph[column] & (1u << row)) != 0) {
                        fill_rect(x + column * scale, y + row * scale, scale, scale, colour);
                    }
                }
            }
        } else if (*text == '.') {
            fill_rect(x + scale, y + 6 * scale, scale, scale, colour);
        } else if (*text == ',') {
            fill_rect(x + scale, y + 5 * scale, scale, 2 * scale, colour);
        } else if (*text == '-') {
            fill_rect(x + scale, y + 3 * scale, 3 * scale, scale, colour);
        } else if (*text == ':') {
            fill_rect(x + scale, y + scale, scale, scale, colour);
            fill_rect(x + scale, y + 5 * scale, scale, scale, colour);
        } else if (*text == '/') {
            for (uint32_t row = 0; row < 7; row++) {
                fill_rect(x + (5 * row / 7) * scale, y + row * scale, scale, scale, colour);
            }
        }
        x += 6 * scale;
        text++;
    }
}

static void draw_number(uint32_t x, uint32_t y, uint64_t value, uint32_t scale,
                        struct colour colour) {
    char digits[21];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        char digit[2] = { digits[--count], '\0' };
        draw_text(x, y, digit, scale, colour);
        x += 6 * scale;
    }
}

static void draw_logo(uint32_t x, uint32_t y, uint32_t size) {
    const struct colour cyan = { 91, 246, 209 };
    const struct colour blue = { 112, 137, 255 };
    for (uint32_t i = 0; i < size; i++) {
        for (int32_t thickness = -2; thickness <= 2; thickness++) {
            put_pixel((int32_t)(x + i), (int32_t)(y + i) + thickness, cyan);
            put_pixel((int32_t)(x + size - 1 - i), (int32_t)(y + i) + thickness, blue);
        }
    }
}

static void draw_screen(void) {
    if (screen == NULL || screen->width < 640 || screen->height < 400) {
        return;
    }

    const struct colour background = { 8, 13, 25 };
    const struct colour surface = { 16, 24, 40 };
    const struct colour border = { 35, 49, 70 };
    const struct colour white = { 237, 244, 255 };
    const struct colour muted = { 133, 151, 177 };
    const struct colour cyan = { 91, 246, 209 };
    const struct colour blue = { 112, 137, 255 };

    uint32_t width = (uint32_t)screen->width;
    uint32_t height = (uint32_t)screen->height;
    uint32_t margin = width / 16;
    uint32_t card_y = height / 2 + 18;
    uint32_t card_bottom = height - 52;
    uint32_t card_height = card_bottom - card_y;

    fill_rect(0, 0, width, height, background);
    fill_rect(0, 0, width, 5, cyan);
    draw_logo(margin, 22, 40);
    draw_text(margin + 52, 28, "XPERZ OS", 2, white);
    draw_text(margin + 52, 49, "KERNEL EDITION", 1, muted);
    fill_rect(margin, 78, width - 2 * margin, 1, border);

    draw_text(margin, 104, "YOUR MACHINE, YOUR RULES", 2, cyan);
    draw_text(margin, 130, "XPERZ OS", 5, white);
    draw_text(margin, 176, "OPEN SOURCE KERNEL FOUNDATION", 2, muted);

    fill_rect(margin, card_y, width - 2 * margin, card_height, surface);
    fill_rect(margin, card_y, width - 2 * margin, 1, border);
    fill_rect(margin, card_bottom - 1, width - 2 * margin, 1, border);
    draw_text(margin + 20, card_y + 18, "SYSTEM OVERVIEW", 1, muted);
    fill_rect(margin + 20, card_y + 34, width - 2 * margin - 40, 1, border);

    uint32_t left_column = margin + 20;
    uint32_t right_column = width / 2 + 12;
    uint32_t first_row = card_y + 50;
    uint32_t second_row = card_y + card_height / 2 + 5;

    draw_text(left_column, first_row, "FIRMWARE", 1, muted);
    draw_text(left_column, first_row + 15, firmware_name(), 2, white);
    draw_text(right_column, first_row, "USABLE MEMORY", 1, muted);
    draw_number(right_column, first_row + 15, usable_memory / (1024 * 1024), 2, white);
    draw_text(right_column + 6 * 6 * 2, first_row + 15, "MB", 2, cyan);

    draw_text(left_column, second_row, "DISPLAY", 1, muted);
    draw_number(left_column, second_row + 15, screen->width, 2, white);
    draw_text(left_column + 6 * 4 * 2, second_row + 15, "X", 2, cyan);
    draw_number(left_column + 6 * 5 * 2, second_row + 15, screen->height, 2, white);
    draw_text(right_column, second_row, "SERIAL CONSOLE", 1, muted);
    draw_text(right_column, second_row + 15, serial_ready ? "COM1 READY" : "NOT DETECTED",
              2, serial_ready ? cyan : blue);

    draw_text(margin, height - 28, "TYPE HELP ON COM1 TO OPEN THE KERNEL CONSOLE", 1, muted);
}

static void print_memory(void) {
    serial_write("Usable memory: ");
    serial_number(usable_memory / (1024 * 1024));
    serial_write(" MiB\n");
}

static void shell(void) {
    char line[80];
    serial_write("\nXPERZ OS KERNEL READY\n");
    serial_write("Xperz x86_64 kernel prototype. Type HELP for commands.\n");
    print_memory();

    for (;;) {
        serial_write("\nXPERZ> ");
        serial_line(line, sizeof(line));
        uppercase(line);

        if (line[0] == '\0') {
            continue;
        }
        if (line[0] == 'H' && line[1] == 'E' && line[2] == 'L' &&
            line[3] == 'P' && line[4] == '\0') {
            serial_write("Commands: HELP ABOUT MEM CLEAR HALT\n");
        } else if (line[0] == 'A' && line[1] == 'B' && line[2] == 'O' &&
                   line[3] == 'U' && line[4] == 'T' && line[5] == '\0') {
            serial_write("Xperz OS is an experimental x86_64 kernel foundation.\n");
            serial_write("This shell runs in the kernel; it is not a full desktop OS.\n");
        } else if (line[0] == 'M' && line[1] == 'E' && line[2] == 'M' &&
                   line[3] == '\0') {
            print_memory();
        } else if (line[0] == 'C' && line[1] == 'L' && line[2] == 'E' &&
                   line[3] == 'A' && line[4] == 'R' && line[5] == '\0') {
            serial_write("\033[2J\033[H");
        } else if (line[0] == 'H' && line[1] == 'A' && line[2] == 'L' &&
                   line[3] == 'T' && line[4] == '\0') {
            serial_write("CPU halted. Reset the machine to boot again.\n");
            for (;;) {
                __asm__ volatile ("cli; hlt" : : : "memory");
            }
        } else {
            serial_write("Unknown command. Type HELP.\n");
        }
    }
}

__attribute__((noreturn))
void kmain(void) {
    if (serial_detect()) {
        serial_init();
        serial_write("Xperz kernel starting...\n");
    }

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        serial_write("ERROR: unsupported Limine base revision.\n");
        for (;;) {
            __asm__ volatile ("cli; hlt" : : : "memory");
        }
    }

    if (framebuffer_request.response != NULL &&
        framebuffer_request.response->framebuffer_count > 0) {
        struct limine_framebuffer *candidate =
            framebuffer_request.response->framebuffers[0];
        if (candidate->address != NULL && candidate->memory_model == LIMINE_FRAMEBUFFER_RGB &&
            candidate->bpp == 32 && candidate->red_mask_size < 32 &&
            candidate->green_mask_size < 32 && candidate->blue_mask_size < 32 &&
            candidate->red_mask_shift < 32 && candidate->green_mask_shift < 32 &&
            candidate->blue_mask_shift < 32 &&
            candidate->red_mask_size + candidate->red_mask_shift <= 32 &&
            candidate->green_mask_size + candidate->green_mask_shift <= 32 &&
            candidate->blue_mask_size + candidate->blue_mask_shift <= 32) {
            screen = candidate;
        }
    }

    usable_memory = memory_total();
    draw_screen();
    if (serial_ready) {
        shell();
    }
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}
