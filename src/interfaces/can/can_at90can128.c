/*
Cubesat Space Protocol - A small network-layer protocol designed for Cubesats
Copyright (C) 2010 GomSpace ApS (gomspace.com)
Copyright (C) 2010 AAUSAT3 Project (aausat3.space.aau.dk)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

/* AT90CAN128 driver */

#include <stdio.h>
#include <inttypes.h>

#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <csp/interfaces/csp_if_can.h>

#include "can.h"
#include "can_at90can128.h"

/* MOB segmentation */
#define CAN_RX_MOBS 7
#define CAN_TX_MOBS 8
#define CAN_MOBS	15

/** Callback functions */
can_tx_callback_t txcb;
can_rx_callback_t rxcb;

/** Identifier and mask */
uint32_t can_id;
uint32_t can_mask;

void can_configure_mobs(void) {

	int mob;

	/* Initialize mobs */
	for (mob = 0; mob < CAN_MOBS; mob++) {
		/* Set MOB */
		CAN_SET_MOB(mob);
		CAN_CLEAR_MOB();

		/* Only RX mobs require special initialization */
		if (mob >= CAN_TX_MOBS) {
			/* Set id and mask */
			CAN_SET_EXT_ID(can_id);
			CAN_SET_EXT_MSK(can_mask);

			/* Accept extended frames */
			CAN_CLEAR_IDEMSK();
			CAN_CONFIG_RX();
		}

	}
}

int can_configure_bitrate(unsigned long int afcpu, uint32_t bps) {

	/* Set baud rate (500 kbps) */
	if (bps != 500000) {
		printf_P(PSTR("CAN bitrate must be 500000 (was %d)\r\n"));
		return -1;
	}

	/* TODO: Set arbitrary bitrate */
	if (afcpu == 8000000) {
		CANTCON = 0x02;      // Set CAN Timer Prescaler
		CANBT1 = 0x02;       //!< Tscl  = 2x Tclkio = 250 ns
		CANBT2 = 0x04;       //!< Tsync = 1x Tscl, Tprs = 3x Tscl, Tsjw = 1x Tscl
		CANBT3 = 0x13;       //!< Tpsh1 = 2x Tscl, Tpsh2 = 2x Tscl, 3 sample points
	} else if (afcpu == 12000000) {
		CANTCON = 0x02;      // Set CAN Timer Prescaler
		CANBT1 = 0x02;       //!< Tscl  = 2x Tclkio = 166.666 ns
		CANBT2 = 0x08;       //!< Tsync = 1x Tscl, Tprs = 5x Tscl, Tsjw = 1x Tscl
		CANBT3 = 0x25;       //!< Tpsh1 = 3x Tscl, Tpsh2 = 3x Tscl, 3 sample points
	} else if (afcpu == 16000000) {
		CANTCON = 128;       // Set CAN Timer Prescaler
		CANBT1 = 0x06;       //!< Tscl  = 4x Tclkio = 250 ns
		CANBT2 = 0x04;       //!< Tsync = 1x Tscl, Tprs = 3x Tscl, Tsjw = 1x Tscl
		CANBT3 = 0x13;       //!< Tpsh1 = 2x Tscl, Tpsh2 = 2x Tscl, 3 sample points
	} else {
		printf_P(PSTR("Error, missing CAN driver defines for that FCPU=%d\r\n"), afcpu);
		return -1;
	}

	return 0;

}

int can_init(uint32_t id, uint32_t mask, can_tx_callback_t atxcb, can_rx_callback_t arxcb, void * conf, int conflen) {

	uint32_t bitrate;
	uint32_t clock_speed;
	struct can_at90can128_conf * can_conf;

	/* Validate config size */
	if (conf != NULL && conflen > 0) {
		if (conflen != sizeof(struct can_at90can128_conf)) {
			return -1;
		} else {
			can_conf = (struct can_at90can128_conf *)conf;
			bitrate = can_conf->bitrate;
			clock_speed = can_conf->clock_speed;
		}
	}

	/* Set id and mask */
	can_id = id;
	can_mask = mask;

	/* Set callbacks */
	txcb = atxcb;
	rxcb = arxcb;

	/* Configure CAN module */
	CAN_DISABLE();
	CAN_RESET();

	/* Enables mob 0-14 interrupts */
	CANIE1 = 0x7F;
	CANIE2 = 0xFF;

	/* Configure bitrate */
	can_configure_bitrate(fcpu, bitrate);

	/* Configure MOBS */
	can_configure_mobs();

	/* Enable and return */
	CAN_ENABLE();
	CAN_SET_INTERRUPT();

	return 0;

}

int can_mbox_get(void) {

	/* Disable CAN interrupt */
	CAN_CLEAR_INTERRUPT();

	uint8_t i;

	/* Search free MOB from 0 -> CAN_TX_MOBs */
	for(i = 0; i < CAN_TX_MOBS; i++) {
		if ((CANEN2 & (1 << i)) == 0) {
			/* TODO: Mark mbox used in a locked array */
			CAN_SET_INTERRUPT();
			return i;
		}
	}
	
	printf_P(PSTR("TX overflow, no available MOB\r\n"));

	/* Enable CAN interrupt */
	CAN_SET_INTERRUPT();

	return -1;

}

int can_mbox_data(int m, can_id_t id, uint8_t data[], uint8_t dlc) {

    int i;

	/* Select and clear mob */
	CAN_SET_MOB(m);
	CAN_MOB_ABORT();
	CAN_CLEAR_STATUS_MOB();

	/* Set identifier */
	CAN_SET_EXT_ID(id);

	/* Set data */
    for (i = 0; i < dlc; i++)
        CANMSG = data[i];

    /* Set DLC */
    CAN_CLEAR_DLC();
    CAN_SET_DLC(dlc);

    return 0;

}

int can_mbox_send(int m) {

	/* Start TX */
	CAN_CONFIG_TX();
	return 0;

}

int can_mbox_release(int mbox) {

	/* No action required */
	return 0;

}

/** Find Oldest MOB
 * Searches the CAN timestamp and returns the
 * can-mob with the oldest timestamp.
 */
static inline uint8_t can_find_oldest_mob(void) {

	static uint8_t mob, mob_winner;
	static uint16_t mob_time;
	static uint32_t time;
	static uint16_t diff, diff_highest;

	diff_highest = 0;
	for (mob = 0; mob <= 14; mob++) {
		CAN_SET_MOB(mob);
		if (CANSTMOB) {
			time = CANTIMH << 8 | CANTIML;
			mob_time = CANSTMH << 8 | CANSTML;

			/* Check for overflow */
			if (mob_time > time) {
				diff = (time + 0xFFFF) - mob_time;				
			} else {
				diff = time - mob_time;
			}
			if (diff >= diff_highest) {
				mob_winner = mob;
				diff_highest = diff;
			}
		}
	}

	return mob_winner;

}

/** Can interrupt vector 
 * This is the heart of the driver
 * In here frames are accepted and the hardware is freed
 * before returning from the ISR the frame is passed to the 
 * lower handlers of the protocol layer through a callback.
 */
ISR(CANIT_vect) {

	static uint8_t mob;
	static portBASE_TYPE xTaskWoken;
	
	xTaskWoken = pdFALSE;

	/* For each MOB that has interrupted */
	while (CAN_HPMOB() != 0xF) {

		/* Locate MOB */
		mob = can_find_oldest_mob();
		CAN_SET_MOB(mob);

		if (CANSTMOB & ERR_MOB_MSK) {
			/* Clear status */
			CAN_CLEAR_STATUS_MOB();

			/* Error */
			CAN_MOB_ABORT();

			/* Remember to re-enable RX */
			if (mob >= CAN_TX_MOBS)
				CAN_CONFIG_RX();

		} else if (CANSTMOB & MOB_RX_COMPLETED) {
			/* RX Complete */
			int i;
			can_frame_t frame;

			/* Clear status */
			CAN_CLEAR_STATUS_MOB();

			if (mob == CAN_MOBS - 1) {
				/* RX overflow */
				printf_P(PSTR("RX Overflow!\r\n"));
				CAN_DISABLE();
				can_configure_mobs();
				CAN_ENABLE();
			}

			/* Read DLC */
			frame.dlc = CAN_GET_DLC();

			/* Read data */
			for (i = 0; i < frame.dlc; i++)
				frame.data[i] = CANMSG;

			/* Read identifier */
			CAN_GET_EXT_ID(frame.id);

			/* Do RX-Callback */
			if (rxcb != NULL) rxcb(&frame, &xTaskWoken);

			/* The callback handler might have changed active mob */
			CAN_SET_MOB(mob);
			CAN_CONFIG_RX();

		} else if (CANSTMOB & MOB_TX_COMPLETED) {
			/* TX Complete */
			can_id_t id;

			/* Clear status */
			CAN_CLEAR_STATUS_MOB();

			/* Read identifier */
			CAN_GET_EXT_ID(id);

			/* Do TX-Callback */
			if (txcb != NULL) txcb(id, &xTaskWoken);

		} else {
			printf_P(PSTR("Unknown status: %#x\r\n"), CANSTMOB);

			/* Unknown interrupt */
			CAN_CLEAR_STATUS_MOB();
			CAN_MOB_ABORT();

		}

	}
	
	/* End of ISR */
	if (xTaskWoken == pdTRUE)
		taskYIELD();

}