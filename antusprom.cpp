/*
Copyright 2009 Antus ant@symons.net.au
Modified 2010 Jason Roughley pis.controller@gmail.com

This file is part of Antusprom.

Antusprom is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Antusprom is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Antusprom.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <windows.h>
#include "stdafx.h"

#define VMAJ 0
#define VMIN 7
typedef unsigned char UCHAR;

HANDLE hSerialECU;
HANDLE hSerialTP;
HANDLE hSerialSP;
HANDLE ghMutex;
UCHAR emurom[0xFFFF] = {0};
BOOL rom_dirty[0xFFFF];

BOOL ecu = TRUE;		// is the ecu connected?
BOOL echo = FALSE;		// expect cable echo?
BOOL debug = FALSE;		// extra output to help debugging


unsigned char *readEcuMemory(HANDLE hSerial, UWORD address, UWORD length)
{
UCHAR *buffer;
UCHAR packetbuffer[0xFF];
UCHAR request[5] = {'?', 0, 0, 0, 0};
DWORD dwBytesCount = 0;
UINT i, actual, retries;

	buffer = (UCHAR *) malloc(length + 1);
	if (!buffer) return NULL;

	request[1] = ((address) & 0xFF00) >> 8;
	request[2] = (address) & 0xFF;
	request[3] = (length & 0xFF00) >> 8;
	request[4] = length & 0x00FF;

	printf ("readEcuMemory: @%04X:%04X..", address, length);
		
	PurgeComm(hSerial, PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);

	if (!WriteFile(hSerial, request, sizeof(request), &dwBytesCount, NULL))
	{
		printf("WriteFile request failure.\n");
		return NULL;
	}

	// read back cable echo
	if (echo == 1) ReadFile(hSerial, packetbuffer, dwBytesCount, &dwBytesCount, NULL);

	retries = 10;
	actual = 0;
	do
	{
		if (!ReadFile(hSerialECU, &buffer[actual], length - actual, &dwBytesCount, NULL))
			printf("ReadFile: failure\n");

		actual += dwBytesCount;
		retries--;
	}
	while (actual != length && retries);

	if (retries) printf("OK\n");
	else
	{
		printf ("failure to read %04X bytes\n", length);
		return NULL;
	}

	if (debug)
	{
		for (i=0; i < length; i++)
		{
			printf ("%02X", buffer[i]);
		}
		printf ("\n");
	}
	return buffer;
}

DWORD writeEcuMemory(HANDLE hSerial, UWORD address, UWORD length, UCHAR *data)
{
UCHAR request[5] = {'!', 0, 0, 0, 0};
UCHAR readbuf[5];
DWORD dwBytesCount = 0;

	request[1] = ((address) & 0xFF00)>>8;
	request[2] = (address) & 0xFF;
	request[3] = (length & 0xFF00) >> 8;
	request[4] = length & 0x00FF;

	printf("writeEcuMemory: @%04X:%04X..", address, length);

	if (!WriteFile(hSerial, request, sizeof(request), &dwBytesCount, NULL))
	{
		printf("WriteFile request failure\n");
		return NULL;
	}

	if (!WriteFile(hSerial, data, length, &dwBytesCount, NULL))
		printf("WriteFile data failure\n");
			
	// read back echo
	if (echo == 1)
		ReadFile(hSerial, readbuf, dwBytesCount, &dwBytesCount, NULL);

	printf ("OK\n");

return 1;
}

int setup_other_comm(_TCHAR *port, HANDLE &h)
{
DCB dcbSerialParams = {0};
COMMTIMEOUTS timeouts = {0};

	// Now open our half of the com0com link to tunerproRT
	h = CreateFile(port,
					GENERIC_READ | GENERIC_WRITE,
					0,
					0,
					OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL,
					0);

	if (h == INVALID_HANDLE_VALUE)
	{
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
		{
			printf("%s not found\n\n", port);
			exit(2);
		}
		printf("%s unknown error %d\n\n", port, GetLastError());
		exit(2);
	}
	
	printf("Serial port %s opened\n", port);

	if (!GetCommState(h, &dcbSerialParams))
	{
		printf("error getting com port state\n\n");
		exit(3);
	}

	dcbSerialParams.BaudRate = 115200;
	dcbSerialParams.Parity = NOPARITY;
	dcbSerialParams.ByteSize = 8;
	dcbSerialParams.StopBits = ONESTOPBIT;
	dcbSerialParams.fBinary = TRUE;
	dcbSerialParams.fOutxCtsFlow = FALSE;
	dcbSerialParams.fOutxDsrFlow = FALSE;
	dcbSerialParams.fDtrControl = DTR_CONTROL_DISABLE;
	dcbSerialParams.fRtsControl = RTS_CONTROL_DISABLE;

	if (!SetCommState(h, &dcbSerialParams))
	{
		printf("error setting com port state\n\n");
		exit(4);	
	}
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 1;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 1;

	if (!SetCommTimeouts(h, &timeouts))
	{
		printf("error setting com port timeouts\n\n");
		exit(5);	
	}
	PurgeComm(h, PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);
	return 1;
}

BOOL setup_ecu_comm(_TCHAR *port)
{
DCB dcbSerialParams = {0};
COMMTIMEOUTS timeouts= {0};
UCHAR *tempbuffer;
UINT address, retries, actual, requested;
UCHAR buffer[100];
DWORD dwBytesCount = 0;

	hSerialECU = CreateFile(port,
							GENERIC_READ | GENERIC_WRITE,
							0,
							0,
							OPEN_EXISTING,
							FILE_ATTRIBUTE_NORMAL,
							0);

	if (hSerialECU == INVALID_HANDLE_VALUE)
	{
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			printf("%s not found\n\n", port);
		else
			printf("%s unknown error\n\n", port);

		ecu=0;
	}
	
	if (!GetCommState(hSerialECU, &dcbSerialParams))
		printf("error getting com port state\n\n");

	dcbSerialParams.BaudRate = 8192;
	dcbSerialParams.ByteSize = 8;
	dcbSerialParams.StopBits = ONESTOPBIT;
	dcbSerialParams.Parity = NOPARITY;
	dcbSerialParams.fBinary = TRUE;
	dcbSerialParams.fOutxCtsFlow = FALSE;
	dcbSerialParams.fOutxDsrFlow = FALSE;
	dcbSerialParams.fDtrControl = DTR_CONTROL_DISABLE;
	dcbSerialParams.fRtsControl = RTS_CONTROL_DISABLE;

	if (!SetCommState(hSerialECU, &dcbSerialParams))
		printf("error setting com port state\n\n");

	timeouts.ReadIntervalTimeout = -1;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 1;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 1;

	if (!SetCommTimeouts(hSerialECU, &timeouts))
		printf("error setting com port timeouts\n\n");
	
	//ecu=0;
	if (ecu)
	{
		printf ("Waiting for ECU on %s\n", port);

		do
		{
			PurgeComm(hSerialECU, PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);
			if (!WriteFile(hSerialECU, "S", 1, &dwBytesCount, NULL))
				printf("WriteFile: title request failure\n");

			retries = 3;
			requested = 62;
			actual = 0;
			do
			{
				if (!ReadFile(hSerialECU, &buffer[actual], requested - actual, &dwBytesCount, NULL))
					printf("ReadFile: failure\n");

				actual += dwBytesCount;
				retries--;
			}
			while (actual != requested && retries);
		} while (actual != requested);

		buffer[actual] = 0;
		printf ("%s\n\n4K Calibration ROM to RAM @ 8192 baud, please wait..\n\n", buffer);

		// now grab the rom
		for (address = 0xF000; address < 0xffff; address += 0x1000)
		{
			printf("ROM -> RAM 0x%04X\n", address); 
			tempbuffer = readEcuMemory(hSerialECU, address, 0x1000);
		
			if (tempbuffer)
			{
					memcpy(&emurom[address], tempbuffer, 0x1000);
					free(tempbuffer);
			}
		}

		// now flag all bytes as 'clean'
		memset(rom_dirty, FALSE, 0xffff);
	}
	else
	{
		printf ("running in test mode (no ecu comms)\n");
		printf ("not copying ecu rom to pc ram\n");
	}
	return 1;
}

DWORD WINAPI WriteThread(LPVOID emuromp)
{
UCHAR *emurom = (UCHAR *) emuromp;
UINT current, write, length;
BOOL found;

	for(;;)
	{
		current = 0;

		do
		{
			found = FALSE;

			// find a dirty byte
			while (!found && current <= 0xffff)
			{
				if (rom_dirty[current]) found = TRUE;
				else current++;
			}

			if (found)
			{
				write = current++;
				length = 1;

				// see how many contiguous bytes there are up to a max 256
				while (current <= 0xffff && rom_dirty[current] && length < 0x1000)
				{
					current++;
					length++;
				}

				WaitForSingleObject( ghMutex, INFINITE);

				// send the data to the ECU
				if (writeEcuMemory(hSerialECU, write, length, &emurom[write]))
				{
					// mark the sent data as clean
					while (write < current) rom_dirty[write++] = FALSE;
				}
				else
				{
					// abort and retry
					current = write;
				}

				ReleaseMutex(ghMutex);
			}
		} while (current <= 0xffff);

		Sleep(1000);
	}
	return 0;
}

DWORD WINAPI input_handler_thread(LPVOID which)
{
HANDLE *h = (HANDLE *)which;
UCHAR autopromid[]={0x02, 0x05, 'A'};
UCHAR okay[]={'O'};

// comms stuff
UCHAR packetbuffer[0x105];
DWORD dwBytesCount;

// emu stuff
DWORD length, address;
UINT i, t;
UCHAR command, c;
UINT cmd_state = 0, cmd_bytes = 0, retries, actual;
UCHAR cs;

	while (1)
	{
		if (ReadFile(h, packetbuffer, sizeof(packetbuffer), &dwBytesCount, NULL) && dwBytesCount != 0)
		{
			if (debug)
			{
				printf("TP sent: ");
				for(i = 0; i != dwBytesCount; i++) printf(" %02X ", packetbuffer[i]);
				printf("\n\n");
			}
			cmd_bytes = dwBytesCount;
			i = 0;
			do
			{
				c = packetbuffer[i++];
				switch(cmd_state)
				{
					case 0:
						command = c & 0xdf;
						switch (c)
						{
							case 'V':
								WaitForSingleObject( ghMutex, INFINITE);
								printf("Version requested, sending V2.5 AutoProm when locked..");
								if (WriteFile(h, autopromid, sizeof(autopromid), &dwBytesCount, NULL))
									printf ("OK\n");	
								else
									printf ("Failed\n");

								ReleaseMutex(ghMutex);
								break;

							case 'R':
							case 'W':
								cmd_state++;
								break;

							// ignore
							case 'k':
							case 'K':
							case 'Z':
							case 'z':
								break;

							case 's':
							case 'm':
								packetbuffer[0] = c;
								cmd_state = 10;
								break;

							default:
								printf("spurious character %02X out of sequence?\n", c);
								PurgeComm(h, PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);
								i = cmd_bytes;
								break;
						}
						break;

					case 1: // command length
						length = c;
						if (!length) length = 0x100;
						cmd_state++;
						break;

					case 2:	// address hi byte
						address = c << 8;
						cmd_state++;
						break;

					case 3: // address lo byte
						cmd_state++;
						address += c;
						switch (command)
						{
							case 'R':
								printf("ROM read @%04X:%02X..", address, length);
								break;

							case 'W':
								printf("ROM write @%04X:%02X..", address, length);
								break;
						}
						break;

					case 4: // checksum / write byte
						switch (command)
						{
							case 'R':
								if (WriteFile(h, &emurom[address], length, &dwBytesCount, NULL))
									printf("OK\n");
								else
									printf("Writefile failure\n");

								// send checksum
								for (cs = 0, t = 0; t < length; t++)
								{
									cs += emurom[address + t];
									if (debug) printf(" %02X ", emurom[address + t]);
								}

								WriteFile(h, &cs, 1, &dwBytesCount, NULL);

								if (debug) printf("\n");
								cmd_state = FALSE;
								break;

							case 'W':
								if (!length) // checksum
								{
									cmd_state = 0;
									if(WriteFile(h, okay, 1, &dwBytesCount, NULL))
										printf("OK\n");
									else
										printf("WriteFile failure\n");
									break;
								}
								emurom[address] = c;
								rom_dirty[address] = TRUE;
								address++;
								length--;
						}
						break;

					case 10:
						cmd_state++;
						packetbuffer[1] = c;
						break;

					case 11:
						cmd_state++;
						packetbuffer[2] = c;
						break;

					case 12:
						cmd_state++;
						packetbuffer[3] = c;
						length = c << 8;
						break;

					case 13:
						cmd_state++;
						packetbuffer[4] = c;
						length += c;

						WaitForSingleObject( ghMutex, INFINITE);

						if (debug) printf("direct ALDL passthrough\n");

						PurgeComm(hSerialECU, PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);
						WriteFile(hSerialECU, packetbuffer, 6, &dwBytesCount, NULL);
						if (debug) printf("sent %d bytes to the ecu\n", dwBytesCount);
						if (echo) WriteFile(h, packetbuffer, 6, &dwBytesCount, NULL);
						if (debug && echo) printf("sent %d bytes of echo back to sender\n", dwBytesCount);

						// start collecting returned chars
						retries = 10;
						actual = 0;
						do
						{
							if (!ReadFile(hSerialECU, &packetbuffer[actual], length - actual, &dwBytesCount, NULL))
								printf("ReadFile: failure\n");

							actual += dwBytesCount;
							retries--;
						}
						while (actual != length && retries);

						if (debug) printf("read %d bytes of reply\n", actual);
						if (retries) WriteFile(h, packetbuffer, actual, &dwBytesCount, NULL);
						else
						{
							PurgeComm(h, PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);
							PurgeComm(hSerialECU, PURGE_RXABORT|PURGE_TXABORT|PURGE_RXCLEAR|PURGE_TXCLEAR);
						}

						if (debug)
						{
							printf("sent %d bytes of reply back as answer\n", dwBytesCount);
							for (i=0; i<dwBytesCount; i++)	{
								printf("%02X ",packetbuffer[i]);
							}
							printf("\n\n");
						}

						ReleaseMutex(ghMutex);

						cmd_state = FALSE;
						break;
				}
			} while (i < cmd_bytes);
		}
		else Sleep(200);
	}

	return 1;
}

int _tmain(int argc, _TCHAR* argv[])
{
// thread stuff
HANDLE hWriteThread;
HANDLE hInputThreadTP;
HANDLE hInputThreadSP;
DWORD dwGenericThread;

	printf("Welcom to MODIFIED antusprom for PIS firmware V%01d.%01d!\nYour realtime ALDL solution! :)\n", VMAJ, VMIN);
	printf("This version was built %s cable echo support\n\n", echo?"with":"without");
	printf("(c) 2009 Antus ant@symons.net.au\n");
	printf("Modified 2010 Jason Roughley pis.controller@gmail.com\n\n");
	
	if (echo == 1)	{
		printf("This version built to expect ALDL cable echo\n");
	}
	
	if (argc < 3 || argc > 5)	{
		printf("Usage: %s COM1 COM2 <COM3>\n\nwhere COM1 is the port with ECU connected (or 'test' for testing with no ecu)\nand COM5 is a com0com loopback serial.\nCOM7 is another com0com loopback, or for ALDL passthrough\n\n", argv[0]);
		exit(1);
	}

	if (argc == 5 && !strcmp("debug", argv[4])) debug = TRUE;

	setup_ecu_comm(argv[1]);
	setup_other_comm(argv[2], hSerialTP);
	printf ("tunerproRT link com port init OK\n\n");
	if (argv[3])	{
		setup_other_comm(argv[3], hSerialSP);
		printf ("forwarding com port init OK\n\n");
	}

	ghMutex = CreateMutex( NULL, FALSE, NULL);   
    
	if (ghMutex == NULL) {
        printf("CreateMutex error: %d\n", GetLastError());
        exit(12);
    }

	if (ecu)	{
		printf ("starting writer thread\n");
		hWriteThread = CreateThread(NULL, 0, WriteThread, &emurom, 0, &dwGenericThread);
		if(hWriteThread == NULL)	{
				DWORD dwError = GetLastError();
				printf("Error creating writer thread %d\n\n", dwError);
				exit(10);
		}
	}

	printf ("starting TP input thread\n");
	hInputThreadTP = CreateThread(NULL, 0, input_handler_thread, hSerialTP, 0, &dwGenericThread);
	if(hInputThreadTP == NULL)	{
			DWORD dwError = GetLastError();
			printf("Error creating TP thread %d\n\n", dwError);
			exit(10);
	}
	

	if (argv[3])	{
		printf ("starting ALDL forwarding thread\n");
		hInputThreadSP = CreateThread(NULL,0, input_handler_thread, hSerialSP, 0, &dwGenericThread);
		if(hInputThreadSP == NULL)	{
				DWORD dwError = GetLastError();
				printf("Error creating SP thread %d\n\n", dwError);
				exit(10);
		}
	}

	while(1)	{
		Sleep(10000);
	}

	CloseHandle(ghMutex);
	if (hSerialECU) CloseHandle(hSerialECU);
	if (hSerialTP) CloseHandle(hSerialTP);
	if (hSerialSP) CloseHandle(hSerialTP);

	return 0;
}
