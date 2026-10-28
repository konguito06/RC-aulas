// RCOM 2026/2027
//
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdio.h>
#include <string.h>

//------------------------------- PACKET CONSTANTS ---------------------------------------//
// Shared by transmitter and receiver - both sides must agree on these.
//
// Control packet (START / END):  [C][T0][L0][V0...][T1][L1][V1...]
// Data packet:                   [C=2][L2][L1][data...]   size = 256*L2 + L1

#define C_DATA 2
#define C_START 1
#define C_END 3

#define T_FILESIZE 0 // V = file size, big-endian, L bytes
#define T_FILENAME 1 // V = file name, L chars, not null-terminated

#define DATA_HEADER_SIZE 3                              // C, L2, L1
#define DATA_CHUNK_SIZE (MAX_PAYLOAD_SIZE - DATA_HEADER_SIZE)

//---------------------------------- TRANSMITTER -----------------------------------------//

// Builds a START/END control packet: C, then TLVs for file size and file name.
// Returns the packet size, or -1 if it doesn't fit in MAX_PAYLOAD_SIZE.
static int buildControlPacket(unsigned char c, long fileSize, const char *filename,
                              unsigned char *packet)
{
    int size = 0;
    packet[size++] = c;

    // File size goes big-endian, using only as many bytes as needed
    unsigned char sizeBytes[sizeof(long)];
    int nSizeBytes = 0;
    long tmp = fileSize;
    do {
        sizeBytes[nSizeBytes++] = tmp & 0xFF;
        tmp >>= 8;
    } while (tmp > 0 && nSizeBytes < (int)sizeof(long));

    packet[size++] = T_FILESIZE;
    packet[size++] = nSizeBytes;
    for (int i = nSizeBytes - 1; i >= 0; i--)
        packet[size++] = sizeBytes[i];

    int nameLen = strlen(filename);
    if (nameLen > 255 || size + 2 + nameLen > MAX_PAYLOAD_SIZE)
        return -1;

    packet[size++] = T_FILENAME;
    packet[size++] = nameLen;
    memcpy(&packet[size], filename, nameLen);
    size += nameLen;

    return size;
}

// Sends the whole file: START packet, data packets, END packet.
// Returns 0 on success or -1 on error.
static int sendFile(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        perror("fopen");
        return -1;
    }

    long fileSize = -1;
    if (fseek(file, 0, SEEK_END) == 0)
        fileSize = ftell(file);
    if (fileSize < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        perror("Could not get file size");
        fclose(file);
        return -1;
    }

    // Only send the name, not the local path (e.g. "../files/penguin.gif" -> "penguin.gif")
    const char *slash = strrchr(filename, '/');
    const char *name = slash ? slash + 1 : filename;

    unsigned char packet[MAX_PAYLOAD_SIZE];

    //START packet//

    int controlSize = buildControlPacket(C_START, fileSize, name, packet);
    if (controlSize < 0)
    {
        printf("File name too long for control packet\n");
        fclose(file);
        return -1;
    }
    if (llSend(packet, controlSize) < 0)
    {
        printf("Failed to send START packet\n");
        fclose(file);
        return -1;
    }

    //Data packets//

    long bytesSent = 0;
    int nRead;
    while ((nRead = fread(&packet[DATA_HEADER_SIZE], 1, DATA_CHUNK_SIZE, file)) > 0)
    {
        packet[0] = C_DATA;
        packet[1] = (nRead >> 8) & 0xFF; // L2
        packet[2] = nRead & 0xFF;        // L1

        if (llSend(packet, DATA_HEADER_SIZE + nRead) < 0)
        {
            printf("Failed to send data packet at byte %ld\n", bytesSent);
            fclose(file);
            return -1;
        }

        bytesSent += nRead;
        printf("Sent %ld / %ld bytes\n", bytesSent, fileSize);
    }

    // fread returning 0 can mean EOF or a read error - don't send END on an error
    if (ferror(file) || bytesSent != fileSize)
    {
        printf("Error reading file (sent %ld of %ld bytes)\n", bytesSent, fileSize);
        fclose(file);
        return -1;
    }

    fclose(file);

    //END packet//

    controlSize = buildControlPacket(C_END, fileSize, name, packet);
    if (controlSize < 0 || llSend(packet, controlSize) < 0)
    {
        printf("Failed to send END packet\n");
        return -1;
    }

    return 0;
}

//------------------------------------ RECEIVER ------------------------------------------//

// Receives the whole file: START packet, data packets, END packet,
// and writes it to filename.
// Returns 0 on success or -1 on error.
static int receiveFile(const char *filename)
{
    //to do
    (void)filename;
    return -1;
}

//----------------------------------- MAIN -----------------------------------------//

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer llParameters = {
        .baudRate = baudRate,
        .nRetransmissions = nTries,
        .timeout = timeout,
    };
    strncpy(llParameters.serialPort, serialPort, sizeof(llParameters.serialPort) - 1);
    llParameters.serialPort[sizeof(llParameters.serialPort) - 1] = '\0';

    if (strcmp(role, "tx") == 0)
    {
        if (llOpenTx(llParameters) < 0)
        {
            printf("llOpenTx failed\n");
            return;
        }

        if (sendFile(filename) < 0)
            printf("File transmission failed\n");
        else
            printf("File sent successfully\n");

        if (llCloseTx() < 0)
            printf("llCloseTx failed\n");
    }
    else if (strcmp(role, "rx") == 0)
    {
        if (llOpenRx(llParameters) < 0)
        {
            printf("llOpenRx failed\n");
            return;
        }

        if (receiveFile(filename) < 0)
            printf("File reception failed\n");
        else
            printf("File received successfully\n");

        if (llCloseRx() < 0)
            printf("llCloseRx failed\n");
    }
    else
    {
        printf("Invalid role: %s. Must be 'tx' or 'rx'.\n", role);
    }
}
