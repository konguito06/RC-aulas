// RCOM 2026/2027
// Link layer protocol implementation

#define _POSIX_SOURCE 1 // POSIX compliant source

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "link_layer.h"
#include "serial_port.h"

//------------------------------- PROTOCOL CONSTANTS -------------------------------------//

#define FLAG 0x7E
#define ESC 0x7D

#define A_TX 0x03
#define A_RX 0x01

#define C_SET 0X03
#define C_UA 0x07
#define C_DISC 0X0B

#define C_I0 0x00
#define C_I1 0x80
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_REJ1 0x55

//------------------------------------ GLOBAL STATE ---------------------------------------//

static int Ns = 0;         // bit number for the next frame we send
static int expectedNs = 0; // bit number for the next frame we want
static int savedTimeout = 0;
static int savedRetransmissions = 0;

volatile sig_atomic_t alarmEnabled = 0;
volatile sig_atomic_t alarmCount = 0;

//-------------------------------------- TYPES --------------------------------------------//

typedef enum { 
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} State;

typedef enum {
    I_START,   // waiting for the opening FLAG
    I_FLAG,    // got FLAG, expecting address
    I_A,       // got address, expecting control (I0 or I1)
    I_C,       // got control, expecting BCC1
    I_DATA     // header is valid, collecting data until the closing FLAG
} IState;

//--------------------------------- SHARED HELPERS ----------------------------------------//

void alarmHandler(int signal) {
    (void)signal;
    alarmEnabled = 0;
    alarmCount++;
}

static void updateState(State *state, unsigned char byte,
                        unsigned char expectedA, unsigned char expectedC)
{
    // Check each header byte in order, then wait for the closing flag.
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

static int writeFrame(const unsigned char *frame, int frameSize)
{
    // Keep writing until the whole frame is out.
    int written = 0;
    while (written < frameSize)
    {
        int res = writeBytesSerialPort(frame + written, frameSize - written);
        if (res <= 0)
            return -1;
        written += res;
    }
    return 0;
}

static int sendSupervisionFrame(unsigned char a, unsigned char c)
{
    // Supervisory frames have no data, just the header and flags.
    unsigned char f[5] = {FLAG, a, c, a ^ c, FLAG};
    return writeFrame(f, 5);
}

//------------------------------------ LINK OPEN ------------------------------------------//

int llOpenTx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        alarm(0);
        return -1;
    }

    savedTimeout = llParameters.timeout;
    savedRetransmissions = llParameters.nRetransmissions;
    Ns = 0;
    expectedNs = 0;

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

    // SET starts the connection; keep retrying until UA arrives.
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

        if (writeFrame(setFrame, 5) < 0)
        {
            alarm(0);
            closeSerialPort();
            return -1;
        }

        alarmEnabled = 1;
        alarm(llParameters.timeout);

        State state = START;
        unsigned char byte;

        while (state != STOP && alarmEnabled)
        {
            int res = readByteSerialPort(&byte);

            if (res < 0 && errno == EINTR)
            {
                if (!alarmEnabled)
                    break;
                continue;
            }

            if (res < 0)
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
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    savedTimeout = llParameters.timeout;
    savedRetransmissions = llParameters.nRetransmissions;
    Ns = 0;
    expectedNs = 0;

    printf("Serial port %s opened\n", llParameters.serialPort);

    State state = START;
    unsigned char byte;

    // Wait for SET before replying with UA.
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

        updateState(&state, byte, A_TX, C_SET);
    }
        
    printf("SET recebido corretamente\n");
   
    unsigned char uaFrame[5];

    uaFrame[0] = FLAG;
    uaFrame[1] = A_TX;
    uaFrame[2] = C_UA;
    uaFrame[3] = A_TX ^ C_UA;
    uaFrame[4] = FLAG;

    if (writeFrame(uaFrame, 5) < 0)
    {
        closeSerialPort();
        return -1;
    }

    printf("UA enviado\n");

    return 0;
}

//------------------------------------ LINK SEND ------------------------------------------//

static int stuffByte(unsigned char b, unsigned char *out)
{
    // Escape FLAG and ESC so they can't look like frame boundaries.
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

// Build an information frame and wait for its RR/REJ response.
int llSend(const unsigned char *buf, int bufSize)
{
    if (buf == NULL || bufSize < 0 || bufSize > MAX_PAYLOAD_SIZE)
        return -1;

    int maxSize = 2*(bufSize + 1) + 5;
    unsigned char *frame = malloc(maxSize);

    if (frame == NULL)
    {
        perror("malloc");
        return -1;
    }

    //-------------------- Build the information frame --------------------//

    int frameSize = 0;

    unsigned char c = Ns ? C_I1 : C_I0; // mark this frame with its sequence bit

    frame[frameSize++] = FLAG;
    frame[frameSize++] = A_TX;
    frame[frameSize++] = c;
    frame[frameSize++] = A_TX ^ c; // BCC1

    // BCC2 checks the payload; both it and the payload need stuffing.
    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++){
        bcc2 ^= buf[i];      //Xor do byte
        frameSize += stuffByte(buf[i], &frame[frameSize]); //stuffa todos os bytes
    }

    frameSize += stuffByte(bcc2, &frame[frameSize]);   // BCC2 tbm fica stuffed
    frame[frameSize++] = FLAG;

    //-------------------- Send and wait for RR/REJ -----------------------//

    int result = -1;
    int accepted = 0;
    alarmCount = 0;

    while((alarmCount <= savedRetransmissions) && !accepted){
        if (writeFrame(frame, frameSize) < 0)
        {
            free(frame);
            return -1;
        }

        // Retry if the timer expires or the receiver asks for a resend.
        alarmEnabled = 1;
        alarm(savedTimeout);

        State rrState = START, rejState = START;
        unsigned char rr  = Ns ? C_RR0 : C_RR1;    // RR(!Ns)
        unsigned char rej = Ns ? C_REJ1 : C_REJ0;  // REJ(Ns)
        unsigned char byte;

        while (rrState != STOP && rejState != STOP && alarmEnabled)
        {
            int res = readByteSerialPort(&byte);
            if (res < 0 && errno == EINTR)
            {
                if (!alarmEnabled)
                    break;
                continue;
            }
            if (res < 0)
            {
                alarm(0);
                alarmEnabled = 0;
                free(frame);
                return -1;
            }
            if (res == 0) continue;

            updateState(&rrState,  byte, A_TX, rr);
            updateState(&rejState, byte, A_TX, rej);
        }

        if (rrState == STOP)
        {
            alarm(0);
            alarmEnabled = 0;
            // The RR confirms the next sequence bit, so advance ours.
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

//----------------------------------- LINK RECEIVE ----------------------------------------//

int llReceive(unsigned char *packet)
{   
    if (packet == NULL)
        return -1;

    IState state = I_START;
    unsigned char a = 0, c = 0, byte;
    unsigned char frameData[MAX_PAYLOAD_SIZE + 1]; // payload plus BCC2
    int i = 0;          // bytes collected so far
    int escape = 0;     // previous byte was ESC
    State setState = START;

    while(1){
        
        int res = readByteSerialPort(&byte);
        if (res < 0) return -1;
        if (res == 0) continue;

        // A retransmitted SET means the UA from llOpenRx was lost. Keep
        // answering it while waiting for information frames.
        if (state != I_DATA)
        {
            updateState(&setState, byte, A_TX, C_SET);
            if (setState == STOP)
            {
                if (sendSupervisionFrame(A_TX, C_UA) < 0)
                    return -1;
                setState = START;
            }
        }

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
                if (escape)
                {
                    if (byte == FLAG)
                    {
                        i = 0; escape = 0; state = I_FLAG;
                        break;
                    }

                    if (i >= MAX_PAYLOAD_SIZE + 1)
                    {
                        i = 0; escape = 0; state = I_START;
                        break;
                    }
                    frameData[i++] = byte ^ 0x20;   // desfazer stuffing
                    escape = 0;
                }
                else if (byte == ESC)
                {
                    escape = 1;                  // proximo byte e o stuffed 
                }
                else if (byte == FLAG)
                {
                    if (i < 1) { state = I_FLAG; break; }   // no room for BCC2
                    int frameNs = (c == C_I1);
                    unsigned char bcc2 = 0;
                    
                    for (int k = 0; k < i - 1; k++)
                        bcc2 ^= frameData[k];               // XOR of the data only
                    int bcc2Ok = (bcc2 == frameData[i - 1]); // last byte is BCC2

                    if (frameNs == expectedNs)
                    {
                        if (bcc2Ok)
                        {
                            // Accept this frame and ask for the next sequence bit.
                            expectedNs = !expectedNs;
                            if (sendSupervisionFrame(A_TX, expectedNs ? C_RR1 : C_RR0) < 0)
                                return -1;
                            for (int k = 0; k < i - 1; k++)
                                packet[k] = frameData[k];
                            return i - 1;                   // data length, without BCC2
                        }
                        else
                        {
                            // Bad payload checksum: ask the sender to retry it.
                            if (sendSupervisionFrame(A_TX, expectedNs ? C_REJ1 : C_REJ0) < 0)
                                return -1;
                        }
                    }
                    else
                    {
                        // It is a duplicate; resend RR in case the last one was lost.
                        if (sendSupervisionFrame(A_TX, expectedNs ? C_RR1 : C_RR0) < 0)
                            return -1;
                    }

                    state = I_START;   // frame handled, wait for the next one
                    i = 0;
                }
                else
                {
                    if (i >= MAX_PAYLOAD_SIZE + 1)
                    {
                        i = 0; escape = 0; state = I_START;
                        break;
                    }
                    frameData[i++] = byte;
                }
                break;
        }
    }
}

//------------------------------------ LINK CLOSE -----------------------------------------//

int llCloseTx()
{
    alarmCount = 0;
    alarmEnabled = 0;
    int discReceived = 0;

    while ((alarmCount <= savedRetransmissions) && !discReceived)
    {
        // Start disconnect; wait for the receiver's DISC.
        if (sendSupervisionFrame(A_TX, C_DISC) < 0)
        {
            closeSerialPort();
            return -1;
        }

        alarmEnabled = 1;
        alarm(savedTimeout);

        State state = START;
        unsigned char byte;

        while (state != STOP && alarmEnabled)
        {
            int res = readByteSerialPort(&byte);
            if (res < 0 && errno == EINTR)
            {
                if (!alarmEnabled)
                    break;
                continue;
            }
            if (res < 0)
            {
                alarm(0);
                alarmEnabled = 0;
                closeSerialPort();
                return -1;
            }
            if (res == 0) continue;

            updateState(&state, byte, A_RX, C_DISC);
        }

        if (state == STOP)
        {
            alarm(0);
            alarmEnabled = 0;
            discReceived = 1;
        }
    }

    if (!discReceived)
    {
        printf("Falha ao fechar ligação\n");
        closeSerialPort();
        return -1;
    }

    // The transmitter's reply to the receiver's DISC uses the receiver address.
    if (sendSupervisionFrame(A_RX, C_UA) < 0)
    {
        closeSerialPort();
        return -1;
    }

    closeSerialPort();
    return 0;
}

int llCloseRx()
{
    State state = START;
    unsigned char byte;

    // Wait for the sender to start closing the connection.
    while (state != STOP)
    {
        int res = readByteSerialPort(&byte);
        if (res < 0) { closeSerialPort(); return -1; }
        if (res == 0) continue;

        updateState(&state, byte, A_TX, C_DISC);
    }

    if (sendSupervisionFrame(A_RX, C_DISC) < 0)
    {
        closeSerialPort();
        return -1;
    }

    State uaState = START, discState = START;

    while (uaState != STOP)
    {
        int res = readByteSerialPort(&byte);
        if (res < 0) { closeSerialPort(); return -1; }
        if (res == 0) continue;

        updateState(&uaState, byte, A_RX, C_UA);
        updateState(&discState, byte, A_TX, C_DISC);

        if (discState == STOP)
        {
            if (sendSupervisionFrame(A_RX, C_DISC) < 0)
            {
                closeSerialPort();
                return -1;
            }
            discState = START;
        }
    }

    closeSerialPort();
    return 0;
}
