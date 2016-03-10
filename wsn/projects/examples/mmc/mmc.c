/* ***********************************************************************
**
**  Copyright (C) 2006  Jesper Hansen <jesper@redegg.net> 
**
**
**  Interface functions for MMC/SD cards
**
**  File mmc_if.h
**
*************************************************************************
**
**  This program is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License
**  as published by the Free Software Foundation; either version 2
**  of the License, or (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software Foundation, 
**  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
**
*************************************************************************/

/* Modified by Hsin-Mu (Michael) Tsai <hsinmu@cmu.edu> to address a "write busy" bug.      July 2008 */

#include <nrk.h>

#include <avr/io.h>
#include <inttypes.h>
#include <stdio.h>
#include "mmc.h"

uint8_t busy_flag=0;


/** Hardware SPI I/O. 
	\param byte Data to send over SPI bus
	\return Received data from SPI bus
*/
uint8_t spi_byte(uint8_t byte)
{
	SPDR = byte;
	while(!(SPSR & (1<<SPIF)))
	{}
	return SPDR;
}



/** Send a command to the MMC/SD card.
	\param command	Command to send
	\param px	Command parameter 1
	\param py	Command parameter 2
*/
void mmc_send_command(uint8_t command, uint16_t px, uint16_t py)
{
	register union u16convert r;

	MMC_CS_PORT &= ~(1 << MMC_CS);	// enable CS

	spi_byte(0xff);			// dummy byte

	spi_byte(command | 0x40);

	r.value = px;
	spi_byte(r.bytes.high);	// high byte of param x
	spi_byte(r.bytes.low);	// low byte of param x

	r.value = py;
	spi_byte(r.bytes.high);	// high byte of param y
	spi_byte(r.bytes.low);	// low byte of param y

	spi_byte(0x95);			// correct CRC for first command in SPI          
							// after that CRC is ignored, so no problem with 
							// always sending 0x95                           
	spi_byte(0xff);			// ignore return byte
}


/** Get Token.
	Wait for and return a non-ff token from the MMC/SD card
	\return The received token or 0xFF if timeout
*/
uint8_t mmc_get(void)
{
	uint16_t i = 0xffff;
	uint8_t b = 0xff;

	while ((b == 0xff) && (--i)) 
	{
		b = spi_byte(0xff);
	}
	return b;

}

/** Get Datatoken.
	Wait for and return a data token from the MMC/SD card
	\return The received token or 0xFF if timeout
*/
uint8_t mmc_datatoken(void)
{
	uint16_t i = 0x07ff;
	
	uint8_t b = 0xff;

	while ((b != 0xfe) && (--i)) 
	{
		b = spi_byte(0xff);
	}
	return b;
}


/** Finish Clocking and Release card.
	Send 10 clocks to the MMC/SD card
 	and release the CS line 
*/
void mmc_clock_and_release(void)
{
	uint8_t i;

	// SD cards require at least 8 final clocks
	//	for(i=0;i<10;i++)
	for(i=0;i<32;i++)
		spi_byte(0xff);	

	
	//nrk_spin_wait_us(320);

    MMC_CS_PORT |= (1 << MMC_CS);	// release CS (high)
}



/** Read MMC/SD sector.
 	Read a single 512 byte sector from the MMC/SD card
	\param lba	Logical sectornumber to read
	\param buffer	Pointer to buffer for received data
	\return 0 on success, -1 on error
*/
int mmc_readsector(uint32_t lba, uint8_t *buffer)
{
	uint16_t i;

	// send read command and logical sector address

	mmc_send_command(17,(lba>>7) & 0xffff, (lba<<9) & 0xffff);

	if (mmc_datatoken() != 0xfe)	// if no valid token
	{
	    mmc_clock_and_release();	// cleanup and	
   		return -1;					// return error code
	}

	for (i=0;i<512;i++)				// read sector data
    	*buffer++ = spi_byte(0xff);

	spi_byte(0xff);					// ignore dummy checksum
	spi_byte(0xff);					// ignore dummy checksum

	mmc_clock_and_release();		// cleanup

	return 0;						// return success		
}


inline int mmc_writesector(uint32_t lba, uint8_t *buffer) {
  return _mmc_writesector(lba, buffer, 0);  
}

inline int mmc_writesector_pending(uint32_t lba, uint8_t *buffer) {
  return _mmc_writesector(lba, buffer, 1);
}


/** Write MMC/SD sector.
 	Read a single 512 byte sector from the MMC/SD card
	\param lba	Logical sectornumber to read
	\param buffer	Pointer to buffer for received data
	\return 0 on success, -1 on error
*/
int _mmc_writesector(uint32_t lba, uint8_t *buffer, uint8_t pending)
{
	uint16_t i;
	//	uint8_t t,b,j;
	// send read command and logical sector address
	uint8_t val;
	uint16_t count=0, write_count=0;

	//	printf("lba:%lu\n",lba);

	if (busy_flag)
	  return (-1);

	do {
	++write_count;

	  mmc_send_command(24,(lba>>7) & 0xffff, (lba<<9) & 0xffff);
	    
	  val=mmc_get();

	  if (val==0xff) { //write command timeout
	    mmc_clock_and_release();
	    return (-1);
	  }
	    
	  spi_byte(0xFE);   //start block token
	  for (i=0;i<512;i++)				// read sector data
	    {
	      spi_byte(buffer[i]);
	    }
	  
	  spi_byte(0xff);					// ignore dummy checksum
	  spi_byte(0xff);					// ignore dummy checksum
	  
	  while ((val=mmc_get())==0xff);
	  if ((val & 0x1f)!= 5) {
	    //printf("data_response: %X\n", val);
	    continue; // send the command again.
	  }
	  
	  while(spi_byte(0xff)==0) {
	    if (!pending && count >=3000)
	      break;
	    ++count;
	  }  //waiting for write operation to finish


	  if (!pending) {
	    if (count>=3000) { //timeout ....release without waiting for the busy signal
	      busy_flag=1;
#ifdef MMC_DEBUG
	      printf("timeout\n");
#endif
	      mmc_clock_and_release();
	      return (0);  // the write operation is actually successful. (just waiting for it to be done)
	      
	    }
	  }
	  
	}while( (val & 0x1f) != 5);
	mmc_clock_and_release();		// cleanup
	//	printf("write count:%u busy count:%u\n", write_count, count);
	
	return 0;						// return success		
}

uint8_t mmc_card_detect()
{
return (!(MMC_DETECT_PIN & (1<<MMC_DETECT)));

}



/** Init MMC/SD card.
	Initialize I/O ports for the MMC/SD interface and 
	send init commands to the MMC/SD card
	\return 0 on success, other values on error 
*/
uint8_t mmc_init(void)
{
	int i;
	uint8_t val;
	uint16_t counter=0;
	

	
	// setup I/O ports 

	SPI_PORT &= ~((1 << MMC_SCK) | (1 << MMC_MOSI));	// low bits
	SPI_PORT |= (1 << MMC_MISO);						// high bits
	SPI_DDR  |= (1<<MMC_SCK) | (1<<MMC_MOSI);			// direction


	MMC_DETECT_PORT |= (1 << MMC_DETECT);	// Initial level is high	
	MMC_DETECT_DDR  &= ~(1 << MMC_DETECT);	// Direction is input 
	
	MMC_CS_PORT |= (1 << MMC_CS);	// Initial level is high	
	MMC_CS_DIR  |= (1 << MMC_CS);	// Direction is output


//	SPI_DDR |= (1<<0);			// assume it's bit0 (mega128, portB and others)

	SPCR = (1<<MSTR)|(1<<SPE);	// enable SPI interface
	SPSR = 1;					// set double speed	


	for(i=0;i<10;i++)			// send 80 clocks while card power stabilizes
		spi_byte(0xff);

	if (busy_flag) { //write operation is possibly still in progress

	  MMC_CS_PORT &= ~(1 << MMC_CS);	// enable CS so SD starts to access dataOut
	  
	  for(i=0;i<10;i++)			// send 80 clocks while card power stabilizes
	    spi_byte(0xff);

	  do{
	    val=spi_byte(0xff);
	    ++counter;
	  }while(val==0 && counter <= 1000) ; //wait for possible write operation to finish.

	  if (counter>=1000) {  //timeout again...
#ifdef MMC_DEBUG
	    nrk_kprintf(PSTR("mmc_init: waiting for write busy but timeout again..\n"));
#endif
	    mmc_clock_and_release();
	    return 4;
	  }
#ifdef MMC_DEBUG
	  printf("counter:%d\n", counter);
#endif
  	  busy_flag=0;
	}

	
	mmc_send_command(0,0,0);	// send CMD0 - reset card
	
	if ((val=mmc_get()) != 1)			// if no valid response code
	  {
	  //printf("mmc_init: mmc get val:%u\n", val);
	  
	  mmc_clock_and_release();
	  return 3;
	}
	

	//
	// send CMD1 until we get a 0 back, indicating card is done initializing 
	//
	i = 0xffff;						// max timeout
	while (((val=spi_byte(0xff)) != 0) && (--i))	// wait for it
	{
	     mmc_send_command(1,0,0);	// send CMD1 - activate card init
	}




	//printf("mmc_init: CMD1 response: %u\n", val);
	mmc_clock_and_release();		// clean up

	if (i == 0)						// if we timed out above
	   return 2;					// return failure code

	return 0;
}


