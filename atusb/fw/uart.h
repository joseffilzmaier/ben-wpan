#ifndef UART_H_
#define	UART_H_

#include <stdio.h>

void USART_Init(void);
int USART_WriteChar(char c, FILE* stream);
void USART_WriteHex(unsigned char c);
void USART_NewLine(void);

#endif /* UART_H_ */
