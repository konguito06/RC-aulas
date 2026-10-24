// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <signal.h>

#include <stdio.h>
#include <unistd.h>

#include <stdlib.h>
//------------------------------------------MISC------------------------------------------//
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

#define FLAG 0x7E
#define A_TX 0x03

#define C_SET 0X03
#define C_UA 0x07

//------------------------------------------LLSEND------------------------------------------//

#define ESC 0x7D
#define C_I0 0x00
#define C_I1 0x80
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_REJ1 0x55

static int Ns = 0;
static int savedTimeout = 0;
static int savedRetransmissions = 0;

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

// State Machine

static void updateState(State *state, unsigned char byte,
                        unsigned char expectedA, unsigned char expectedC)
{
    switch (*state)
    {
    case START:
        if (byte == FLAG)
            *state = FLAG_RCV;
        break;

    case FLAG_RCV:
        if (byte == expectedA)
            *state = A_RCV;
        else if (byte != FLAG)
            *state = START;
        break;
    
    case A_RCV:
        if (byte == expectedC)
            *state = C_RCV;
        else if (byte == FLAG)
            *state = FLAG_RCV;
        else
            *state = START;
        break;

    case C_RCV:
        if (byte == (expectedA ^ expectedC))
            *state = BCC_OK;
        else if (byte == FLAG)
            *state = FLAG_RCV;
        else
            *state = START;
        break;

    case BCC_OK:
        if (byte == FLAG)
            *state = STOP;
        else
            *state = START;
        break;

    case STOP:
        break;
    }    
}

//-----------Byte Stuffer----------//

static int stuffByte(unsigned char b, unsigned char *out)
{
    if (b == FLAG)
    {
        out[0] = ESC;
        out[1] = 0x5E;
        return 2;
    }
    if (b == ESC)
    {
        out[0] = ESC;
        out[1] = 0x5D;
        return 2;
    }
    out[0] = b;
    return 1;
}

//---------------------------------supervision frame sender------------------------------------------//

static int expectedNs = 0;   // Ns the receiver expects next

static void sendSupervisionFrame(unsigned char a, unsigned char c)
    {
        unsigned char f[5] = {FLAG, a, c, a ^ c, FLAG};
        writeBytesSerialPort(f, 5);
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
        alarm(0);
        return -1;
    }

    savedTimeout = llParameters.timeout;
    savedRetransmissions = llParameters.nRetransmissions;

    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        alarm(0);
        closeSerialPort();
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
                alarm(0);
                closeSerialPort();
                return -1;
            }

            if (res <= 0)
            {
                continue;
            }
            
            printf("Byte received: 0x%02X\n", byte);

            updateState(&state, byte, A_TX, C_UA);
                
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
        alarm(0);
        closeSerialPort();
        return -1;
    }

    alarm(0);
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
            closeSerialPort();
            return -1;
        }

        if (res == 0)
        {
            continue;
        }

        printf("Byte received: 0x%02X\n", byte);

        updateState(&state, byte, A_TX, C_SET);
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

    return 0;
}

////////////////////////////////////////////////
// LLSEND
////////////////////////////////////////////////
int llSend(const unsigned char *buf, int bufSize)
{
    int maxSize = 2*(bufSize + 1) + 5;
    unsigned char *frame = malloc(maxSize);

    if (frame == NULL)
    {
        perror("malloc");
        return -1;
    }

    //--------------------Frame Building-------------//

    int frameSize = 0;

    unsigned char c = Ns ? C_I1 : C_I0;

    frame[frameSize++] = FLAG;
    frame[frameSize++] = A_TX;
    frame[frameSize++] = c;
    frame[frameSize++] = A_TX ^ c; // BCC1

    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++){
        bcc2 ^= buf[i];      //Xor do byte
        frameSize += stuffByte(buf[i], &frame[frameSize]); //stuffa todos os bytes
    }

    frameSize += stuffByte(bcc2, &frame[frameSize]);   // BCC2 tbm fica stuffed
    frame[frameSize++] = FLAG;

    //----------------sending and waiting for RR/REJ------------------//

    int result = -1;
    int accepted = 0;
    alarmCount = 0;

    while((alarmCount <= savedRetransmissions) && !accepted){
        writeBytesSerialPort(frame, frameSize);

        alarmEnabled = 1;
        alarm(savedTimeout);

        State rrState = START, rejState = START;
        unsigned char rr  = Ns ? C_RR0 : C_RR1;    // RR(!Ns)
        unsigned char rej = Ns ? C_REJ1 : C_REJ0;  // REJ(Ns)
        unsigned char byte;

        while (rrState != STOP && rejState != STOP && alarmEnabled)
        {
            int res = readByteSerialPort(&byte);
            if (res <= 0) continue;

            updateState(&rrState,  byte, A_TX, rr);
            updateState(&rejState, byte, A_TX, rej);
        }

        if (rrState == STOP)
        {
            alarm(0);
            alarmEnabled = 0;
            Ns = !Ns;
            result = bufSize;
            accepted = 1;
        }

        else if (rejState == STOP)
        {
            alarm(0);
            alarmEnabled = 0;
            alarmCount++;       // REJ counts as a retry, loop retransmits
        }
        // else: timeout. The alarm handler already did alarmCount++
    }

    free(frame);
    return result;
}

////////////////////////////////////////////////
// LLRECEIVE
////////////////////////////////////////////////

typedef enum {
    I_START,   // waiting for the opening FLAG
    I_FLAG,    // got FLAG, expecting address
    I_A,       // got address, expecting control (I0 or I1)
    I_C,       // got control, expecting BCC1
    I_DATA     // header is valid, collecting data until the closing FLAG
} IState;


int llReceive(unsigned char *packet)
{   
    IState state = I_START;
    unsigned char a = 0, c = 0, byte;
    int i = 0;          // how many bytes are in packet so far
    int escape = 0;     // 1 = previous byte was 0x7D

    while(1){
        
        int res = readByteSerialPort(&byte);
        if (res < 0) return -1;
        if (res == 0) continue;

        switch(state){
            case I_START:
                if (byte == FLAG) state = I_FLAG;
                break;

            case I_FLAG:
                if (byte == A_TX) { a = byte; state = I_A; }
                else if (byte != FLAG) state = I_START;
                break;

            case I_A:
                if (byte == C_I0 || byte == C_I1) { c = byte; state = I_C; }
                else if (byte == FLAG) state = I_FLAG;
                else state = I_START;
                break;

            case I_C:
                if (byte == (a ^ c)) { i = 0; escape = 0; state = I_DATA; }
                else if (byte == FLAG) state = I_FLAG;
                else state = I_START;
                break;
            
            case I_DATA:
                if (i >= MAX_PAYLOAD_SIZE + 1)
                {
                    i = 0; escape = 0; state = I_START;
                    break;
                }

                if (escape)
                {
                    if (byte == FLAG)
                    {
                        i = 0; escape = 0; state = I_FLAG;
                        break;
                    }
                    
                    packet[i++] = byte ^ 0x20;   // desfazer stuffing
                    escape = 0;
                }
                else if (byte == ESC)
                {
                    escape = 1;                  // proximo byte e o stuffed 
                }
                else if (byte == FLAG)
                {
                    if (i < 1)
                    {
                        escape = 0;
                        state = I_FLAG;
                        break;
                    }

                    unsigned char bcc2 = 0;
                    for (int k = 0; k < i - 1; k++) bcc2 ^= packet[k];

                    int ns = (c == C_I1);
                    unsigned char rr  = ns ? C_RR0  : C_RR1;
                    unsigned char rej = ns ? C_REJ1 : C_REJ0;

                    if (bcc2 == packet[i - 1])
                    {
                        sendSupervisionFrame(A_TX, rr);

                        if (ns == expectedNs)
                        {
                            expectedNs = !expectedNs;
                            return i - 1;
                        }
                    }
                    else
                    {
                        if (ns == expectedNs) sendSupervisionFrame(A_TX, rej);
                        else                  sendSupervisionFrame(A_TX, rr);
                    }

                    i = 0; escape = 0; state = I_START;
                }
                else
                {
                    packet[i++] = byte;
                }
                break;
        }
    }
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
