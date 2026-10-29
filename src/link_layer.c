// RCOM 2026/2027
// Link layer protocol implementation

#define _POSIX_C_SOURCE 200809L // POSIX compliant source (needed for clock_gettime)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "link_layer.h"
#include "serial_port.h"

//------------------------------- PROTOCOL CONSTANTS -------------------------------------//

#define FLAG 0x7E
#define ESC 0x7D
#define ESC_FLAG 0x5E // FLAG ^ 0x20
#define ESC_ESC 0x5D  // ESC ^ 0x20

#define A_TX 0x03
#define A_RX 0x01

#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B

#define C_I0 0x00
#define C_I1 0x80
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_REJ1 0x55

//------------------------------------ GLOBAL STATE ---------------------------------------//

static int Ns = 0;          // bit number for the next frame we send
static int expectedNs = 0;  // bit number for the next frame we want
static int savedTimeout = 0;
static int savedRetransmissions = 0;
static int discPending = 0; // rx: llReceive already consumed the transmitter's DISC

static volatile sig_atomic_t alarmEnabled = 0;
static volatile sig_atomic_t alarmCount = 0;

//------------------------------------ STATISTICS -----------------------------------------//

static struct
{
    struct timespec start, end; // from connection established to close requested
    long framesSent;            // tx: I-frames acknowledged with RR
    long retransmissions;       // tx: I-frames written again
    long timeouts;              // tx: waits for RR/REJ that timed out
    long rejReceived;           // tx: REJs received
    long framesReceived;        // rx: new I-frames delivered to the application
    long duplicates;            // rx: repeated I-frames acknowledged again
    long rejSent;               // rx: I-frames rejected because of a bad BCC2
    long bytes;                 // payload bytes sent (tx) or delivered (rx)
} stats;

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

typedef struct {
    IState state;
    unsigned char c;                          // control byte (I0 or I1)
    unsigned char data[MAX_PAYLOAD_SIZE + 1]; // destuffed payload plus BCC2
    int len;                                  // bytes in data
    int escape;                               // previous byte was ESC
    int corrupt;                              // bad escape or too long: reject it
} IParser;

//--------------------------------- SHARED HELPERS ----------------------------------------//

static void alarmHandler(int signal)
{
    (void)signal;
    alarmEnabled = 0;
    alarmCount++;
}

static int installAlarmHandler(void)
{
    // No SA_RESTART, so the alarm interrupts a blocked read with EINTR.
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;

    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    }
    return 0;
}

static void initLink(LinkLayer llParameters)
{
    savedTimeout = llParameters.timeout;
    savedRetransmissions = llParameters.nRetransmissions;
    Ns = 0;
    expectedNs = 0;
    discPending = 0;
    memset(&stats, 0, sizeof(stats));
}

// Reads one byte. Returns 1 if a byte was read, 0 if none arrived or the alarm
// interrupted the read, -1 on error.
static int readByte(unsigned char *byte)
{
    int res = readByteSerialPort(byte);
    if (res < 0 && errno == EINTR)
        return 0;
    return res;
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

static void printStats(int isTx)
{
    double secs = (stats.end.tv_sec - stats.start.tv_sec) +
                  (stats.end.tv_nsec - stats.start.tv_nsec) / 1e9;
    double bitrate = secs > 0 ? stats.bytes * 8 / secs : 0;

    printf("\n---------- Link layer statistics (%s) ----------\n", isTx ? "tx" : "rx");
    if (isTx)
    {
        printf("I-frames sent:          %ld\n", stats.framesSent);
        printf("Retransmissions:        %ld\n", stats.retransmissions);
        printf("Timeouts:               %ld\n", stats.timeouts);
        printf("REJ received:           %ld\n", stats.rejReceived);
        printf("Payload bytes sent:     %ld\n", stats.bytes);
    }
    else
    {
        printf("I-frames received:      %ld\n", stats.framesReceived);
        printf("Duplicates received:    %ld\n", stats.duplicates);
        printf("REJ sent:               %ld\n", stats.rejSent);
        printf("Payload bytes received: %ld\n", stats.bytes);
    }
    printf("Transfer time:          %.3f s\n", secs);
    printf("Throughput:             %.0f bit/s\n", bitrate);
    printf("------------------------------------------------\n");
}

//------------------------------------ LINK OPEN ------------------------------------------//

int llOpenTx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    initLink(llParameters);

    if (installAlarmHandler() < 0)
    {
        closeSerialPort();
        return -1;
    }

    // SET starts the connection; keep retrying until UA arrives.
    alarmCount = 0;
    alarmEnabled = 0;
    int uaReceived = 0;

    while (alarmCount <= savedRetransmissions && !uaReceived)
    {
        if (sendSupervisionFrame(A_TX, C_SET) < 0)
        {
            alarm(0);
            closeSerialPort();
            return -1;
        }

        alarmEnabled = 1;
        alarm(savedTimeout);

        State state = START;
        unsigned char byte;

        while (state != STOP && alarmEnabled)
        {
            int res = readByte(&byte);
            if (res < 0)
            {
                alarm(0);
                closeSerialPort();
                return -1;
            }
            if (res == 0) continue;

            updateState(&state, byte, A_TX, C_UA);
        }

        if (state == STOP)
        {
            alarm(0);
            alarmEnabled = 0;
            uaReceived = 1;
        }
    }

    if (!uaReceived)
    {
        printf("No UA received, could not open the connection\n");
        closeSerialPort();
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &stats.start);
    return 0;
}

int llOpenRx(LinkLayer llParameters)
{
    if (openSerialPort(llParameters.serialPort, llParameters.baudRate) < 0)
    {
        perror("openSerialPort");
        return -1;
    }

    initLink(llParameters);

    // The receiver only uses the alarm while closing, but set it up here.
    if (installAlarmHandler() < 0)
    {
        closeSerialPort();
        return -1;
    }

    State state = START;
    unsigned char byte;

    // Wait for SET before replying with UA.
    while (state != STOP)
    {
        int res = readByte(&byte);
        if (res < 0)
        {
            closeSerialPort();
            return -1;
        }
        if (res == 0) continue;

        updateState(&state, byte, A_TX, C_SET);
    }

    if (sendSupervisionFrame(A_TX, C_UA) < 0)
    {
        closeSerialPort();
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &stats.start);
    return 0;
}

//------------------------------------ LINK SEND ------------------------------------------//

static int stuffByte(unsigned char b, unsigned char *out)
{
    // Escape FLAG and ESC so they can't look like frame boundaries.
    if (b == FLAG)
    {
        out[0] = ESC;
        out[1] = ESC_FLAG;
        return 2;
    }
    if (b == ESC)
    {
        out[0] = ESC;
        out[1] = ESC_ESC;
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
    for (int i = 0; i < bufSize; i++)
    {
        bcc2 ^= buf[i];
        frameSize += stuffByte(buf[i], &frame[frameSize]);
    }

    frameSize += stuffByte(bcc2, &frame[frameSize]); // BCC2 is stuffed too
    frame[frameSize++] = FLAG;

    //-------------------- Send and wait for RR/REJ -----------------------//

    int result = -1;
    int accepted = 0;
    int attempts = 0;
    alarmCount = 0;

    while ((alarmCount <= savedRetransmissions) && !accepted)
    {
        if (attempts++ > 0)
            stats.retransmissions++;

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
            int res = readByte(&byte);
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
            stats.framesSent++;
            stats.bytes += bufSize;
            result = bufSize;
            accepted = 1;
        }
        else if (rejState == STOP)
        {
            // A REJ proves the link is alive, so it doesn't use up the retry
            // budget (that is only for timeouts). The loop retransmits.
            alarm(0);
            alarmEnabled = 0;
            stats.rejReceived++;
        }
        else
        {
            // Timeout. The alarm handler already did alarmCount++
            stats.timeouts++;
        }
    }

    free(frame);
    return result;
}

//----------------------------------- LINK RECEIVE ----------------------------------------//

static void resetParser(IParser *p)
{
    p->state = I_START;
    p->len = 0;
    p->escape = 0;
    p->corrupt = 0;
}

// Feeds one byte to the I-frame parser. Returns 1 when a frame with a valid
// header has been closed by a FLAG (p->c and p->data hold it), 0 otherwise.
static int parseIByte(IParser *p, unsigned char byte)
{
    switch (p->state)
    {
    case I_START:
        if (byte == FLAG) p->state = I_FLAG;
        break;

    case I_FLAG:
        if (byte == A_TX) p->state = I_A;
        else if (byte != FLAG) p->state = I_START;
        break;

    case I_A:
        if (byte == C_I0 || byte == C_I1) { p->c = byte; p->state = I_C; }
        else if (byte == FLAG) p->state = I_FLAG;
        else p->state = I_START;
        break;

    case I_C:
        if (byte == (A_TX ^ p->c))
        {
            p->len = 0;
            p->escape = 0;
            p->corrupt = 0;
            p->state = I_DATA;
        }
        else if (byte == FLAG) p->state = I_FLAG;
        else p->state = I_START;
        break;

    case I_DATA:
        if (byte == FLAG)
        {
            // The frame ends here. This FLAG can also open the next one.
            int complete = (p->len >= 1); // at least BCC2
            if (p->escape)
                p->corrupt = 1;           // ESC right before the FLAG
            p->escape = 0;
            p->state = I_FLAG;
            return complete;
        }

        if (p->escape)
        {
            if (byte != ESC_FLAG && byte != ESC_ESC)
                p->corrupt = 1;           // noise; the frame will be rejected
            byte ^= 0x20;                 // undo stuffing
            p->escape = 0;
        }
        else if (byte == ESC)
        {
            p->escape = 1;                // next byte is the stuffed one
            break;
        }

        // A full frame fills the buffer exactly, so noise that breaks an ESC
        // pair (one byte becomes two) overflows it. Keep reading until the
        // FLAG and reject the frame, instead of dropping it and waiting for
        // the sender's timeout.
        if (p->len < MAX_PAYLOAD_SIZE + 1)
            p->data[p->len++] = byte;
        else
            p->corrupt = 1;
        break;
    }
    return 0;
}

static int payloadOk(const IParser *p)
{
    unsigned char bcc2 = 0;
    for (int k = 0; k < p->len - 1; k++)
        bcc2 ^= p->data[k];             // XOR of the data only
    return !p->corrupt && bcc2 == p->data[p->len - 1]; // last byte is BCC2
}

// Answers a complete I-frame with RR or REJ.
// Returns 1 if it is new and valid (the caller delivers it), 0 if it was a
// duplicate or corrupted, -1 on write error.
static int answerIFrame(const IParser *p)
{
    int frameNs = (p->c == C_I1);

    if (frameNs != expectedNs)
    {
        // It is a duplicate; resend RR in case the last one was lost.
        stats.duplicates++;
        return sendSupervisionFrame(A_TX, expectedNs ? C_RR1 : C_RR0) < 0 ? -1 : 0;
    }

    if (!payloadOk(p))
    {
        // Bad payload checksum: ask the sender to retry it.
        stats.rejSent++;
        return sendSupervisionFrame(A_TX, expectedNs ? C_REJ1 : C_REJ0) < 0 ? -1 : 0;
    }

    // Accept this frame and ask for the next sequence bit.
    expectedNs = !expectedNs;
    return sendSupervisionFrame(A_TX, expectedNs ? C_RR1 : C_RR0) < 0 ? -1 : 1;
}

int llReceive(unsigned char *packet)
{
    if (packet == NULL)
        return -1;

    IParser parser;
    resetParser(&parser);
    State setState = START, discState = START;
    unsigned char byte;

    while (1)
    {
        int res = readByte(&byte);
        if (res < 0) return -1;
        if (res == 0) continue;

        // Supervision frames can't appear inside a stuffed I-frame, so it is
        // safe to look for them on every byte.

        // A retransmitted SET means the UA from llOpenRx was lost.
        updateState(&setState, byte, A_TX, C_SET);
        if (setState == STOP)
        {
            if (sendSupervisionFrame(A_TX, C_UA) < 0)
                return -1;
            setState = START;
        }

        // A DISC means the transmitter is closing (maybe it gave up). Stop
        // here instead of waiting forever; llCloseRx will answer it.
        updateState(&discState, byte, A_TX, C_DISC);
        if (discState == STOP)
        {
            discPending = 1;
            return -1;
        }

        if (!parseIByte(&parser, byte))
            continue;

        int answer = answerIFrame(&parser);
        if (answer < 0)
            return -1;
        if (answer == 1)
        {
            int size = parser.len - 1;      // data length, without BCC2
            memcpy(packet, parser.data, size);
            stats.framesReceived++;
            stats.bytes += size;
            return size;
        }
    }
}

//------------------------------------ LINK CLOSE -----------------------------------------//

static int closeTx(void)
{
    alarmCount = 0;
    alarmEnabled = 0;
    int discReceived = 0;

    while ((alarmCount <= savedRetransmissions) && !discReceived)
    {
        // Start disconnect; wait for the receiver's DISC.
        if (sendSupervisionFrame(A_TX, C_DISC) < 0)
            return -1;

        alarmEnabled = 1;
        alarm(savedTimeout);

        State state = START;
        unsigned char byte;

        while (state != STOP && alarmEnabled)
        {
            int res = readByte(&byte);
            if (res < 0)
            {
                alarm(0);
                alarmEnabled = 0;
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
        printf("No DISC received, could not close the connection properly\n");
        return -1;
    }

    // The transmitter's reply to the receiver's DISC uses the receiver address.
    return sendSupervisionFrame(A_RX, C_UA);
}

int llCloseTx()
{
    clock_gettime(CLOCK_MONOTONIC, &stats.end);
    int result = closeTx();
    printStats(TRUE);
    closeSerialPort();
    return result;
}

static int closeRx(void)
{
    IParser parser;
    resetParser(&parser);
    unsigned char byte;

    // Wait for the transmitter's DISC, unless llReceive already got it.
    // If the RR for the last I-frame was lost, the transmitter is still
    // repeating that frame, so keep acknowledging it meanwhile.
    State discState = discPending ? STOP : START;
    discPending = 0;

    while (discState != STOP)
    {
        int res = readByte(&byte);
        if (res < 0) return -1;
        if (res == 0) continue;

        updateState(&discState, byte, A_TX, C_DISC);

        if (parseIByte(&parser, byte))
        {
            int answer = answerIFrame(&parser);
            if (answer < 0)
                return -1;
            if (answer == 1)
                printf("Warning: new I-frame received while closing, discarded\n");
        }
    }

    // Reply with DISC and wait for the final UA. Resend our DISC if the UA
    // doesn't arrive in time, or right away if the transmitter repeats its
    // DISC (ours was lost).
    alarmCount = 0;
    alarmEnabled = 0;
    int uaReceived = 0;

    while ((alarmCount <= savedRetransmissions) && !uaReceived)
    {
        if (sendSupervisionFrame(A_RX, C_DISC) < 0)
            return -1;

        alarmEnabled = 1;
        alarm(savedTimeout);

        State uaState = START;
        discState = START;

        while (uaState != STOP && discState != STOP && alarmEnabled)
        {
            int res = readByte(&byte);
            if (res < 0)
            {
                alarm(0);
                alarmEnabled = 0;
                return -1;
            }
            if (res == 0) continue;

            updateState(&uaState, byte, A_RX, C_UA);
            updateState(&discState, byte, A_TX, C_DISC);
        }

        if (uaState == STOP)
        {
            alarm(0);
            alarmEnabled = 0;
            uaReceived = 1;
        }
        else if (discState == STOP)
        {
            alarm(0);
            alarmEnabled = 0;
        }
        // else: timeout. The alarm handler already did alarmCount++
    }

    if (!uaReceived)
    {
        printf("No UA received, could not close the connection properly\n");
        return -1;
    }

    return 0;
}

int llCloseRx()
{
    clock_gettime(CLOCK_MONOTONIC, &stats.end);
    int result = closeRx();
    printStats(FALSE);
    closeSerialPort();
    return result;
}
