// RCOM 2026/2027
//
// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"

#include <stdio.h>
#include <unistd.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
#define BUF_SIZE 256

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

    printf("Serial port %s opened\n", llParameters.serialPort);

    // Create string to send
    unsigned char setFrame[5];

    setFrame[0] = 0x7E;
    setFrame[1] = 0x03;
    setFrame[2] = 0x03;
    setFrame[3] = setFrame[1] ^ setFrame[2]; //xor?
    setFrame[4] = 0x7E;

    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.

    int bytes = writeBytesSerialPort(setFrame, 5); //mudar para 5
    printf("%d bytes written to serial port\n", bytes);

    for (int i = 0; i < 5; i++){
        printf("byte: %02X\n", setFrame[i]);
    }

    // Wait until all bytes have been written to the serial port
    sleep(1);

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

    // Read the five bytes of the SET supervision frame.
    unsigned char setFrame[5];
    int frameBytes = 0;

    while (frameBytes < 5)
    {
        unsigned char byte;
        int bytes = readByteSerialPort(&byte);
        if (bytes < 0)
        {
            perror("readByteSerialPort");
            closeSerialPort();
            return -1;
        }

        if (bytes == 1)
        {
            printf("Byte received: %02X\n", byte);
            setFrame[frameBytes] = byte;
            frameBytes++;
        }
    }

    // Validate the SET frame before replying with UA.
    if (setFrame[0] != 0x7E ||
        setFrame[1] != 0x03 ||
        setFrame[2] != 0x03 ||
        setFrame[3] != (unsigned char)(setFrame[1] ^ setFrame[2]) ||
        setFrame[4] != 0x7E)
    {
        fprintf(stderr, "Invalid SET frame received\n");
        closeSerialPort();
        return -1;
    }

    // Send UA: FLAG | A | UA | BCC1 | FLAG.
    unsigned char uaFrame[5];

    uaFrame[0] = 0x7E;
    uaFrame[1] = 0x03;
    uaFrame[2] = 0x07;
    uaFrame[3] = uaFrame[1] ^ uaFrame[2]; //xor
    uaFrame[4] = 0x7E;

    int bytesWritten = writeBytesSerialPort(uaFrame, 5);
    if (bytesWritten != 5)
    {
        fprintf(stderr, "Could not write the complete UA frame (%d/5 bytes)\n", bytesWritten);
        closeSerialPort();
        return -1;
    }

    printf("UA frame sent: 7E 03 07 04 7E\n");

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
