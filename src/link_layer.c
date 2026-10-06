// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <signal.h>

#include <stdio.h>
#include <unistd.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

#define FLAG 0x7E
#define A_TX 0x03

#define C_SET 0X03
#define C_UA 0x07

typedef enum { 
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} State;

int alarmEnabled = 0;
int alarmCount = 0;

void alarmHandler(int signal) {
    alarmEnabled = 0;
    alarmCount++;

    printf("Alarm #%d recieved\n", alarmCount);
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llOpenTx(LinkLayer llParameters)
{
    // ----------------------------------------------------
    // This example code shows how to open the serial port and send a string.
    // TODO: Adapt and extend this code according to the specifications of the project.
    // ----------------------------------------------------

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    };
    

    printf("Serial port %s opened\n", llParameters.serialPort);

    // Create string to send
    unsigned char setFrame[5];

    setFrame[0] = FLAG;
    setFrame[1] = A_TX;
    setFrame[2] = C_SET;
    setFrame[3] = A_TX ^ C_SET;
    setFrame[4] = FLAG;

    alarmCount = 0;
    alarmEnabled = 0;
    int uaReceived = 0;

while (alarmCount <= llParameters.nRetransmissions && !uaReceived)
    {
        printf("A enviar SET - tentativa %d\n", alarmCount + 1);

        writeBytesSerialPort(setFrame, 5);

        alarmEnabled = 1;
        alarm(llParameters.timeout);

        State state = START;
    unsigned char byte;

    while (state != STOP && alarmEnabled)
    {
        int res = readByteSerialPort(&byte);

        if (res < 0 && alarmEnabled)
        {
            return -1;
        }

        if (res == 0)
        {
            continue;
        }

        if (res > 0)
        {
            printf("Byte received: 0x%02X\n", byte);

            switch (state)
            {
                case START:
                    if (byte == FLAG)
                        state = FLAG_RCV;
                    break;

                case FLAG_RCV:
                    if (byte == A_TX)
                        state = A_RCV;
                    else if (byte != FLAG)
                        state = START;
                    break;

                case A_RCV:
                    if (byte == C_UA)
                        state = C_RCV;
                    else if (byte == FLAG)
                        state = FLAG_RCV;
                    else
                        state = START;
                    break;

                case C_RCV:
                    if (byte == (A_TX ^ C_UA))
                        state = BCC_OK;
                    else if (byte == FLAG)
                        state = FLAG_RCV;
                    else
                        state = START;
                    break;

                case BCC_OK:
                    if (byte == FLAG)
                        state = STOP;
                    else
                        state = START;
                    break;

                case STOP:
                    break;
            }
            }
           }
           if (state == STOP)
            {
                alarm(0);
                alarmEnabled = 0;
                uaReceived = 1;

                printf("UA recebido corretamente\n");
            }
        }
       
        if (!uaReceived)
    {
        printf("Falha ao estabelecer ligação\n");
        return -1;
    }

    // Close serial port
    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return 0;
}

int llOpenRx(LinkLayer llParameters)
{
    // ----------------------------------------------------
    // This example code shows how to open the serial port and receive a string.
    // TODO: Adapt and extend this code according to the specifications of the project.
    // ----------------------------------------------------

    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    printf("Serial port %s opened\n", llParameters.serialPort);

    State state = START;
    unsigned char byte;

    while (state != STOP)
    {
        int res = readByteSerialPort(&byte);

        if (res < 0)
        {
            return -1;
        }

        if (res == 0)
        {
            continue;
        }

        printf("Byte received: 0x%02X\n", byte);

        switch (state)
        {
            case START:
                if (byte == FLAG)
                    state = FLAG_RCV;
                break;

            case FLAG_RCV:
                if (byte == A_TX)
                    state = A_RCV;
                else if (byte != FLAG)
                    state = START;
                break;

            case A_RCV:
                if (byte == C_SET)
                    state = C_RCV;
                else if (byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case C_RCV:
                if (byte == (A_TX ^ C_SET))
                    state = BCC_OK;
                else if (byte == FLAG)
                    state = FLAG_RCV;
                else
                    state = START;
                break;

            case BCC_OK:
                if (byte == FLAG)
                    state = STOP;
                else
                    state = START;
                break;

            case STOP:
                break;
        }
    }
        printf("SET recebido corretamente\n");
   
        unsigned char uaFrame[5];

        uaFrame[0] = FLAG;
        uaFrame[1] = A_TX;
        uaFrame[2] = C_UA;
        uaFrame[3] = A_TX ^ C_UA;
        uaFrame[4] = FLAG;

        writeBytesSerialPort(uaFrame, 5);

        printf("UA enviado\n");
    


    // Close serial port
    if (closeSerialPort() < 0)
    {
        perror("closeSerialPort");
        return -1;
    }

    printf("Serial port %s closed\n", llParameters.serialPort);

    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////
int llReceive(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llCloseTx()
{
    // TODO: Implement this function

    return 0;
}

int llCloseRx()
{
    // TODO: Implement this function

    return 0;
}
