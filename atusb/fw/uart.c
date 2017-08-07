// Basic output routines for USART, adapted to Atmels AT90USB microcontrollers
// Based on AT90USB Datasheet (7593D-AVR-07/06), chapter 18 and examples from
// http://www.mikrocontroller.net/articles/AVR-GCC-Tutorial and
// http://www.roboternetz.de/wissen/index.php/UART_mit_avr-gcc
// S. Salewski 27-FEB-2007

#include <avr/io.h>
#include "uart.h"

#define USART_BAUD 38400UL
#define F_CPU 8000000UL

#define Wait_USART_Ready() while (!(UCSR1A & (1<<UDRE1)))
#define UART_UBRR (F_CPU/(16L*USART_BAUD)-1) 

// initialize USART, 8N1 mode
void
USART_Init(void)
{
	CLKPR = (1<<7);
	CLKPR = 0; // clock prescaler == 0, so we have 16 MHz mpu frequency 
	UBRR1 = UART_UBRR;
	UCSR1C = (1<<UCSZ10) | (1<<UCSZ11);
	UCSR1B = (1<<TXEN1);
	do
	{
		UDR1;
	}
	while (UCSR1A & (1 << RXC1));
	
}

int USART_WriteChar(char c, FILE* stream)
{
	if (c == '\n'){
		USART_NewLine();
	}
	else {
		Wait_USART_Ready();
		UDR1 = c;
	}
	return 0;
}

void
USART_WriteHex(unsigned char c)
{
	unsigned char nibble;
	nibble = (c >> 4);
	if (nibble < 10) nibble += '0'; else nibble += ('A'-10);
	Wait_USART_Ready();
	UDR1 = nibble;
	nibble = (c & 0x0F);
	if (nibble < 10) nibble += '0'; else nibble += ('A'-10);
	Wait_USART_Ready();
	UDR1 = nibble;
}

void
USART_NewLine(void)
{
	Wait_USART_Ready();
	UDR1 = '\r';
	Wait_USART_Ready();
	UDR1 = '\n';
}
