/*
 * screen.hh
 *
 *  Created on: 26/03/2015
 *      Author: raster
 */

#ifndef SRC_SCREEN_HH_
#define SRC_SCREEN_HH_

using namespace std;

#include <inttypes.h>
#include <string>

extern class Screen *screen;

class Screen {

	// screen private global variables
	unsigned int translate[6144],translate2[6144];
	unsigned int *p_translt,*p_translt2;
	unsigned char *pixel; // current address
	unsigned char *base_pixel;
	char flash;
	int currline,currpix;

	int init_line; // cuantity to add to the base address to start to paint
	int next_line; // cuantity to add when we reach the end of line to go to next line
	int next_scanline; // cuantity to add to pass to the next scanline
	int first_line; // first line to start to paint
	int last_line; // last line to paint
	int first_pixel; // first pixel of a line to paint
	int last_pixel; // last pixel of a line to paint
	int next_pixel; // next pixel
	int jump_pixel;

	int contador_flash;

	int tstados_counter; // counts tstates leaved to the next call
	int int_counter; // counts the interrupt duration
	int pixancho,pixalto; // maximum pixel value for width and height
	bool screen_snow; // 0-> no emulate snow; 1-> emulate snow

	uint8_t ulaplus_reg; // contains the last selected register in the ULAPlus

	void paint_pixels (uint8_t octet,uint8_t ink, uint8_t paper);

public:
	uint8_t border;
	bool ulaplus; // ULAPlus inactive or active
	uint8_t bus_value;

	Screen();
	void set_memory_pointers ();
	void show_screen (int tstados);
	void reset(uint8_t);
	void set_ulaplus_register(uint8_t);
	void set_ulaplus_value(uint8_t);
	uint8_t read_ulaplus_value();
};


#endif /* SRC_SCREEN_HH_ */
