#define F_CPU 8000000UL			/* Define CPU clock Frequency e.g. here its 8MHz */

#include <avr/io.h>
#include <string.h>			/* Include string library */
#include <stdio.h>			/* Include standard IO library */
#include <stdlib.h>			/* Include standard library */
#include <stdbool.h>			/* Include standard boolean library */
#include <util/delay.h>			/* Include delay header file */
#include <avr/interrupt.h>		/* Include avr interrupt header file */
#include "USART_RS232_H_file.h"		/* Include USART header file */

#define SERV_ADDR "127.0.0.1"

#define SREG    _SFR_IO8(0x3F)

#define DEFAULT_BUFFER_SIZE		200
#define DEFAULT_TIMEOUT			20000
#define DEFAULT_CRLF_COUNT		2
/* Select Demo */
//#define RECEIVE_DEMO								/* Define RECEIVE demo */
//#define SEND_DEMO									/* Define SEND demo */

/* Define Required fields shown below */
#define DOMAIN				"api.thingspeak.com"
#define PORT				"2022"
#define API_WRITE_KEY		"C7JFHZY54GLCJY38"
#define CHANNEL_ID			"119922"

#define APN					"internet"
#define USERNAME			""
#define PASSWORD			""

#define DEVICE_ID "1225467234"

enum SIM900_RESPONSE_STATUS {
	SIM900_RESPONSE_WAITING,
	SIM900_RESPONSE_FINISHED,
	SIM900_RESPONSE_TIMEOUT,
	SIM900_RESPONSE_BUFFER_FULL,
	SIM900_RESPONSE_STARTING,
	SIM900_RESPONSE_ERROR
};

int8_t Response_Status, CRLF_COUNT = 0;
uint16_t Counter = 0;
uint32_t TimeOut = 0;
char RESPONSE_BUFFER[DEFAULT_BUFFER_SIZE];
char CONNECTION_NUMBER[] = "1";
uint8_t dc = 0;

uint8_t SAD;
uint8_t DAD;
uint8_t pulse;

uint8_t periods [5] = {100,0,0,0,0}; // counts of measuring
uint8_t spans [5] = {100,0,0,0,0}; // time spans between measuring
uint8_t secs = 0;
uint8_t mins = 0;
char digitBuf[2] = {0};
bool sendFlag = false;
uint8_t iter = 0;

char settingsExample[] = "15.5_30.8_15.12"; // span.count_nextSpan.next_Count...

uint8_t dotCount(char *sett)
{
	uint8_t count = 0;
	char* shit = ".";
	char* d = strstr(shit, sett);
	while(d)
	{
		count++;
		d = strstr(shit, sett);	
	}
	return count;
}

void setSettings(char *sett)
{
	uint8_t j = 0;
	uint8_t k = 0;
	uint8_t l = 0;
	for (uint32_t i = 0; i < strlen(sett); i++)
	{
		if (sett[i] == '.')
		{
			spans[k] = atoi(digitBuf);
			k++;
			j=0;
			digitBuf[0] = 0;
			digitBuf[1] = 0;
		}
		else if (sett[i] == '_')
		{
			periods[l] = atoi(digitBuf);
			l++;
			j=0;
			digitBuf[0] = 0;
			digitBuf[1] = 0;
		}
		else
		{
			digitBuf[j] = sett[i];
			j++;
		}
	}
}

void start(void)
{
	TCCR1B &= ~(1<<CS11);
	TCCR1B &= ~(1<<CS10);
	TCCR1B |= (1<<CS12); // frequency division by 256 = 31250
	TIMSK |= (1<<OCIE1A); // match interrupt enable
	OCR1AH = 0b01111010;
	OCR1AL = 0b00010010; // compare register 31250
	TCNT1 = 0;
	TCCR1B |= (1<<WGM12); // CTC - clear timer on compare
}

void getSupposedData()
{
	srand(TCNT1); // ??????????????
	// imitate pressure measuring SAD 160...120 DAD 90...60 pulse 120...60
	SAD = 120 + rand()%160;
	DAD = 60 + rand()%90;
	pulse = 60 + rand()%120;
	sendFlag = true;
}

void Read_Response()
{
	char CRLF_BUF[2];
	char CRLF_FOUND;
	uint32_t TimeCount = 0, ResponseBufferLength;
	while(1)
	{
		if(TimeCount >= (DEFAULT_TIMEOUT+TimeOut))
		{
			CRLF_COUNT = 0; TimeOut = 0;
			Response_Status = SIM900_RESPONSE_TIMEOUT;
			return;
		}

		if(Response_Status == SIM900_RESPONSE_STARTING)
		{
			CRLF_FOUND = 0;
			memset(CRLF_BUF, 0, 2);
			Response_Status = SIM900_RESPONSE_WAITING;
		}
		ResponseBufferLength = strlen(RESPONSE_BUFFER);
		if (ResponseBufferLength)
		{
			_delay_ms(1);
			TimeCount++;
			if (ResponseBufferLength==strlen(RESPONSE_BUFFER))
			{
				for (uint16_t i=0;i<ResponseBufferLength;i++)
				{
					memmove(CRLF_BUF, CRLF_BUF + 1, 1);
					CRLF_BUF[1] = RESPONSE_BUFFER[i];
					if(!strncmp(CRLF_BUF, "\r\n", 2))
					{
						if(++CRLF_FOUND == (DEFAULT_CRLF_COUNT+CRLF_COUNT))
						{
							CRLF_COUNT = 0; TimeOut = 0;
							Response_Status = SIM900_RESPONSE_FINISHED;
							return;
						}
					}
				}
				CRLF_FOUND = 0;
			}
		}
		_delay_ms(1);
		TimeCount++;
	}
}

void Buffer_Flush()
{
	memset(RESPONSE_BUFFER,0,DEFAULT_BUFFER_SIZE);
	Counter = 0;
}

void Start_Read_Response()
{
	Response_Status = SIM900_RESPONSE_STARTING;
	do {
		Read_Response();
	} while(Response_Status == SIM900_RESPONSE_WAITING);

}

void GetResponseBody(char* Response, uint16_t ResponseLength)
{

	uint16_t i = 12;
	char buffer[5];
	while(Response[i] != '\r')
	++i;

	strncpy(buffer, Response + 12, (i - 12));
	ResponseLength = atoi(buffer);

	i += 2;
	uint16_t tmp = strlen(Response) - i;
	memcpy(Response, Response + i, tmp);

	if(!strncmp(Response + tmp - 6, "\r\nOK\r\n", 6))
	memset(Response + tmp - 6, 0, i + 6);
}

bool WaitForExpectedResponse(char* ExpectedResponse)
{
	Buffer_Flush();
	_delay_ms(200);
	Start_Read_Response();						/* First read response */
	if((Response_Status != SIM900_RESPONSE_TIMEOUT) && (strstr(RESPONSE_BUFFER, ExpectedResponse) != NULL))
	return true;							/* Return true for success */
	return false;								/* Else return false */
}

bool SendATandExpectResponse(char* ATCommand, char* ExpectedResponse)
{
	USART_SendString(ATCommand);				/* Send AT command to SIM900 */
	USART_TxChar('\r');
	return WaitForExpectedResponse(ExpectedResponse);
}

bool TCPClient_ApplicationMode(uint8_t Mode)
{
	char _buffer[20];
	sprintf(_buffer, "AT+CIPMODE=%d\r", Mode);
	_buffer[19] = 0;
	USART_SendString(_buffer);
	return WaitForExpectedResponse("OK");
}

bool TCPClient_ConnectionMode(uint8_t Mode)
{
	char _buffer[20];
	sprintf(_buffer, "AT+CIPMUX=%d\r", Mode);
	_buffer[19] = 0;
	USART_SendString(_buffer);
	return WaitForExpectedResponse("OK");
}

bool AttachGPRS()
{
	USART_SendString("AT+CGATT=1\r");
	return WaitForExpectedResponse("OK");
}

bool SIM900_Start()
{
	for (uint8_t i=0;i<5;i++)
	{
		if(SendATandExpectResponse("ATE0","OK")||SendATandExpectResponse("AT","OK"))
		return true;
	}
	return false;
}

bool TCPClient_Shut()
{
	USART_SendString("AT+CIPSHUT\r");
	return WaitForExpectedResponse("OK");
}

bool TCPClient_Close()
{
	USART_SendString("AT+CIPCLOSE=1\r");
	return WaitForExpectedResponse("OK");
}

bool TCPClient_Connect(char* _APN, char* _USERNAME, char* _PASSWORD)
{

	USART_SendString("AT+CREG?\r");
	if(!WaitForExpectedResponse("+CREG: 0,1"))
	return false;

	USART_SendString("AT+CGATT?\r");
	if(!WaitForExpectedResponse("+CGATT: 1"))
	return false;

	USART_SendString("AT+CSTT=\"");
	USART_SendString(_APN);
	USART_SendString("\",\"");
	USART_SendString(_USERNAME);
	USART_SendString("\",\"");
	USART_SendString(_PASSWORD);
	USART_SendString("\"\r");
	if(!WaitForExpectedResponse("OK"))
	return false;

	USART_SendString("AT+CIICR\r");
	if(!WaitForExpectedResponse("OK"))
	return false;

	USART_SendString("AT+CIFSR\r");
	if(!WaitForExpectedResponse("."))
	return false;

	USART_SendString("AT+CIPSPRT=1\r");
	return WaitForExpectedResponse("OK");
}

bool TCPClient_connected() {
	USART_SendString("AT+CIPSTATUS\r");
	CRLF_COUNT = 2;
	return WaitForExpectedResponse("CONNECT OK");
}

uint8_t TCPClient_Start(char* Domain, char* Port)
{
	USART_SendString("AT+CIPMUX?\r");
	if(WaitForExpectedResponse("+CIPMUX: 0"))
	USART_SendString("AT+CIPSTART=\"TCP\",\"");
	else
	{
		USART_SendString("AT+CIPSTART=\"");
		USART_SendString(CONNECTION_NUMBER);
		USART_SendString("\",\"TCP\",\"");
	}
	
	USART_SendString(Domain);
	USART_SendString("\",\"");
	USART_SendString(Port);
	USART_SendString("\"\r");

	CRLF_COUNT = 2;
	if(!WaitForExpectedResponse("CONNECT OK"))
	{
		if(Response_Status == SIM900_RESPONSE_TIMEOUT)
		return SIM900_RESPONSE_TIMEOUT;
		return SIM900_RESPONSE_ERROR;
	}
	return SIM900_RESPONSE_FINISHED;
}

uint8_t TCPClient_Send(char* Data)
{
	USART_SendString("AT+CIPSEND\r");
	CRLF_COUNT = -1;
	WaitForExpectedResponse(">");
	USART_SendString(Data);
	USART_SendString("\r\n");
	USART_TxChar(0x1A);

	if(!WaitForExpectedResponse("SEND OK"))
	{
		if(Response_Status == SIM900_RESPONSE_TIMEOUT)
		return SIM900_RESPONSE_TIMEOUT;
		return SIM900_RESPONSE_ERROR;
	}
	return SIM900_RESPONSE_FINISHED;
}

void waitForInstructions()
{
	TCPClient_Start(DOMAIN, PORT);
	TCPClient_Send(DEVICE_ID);
	Start_Read_Response();
	_delay_ms(600);
	TCPClient_Close();
}

ISR (USART_RXC_vect)							/* Receive ISR routine */
{
	uint8_t oldsrg = SREG;
	RESPONSE_BUFFER[Counter] = UDR;				/* Copy data to buffer & increment counter */
	Counter++;
	if(Counter == DEFAULT_BUFFER_SIZE)
	Counter = 0;
	SREG = oldsrg;
}

ISR (TIMER1_COMPA_vect)
{
	if ((secs%5) == 0)
	{
		char buff[100];
		getSupposedData();
		sprintf(buff, "%s;%d;%d;%d\0", DEVICE_ID, SAD, DAD, pulse);
		USART_SendString(buff);
		memset(buff,0,100);
	}
	if (secs == 59)
	{
		mins++;
		secs = 0;
	} 
	else
	{
		secs++;
	}
	if (mins == spans[iter])
	{
		getSupposedData();
		dc = dotCount(settingsExample);
		mins = 0;
		periods[iter]--;
		if ((periods[iter] == 0) && (iter < dc))
		{
			iter++;
		}
	} 
}

// ---------------------------------------------------------------------------------------------------------------------

int main(void)
{
	start();
	char _buffer[100];

	USART_Init(9600);						/* Initiate USART with 9600 baud rate */
	sei();									/* Start global interrupt */
	
	setSettings(settingsExample);
	getSupposedData();
	sendFlag = false;
	sprintf(_buffer, "%s;%d;%d;%d;%d;%d\0", DEVICE_ID, SAD, DAD, pulse, spans[iter], periods[iter]);
	USART_SendString(_buffer);
	memset(_buffer,0,100);
	/*
	while(!SIM900_Start());
	TCPClient_Shut();
	TCPClient_ConnectionMode(0);				// 0 = Single; 1 = Multi 
	TCPClient_ApplicationMode(0);				// 0 = Normal Mode; 1 = Transperant Mode 
	AttachGPRS();
	while(!(TCPClient_Connect(APN, DEVICE_ID, PASSWORD)));
	*/
	//waitForInstructions();
	//setSettings(RESPONSE_BUFFER);
	
    while (1) 
    {
		TCPClient_Start(SERV_ADDR, PORT);
		
		if (sendFlag == true)
		{
			sprintf(_buffer, "%s;%d;%d;%d", DEVICE_ID, SAD, DAD, pulse);
			sendFlag = false;
			TCPClient_Send(_buffer);
			_delay_ms(600);
			memset(_buffer,0,100);
			TCPClient_Close();
			_delay_ms(15000);	/* Thingspeak server delay */
		}
		
		#ifdef SEND_DEMO
		memset(_buffer, 0, 100);
		sprintf(_buffer, "GET /update?api_key=%s&field1=%d", API_WRITE_KEY, Sample++);
		TCPClient_Send(_buffer);
		_delay_ms(600);
		TCPClient_Close();
		_delay_ms(15000);	/* Thingspeak server delay */
		#endif
		
		#ifdef RECEIVE_DEMO
		memset(_buffer, 0, 100);
		sprintf(_buffer, "GET /channels/%s/feeds/last.txt", CHANNEL_ID);
		TCPClient_Send(_buffer);
		_delay_ms(600);
		TCPClient_Close();
		#endif
		
    }
}


//------------------------------------------------------------------------------------------------------------------------------

