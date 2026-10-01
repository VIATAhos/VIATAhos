#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stdbool.h>

void vga_init(void);
void vga_clear(void);
void vga_flush(void);
void vga_putchar(char c);
void vga_print(const char* str);
void vga_set_color(uint8_t fg, uint8_t bg);
uint8_t vga_get_color();

void vga_set_banner(const char* line1, const char* line2, const char* line3);
void vga_disable_banner(void);

void vga_switch_session(int session);
void vga_scroll_view(int lines);

void vga_recolor_screen(void);
void vga_disable_blink(void);

void vga_screen_off(void);
void vga_screen_on(void);

#endif