/*
 * Copyright 2003-2015 (C) Raster Software Vigo (Sergio Costas)
 * This file is part of FBZX
 *
 * FBZX is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * FBZX is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <sstream>

#include "emulator.hh"
#include "llscreen.hh"
#include "menus.hh"
#include "microdrive.hh"
#include "spk_ay.hh"
#include "tape.hh"
#include "computer.hh"
#include "keyboard.hh"

#include "llsound.hh"
#include "z80free/Z80free.h"

class computer *ordenador;

computer::computer() {

	int bucle;

	this->dblscan = false;
	this->bw = false;

	this->page48k = 0;
	this->bus_counter = 0;
	this->port254 = 0;
	this->issue = 3;
	this->mode128k = 0;
	this->turbo = false;
	this->sound_bit = 0;

	this->tape_write = false;
	this->tape_fast_load = true; // fast load by default

	this->other_ret = 0;

	for (bucle = 0; bucle < 16; bucle++)
		this->ay_registers[bucle] = 0;

	this->ay_emul = 0;
	this->aych_a = 0;
	this->aych_b = 0;
	this->aych_c = 0;
	this->aych_n = 0;
	this->aych_envel = 0;
	this->vol_a = 0;
	this->vol_b = 0;
	this->vol_c = 0;
	this->tst_ay = 0;
	this->tst_ay2 = 0;

	this->ayval_a = 0;
	this->ayval_b = 0;
	this->ayval_c = 0;
	this->ayval_n = 0;
	this->ay_envel_value = 0;
	this->ay_envel_way = 0;

	this->contended_zone = false;
	this->no_contention = false;
	this->cicles_counter=0;

	this->tstados_counter_sound = 0;
	this->num_buff = 0;	// first buffer
	this->sound_cuantity = 0;
	this->sound_current_value = 0;
	this->interr = 0;
	OOTape->register_signal("pause_tape",this);
	OOTape->register_signal("pause_tape_48k",this);
}

computer::~computer() {
}

bool computer::callback_receiver(string signal_received, class Signals *object) {

	if (signal_received == "pause_tape") {
		OOTape->set_pause(true);
		return true;
	}
	if (signal_received == "pause_tape_48k") {
		if ((this->mode128k == 0) || ((this->mport1 & 0x20) != 0)) {
			OOTape->set_pause(true);
		}
		return true;
	}
	return true;
}

/* Returns the bus value when reading a port without a periferial */

byte computer::bus_empty () {

	if (ordenador->mode128k != 3)
		return (screen->bus_value);
	else
		return (255);	// +2A and +3 returns always 255
}

/* calls all the routines that emulates the computer, runing them for 'tstados'
   tstates */

void computer::emulate (int tstados) {

	screen->show_screen (tstados);
	play_ay (tstados);
	play_sound (tstados);
	OOTape->play(tstados);
	microdrive_emulate(tstados);

	if (!OOTape->get_pause()) {
		if (OOTape->read_signal() != 0) {
			this->sound_bit = 1;
		} else {
			this->sound_bit = 0; // if not paused, asign SOUND_BIT the value of tape
		}
	}
}

// check if there's contention and waits the right number of tstates

void computer::do_contention() {

	if ((!this->contended_zone) || (this->no_contention)) {
		return;
	}

	if (this->cicles_counter<14335) {
		return;
	}

	int ccicles=(this->cicles_counter-14335)%8;

	if (ccicles>5) {
		return;
	}

	this->emulate(6-ccicles);

}

// resets the computer and loads the right ROMs

void ResetComputer () {

	static int bucle;

	Z80free_reset (&procesador);
	load_rom (ordenador->mode128k);

	// reset the AY-3-8912

	for (bucle = 0; bucle < 16; bucle++)
		ordenador->ay_registers[bucle] = 0;

	ordenador->aych_a = 0;
	ordenador->aych_b = 0;
	ordenador->aych_c = 0;
	ordenador->aych_n = 0;
	ordenador->aych_envel = 0;
	ordenador->vol_a = 0;
	ordenador->vol_b = 0;
	ordenador->vol_c = 0;

	ordenador->updown=0;
	ordenador->leftright=0;

	ordenador->mport1 = 0;
	ordenador->mport2 = 0;
	ordenador->video_offset = 0;	// video in page 9 (page 5 in 128K)
	switch (ordenador->mode128k) {
	case 0:		// 48K
		ordenador->block0 = ordenador->memoria;
		ordenador->block1 = ordenador->memoria + 131072;	// video mem. in page 9 (page 5 in 128K)
		ordenador->block2 = ordenador->memoria + 65536;	// 2nd block in page 6 (page 2 in 128K)
		ordenador->block3 = ordenador->memoria + 65536;	// 3rd block in page 7 (page 3 in 128K)
		ordenador->mport1 = 32;	// access to port 7FFD disabled
	break;
	
	case 3:		// +2A/+3
		Z80free_Out (0x1FFD, 0);
	case 1:		// 128K
	case 2:		// +2
	case 4:		// spanish 128K
		Z80free_Out (0x7FFD, 0);
	break;
	}
	keyboard->reset();
	screen->reset(ordenador->mode128k);
	microdrive_reset();
}

void Z80free_Wr (register word Addr, register byte Value) {

	switch (Addr & 0xC000) {
	case 0x0000:
	// only writes in the first 16K if we are in +3 mode and bit0 of mport2 is 1

		if ((ordenador->mode128k == 3) && (1 == (ordenador->mport2 & 0x01)))
			*(ordenador->block0 + Addr) = (unsigned char) Value;
	break;

	case 0x4000:
		ordenador->do_contention();
		*(ordenador->block1 + Addr) = (unsigned char) Value;
	break;
	
	case 0x8000:
		*(ordenador->block2 + Addr) = (unsigned char) Value;
	break;
	
	case 0xC000:
		*(ordenador->block3 + Addr) = (unsigned char) Value;
	break;
	}
}


byte Z80free_Rd (register word Addr) {

	if((ordenador->mdr_active)&&(ordenador->mdr_paged)&&(Addr<8192)) // Interface I
		return((byte)ordenador->shadowrom[Addr]);
	
	switch (ordenador->other_ret) {
	case 1:
		ordenador->other_ret = 0;
		return (201);	// RET instruction
	break;

	default:
		switch (Addr & 0xC000) {
		case 0x0000:
			return ((byte) (*(ordenador->block0 + Addr)));
		break;

		case 0x4000:
			ordenador->do_contention();
			return ((byte) (*(ordenador->block1 + Addr)));
		break;

		case 0x8000:
			return ((byte) (*(ordenador->block2 + Addr)));
		break;

		case 0xC000:
			return ((byte) (*(ordenador->block3 + Addr)));
		break;

		default:
			printf ("Memory error\n");
			exit (1);
			return 0;
		}
		break;
	}
}

void Z80free_Out (register word Port, register byte Value) {

	// Microdrive access
	
	register word maskport;
	
	if (((Port&0x0001)==0)||((Port>=0x4000)&&(Port<0x8000))) {
		ordenador->do_contention();
	}

	// ULAPlus
	if (Port == 0xBF3B) {
		ordenador->do_contention();
		screen->set_ulaplus_register(Value);
		return;
	}
	if (Port == 0xFF3B) {
		ordenador->do_contention();
		screen->set_ulaplus_value(Value);
	}

	if(((Port &0x0018)!=0x0018)&&(ordenador->mdr_active))
		microdrive_out(Port,Value);
	
	// ULA port (A0 low)

	if (!(Port & 0x0001)) {
		ordenador->port254 = (unsigned char) Value;
		screen->border = ((((unsigned char) Value) & 0x07));

		if (OOTape->get_pause()) {
			if (Value & 0x10)
				ordenador->sound_bit = 1;
			else
				ordenador->sound_bit = 0;	// assign to SOUND_BIT the value
		}
	}

	// Memory page (7FFD & 1FFD)

	if (ordenador->mode128k==3) {
		maskport=0x0FFD;
	} else {
		maskport=0x3FFD;
	}
	
	if (((Port|maskport) == 0x7FFD) && (0 == (ordenador->mport1 & 0x20))) {
		ordenador->mport1 = (unsigned char) Value;
		screen->set_memory_pointers ();	// set the pointers
	}

	if (((Port|maskport) == 0x1FFD) && (0 == (ordenador->mport1 & 0x20))) {
		ordenador->mport2 = (unsigned char) Value;
		screen->set_memory_pointers ();	// set the pointers
	}

	// Sound chip (AY-3-8912)

	if (((Port|maskport) == 0xFFFD)&&(ordenador->ay_emul))
		ordenador->ay_latch = ((unsigned int) (Value & 0x0F));

	if (((Port|maskport) == 0xBFFD)&&(ordenador->ay_emul)) {
		ordenador->ay_registers[ordenador->ay_latch] = (unsigned char) Value;
		if (ordenador->ay_latch == 13)
			ordenador->ay_envel_way = 2;	// start cycle
	}
}


byte Z80free_In (register word Port) {

	static unsigned int temporal_io;
	byte pines;

	if (((Port&0x0001)==0)||((Port>=0x4000)&&(Port<0x8000))) {
		ordenador->do_contention();
	}

	temporal_io = (unsigned int) Port;

	if (Port == 0xFF3B) {
		ordenador->do_contention();
		return (screen->read_ulaplus_value());
	}

	if (!(temporal_io & 0x0001)) {
		pines = 0xBF;	// by default, sound bit is 0
		if (!(temporal_io & 0x0100))
			pines &= keyboard->s8;
		if (!(temporal_io & 0x0200))
			pines &= keyboard->s9;
		if (!(temporal_io & 0x0400))
			pines &= keyboard->s10;
		if (!(temporal_io & 0x0800))
			pines &= keyboard->s11;
		if (!(temporal_io & 0x1000))
			pines &= keyboard->s12;
		if (!(temporal_io & 0x2000))
			pines &= keyboard->s13;
		if (!(temporal_io & 0x4000))
			pines &= keyboard->s14;
		if (!(temporal_io & 0x8000))
			pines &= keyboard->s15;

		if (OOTape->get_pause()) {
			if (ordenador->issue == 2)	{
				if (ordenador->port254 & 0x18)
					pines |= 0x40;
			} else {
				if (ordenador->port254 & 0x10)
					pines |= 0x40;
			}
			if (random()<(RAND_MAX/200)) { // add tape noise when paused
				pines |= 0x40;
			}
		} else {
			if (OOTape->read_signal() != 0)
				pines |= 0x40;	// sound input
			else
				pines &= 0xBF;	// sound input
		}
		return (pines);
	}

	// Joystick
	if (!(temporal_io & 0x0020)) {
		if (keyboard->joystick == 1) {
			return (keyboard->js);
		} else {
			return 0; // if Kempston is not selected, emulate it, but always 0
		}
	}

	if ((temporal_io == 0xFFFD)&&(ordenador->ay_emul))
		return (ordenador->ay_registers[ordenador->ay_latch]);

	// Microdrive access
	
	if(((Port &0x0018)!=0x0018)&&(ordenador->mdr_active))
		return(microdrive_in(Port));
	
	pines=ordenador->bus_empty();

	return (pines);
}
