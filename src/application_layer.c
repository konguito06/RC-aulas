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

// Parses a START/END control packet whose first byte must be expectedC.
// Extracts the file size (T_FILESIZE) and the file name (T_FILENAME).
// name must have room for 256 chars. Returns 0 on success or -1 if the packet
// is malformed.
static int parseControlPacket(const unsigned char *packet, int size, unsigned char expectedC,
                              long *fileSize, char *name)
{
    if (size < 1 || packet[0] != expectedC)
        return -1;

    int gotSize = 0;
    name[0] = '\0';

    int i = 1;
    while (i < size)
    {
        if (i + 2 > size) // no room for T and L
            return -1;

        unsigned char t = packet[i];
        unsigned char l = packet[i + 1];
        i += 2;

        if (i + l > size) // V runs past the end of the packet
            return -1;

        if (t == T_FILESIZE)
        {
            if (l < 1 || l > sizeof(long))
                return -1;
            if (l == sizeof(long) && (packet[i] & 0x80)) // would overflow a long
                return -1;

            long value = 0;
            for (int k = 0; k < l; k++)
                value = (value << 8) | packet[i + k];

            *fileSize = value;
            gotSize = 1;
        }
        else if (t == T_FILENAME)
        {
            memcpy(name, &packet[i], l);
            name[l] = '\0';
        }
        // Unknown TLVs are skipped

        i += l;
    }

    return gotSize ? 0 : -1;
}

// Receives the whole file: START packet, data packets, END packet,
// and writes it to filename.
// Returns 0 on success or -1 on error.
static int receiveFile(const char *filename)
{
    unsigned char packet[MAX_PAYLOAD_SIZE];
    long fileSize = 0, endSize = 0;
    char sentName[256], endName[256];
    FILE *file = NULL;

    //START packet//

    int n = llReceive(packet);
    if (n < 0)
    {
        printf("Failed to receive START packet\n");
        return -1;
    }
    if (parseControlPacket(packet, n, C_START, &fileSize, sentName) < 0)
    {
        printf("First packet is not a valid START packet\n");
        return -1;
    }
    printf("Receiving \"%s\" (%ld bytes), saving as \"%s\"\n", sentName, fileSize, filename);

    file = fopen(filename, "wb");
    if (file == NULL)
    {
        perror("fopen");
        return -1;
    }

    //Data packets//

    long bytesReceived = 0;
    while (1)
    {
        n = llReceive(packet);
        if (n < 0)
        {
            printf("Connection lost before END packet (received %ld of %ld bytes)\n",
                   bytesReceived, fileSize);
            goto fail;
        }
        if (n == 0)
        {
            printf("Empty packet received\n");
            goto fail;
        }

        if (packet[0] == C_END)
            break;

        if (packet[0] != C_DATA)
        {
            printf("Unexpected packet type %d\n", packet[0]);
            goto fail;
        }

        if (n < DATA_HEADER_SIZE)
        {
            printf("Data packet too short\n");
            goto fail;
        }

        int dataSize = 256 * packet[1] + packet[2];
        if (dataSize != n - DATA_HEADER_SIZE)
        {
            printf("Data packet size mismatch (header says %d, got %d)\n",
                   dataSize, n - DATA_HEADER_SIZE);
            goto fail;
        }
        if (bytesReceived + dataSize > fileSize)
        {
            printf("Received more data than announced in START packet\n");
            goto fail;
        }

        if (fwrite(&packet[DATA_HEADER_SIZE], 1, dataSize, file) != (size_t)dataSize)
        {
            perror("fwrite");
            goto fail;
        }

        bytesReceived += dataSize;
        printf("Received %ld / %ld bytes\n", bytesReceived, fileSize);
    }

    //END packet//

    if (parseControlPacket(packet, n, C_END, &endSize, endName) < 0)
    {
        printf("Invalid END packet\n");
        goto fail;
    }
    if (endSize != fileSize || strcmp(endName, sentName) != 0)
    {
        printf("END packet does not match START packet\n");
        goto fail;
    }
    if (bytesReceived != fileSize)
    {
        printf("Incomplete file (received %ld of %ld bytes)\n", bytesReceived, fileSize);
        goto fail;
    }

    if (fclose(file) != 0)
    {
        perror("fclose");
        remove(filename);
        return -1;
    }

    return 0;

fail:
    fclose(file);
    remove(filename); // don't leave a partial file that looks valid
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
