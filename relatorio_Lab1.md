<!--
Submission limits (from "Lab 1 - Efficiency Report Guidelines"):
- 3 pages maximum, including figures and tables; 11 pt font
- PDF named "Authors_Lab1_Report.pdf"
- Author names and class on the first page
- Every figure and table numbered and captioned
-->

# Lab 1 – Characterisation of the Data-Link Protocol Efficiency

**Authors:** `<Luís Kong> (up202409115)`, `<André Pereira> (up202405143)` — Class `<3LEIC15>`
**Course:** Redes de Computadores (RC), L.EIC – FEUP, 2026/2027

## Summary

We implemented a Stop-and-Wait ARQ data-link protocol and used it to transfer files over an RS-232 serial link. We measured its efficiency S while varying four parameters one at a time: the frame error rate (FER), the propagation delay (T<sub>prop</sub>), the baud rate (C) and the frame size (L). With the default parameters (C = 9600 bit/s, no errors, no delay), the measured efficiency is `<S₀>`. The measurements `<agree / disagree>` with the theoretical model S = (1 − FER)/(1 + 2a), with the largest deviation at `<condition>`.

## Introduction

The goal of the first lab was to build reliable file transfer between two computers connected by an RS-232 serial link. The data-link layer provides framing, error detection, acknowledgements and retransmission using Stop-and-Wait ARQ. The application layer splits the file into data packets and sends them through the data-link interface (`llOpen`, `llSend`, `llReceive`, `llClose`), so each layer stays independent of the other.

This report evaluates how efficiently the protocol uses the link. Section 2 describes the protocol, the test setup and the theoretical model. Section 3 presents the measured efficiency for each parameter and compares it with the model. Section 4 gives the conclusions.

## Methodology

### Protocol

Every frame is delimited by FLAG (0x7E) and carries an address (A), a control field (C) and a header check BCC1 = A ⊕ C. Information frames (I0/I1) also carry the payload and a data check BCC2, the XOR of all payload bytes. Byte stuffing replaces 0x7E and 0x7D inside the frame with ESC (0x7D) followed by the byte XOR 0x20. The receiver answers RR(N) for a correct frame and REJ(N) when BCC2 fails. The transmitter retransmits on REJ, or after a 4 s timeout, and gives up after 3 retransmissions without a reply. Each I-frame carries at most 1000 payload bytes: a 3-byte data-packet header plus 997 bytes of the file.

### Experimental setup

The tests ran over the `cable` program, which emulates the serial link between transmitter and receiver. Its `baud <C>`, `prop <µs>` and `ber <p>` commands set the baud rate, the one-way propagation delay and the bit error rate. Bit errors affect both directions, so supervision frames can also be corrupted. Every test transferred `penguin.gif` (10 968 bytes). At `llClose`, the transmitter prints the transfer time, the number of retransmissions, timeouts and REJs, and the throughput.

Starting from a baseline of C = 9600 bit/s, T<sub>prop</sub> = 0, BER = 0 and L = 1000 bytes, we varied one parameter at a time:

- **C:** 1200, 2400, 4800, 9600, 19 200, 38 400, 57 600, 115 200 bit/s
- **T<sub>prop</sub>:** 0, 10, 50, 100, 200, 500 ms
- **FER:** BER values chosen to give a FER of about 0, 0.1, 0.2, 0.3, 0.4 and 0.5
- **L:** 125, 250, 500, 1000 bytes (changing only the data chunk size, for these tests)

Each point is the mean of 3 runs.

### Efficiency model

The received bit rate is R = (file size × 8) / transfer time, and the measured efficiency is S = R / C. For Stop-and-Wait, the frame transmission time is T<sub>f</sub> = L·8 / C and a = T<sub>prop</sub> / T<sub>f</sub>. The theoretical efficiency is

  S = (1 − FER) / (1 + 2a),  with FER ≈ 1 − (1 − BER)<sup>8L</sup>.

`cable` sends each byte as 10 bit times (start bit, 8 data bits, stop bit), so the measured S cannot exceed 0.8 even on a perfect link. Frame headers and byte stuffing reduce it further.

## Results

### Baud rate (C)

*Table 1 – Efficiency as a function of the baud rate (T<sub>prop</sub> = 0, BER = 0, L = 1000 bytes).*

| C (bit/s) | Time (s) | R (bit/s) | S measured | S theoretical |
|---|---|---|---|---|
| 1200 | | | | |
| 9600 | | | | |
| 115 200 | | | | |

*Figure 1 – S as a function of C.* `<figure>`

`<Discussion: S should stay roughly constant with C (a ≈ 0); explain any difference from 0.8 (header and stuffing overhead, processing time).>`

### Propagation delay (T<sub>prop</sub>)

*Table 2 – Efficiency as a function of T<sub>prop</sub> (C = 9600 bit/s, BER = 0, L = 1000 bytes).*

| T<sub>prop</sub> (ms) | a | Time (s) | R (bit/s) | S measured | S theoretical |
|---|---|---|---|---|---|
| 0 | | | | | |
| 100 | | | | | |
| 500 | | | | | |

*Figure 2 – S as a function of a, measured and theoretical.* `<figure>`

`<Discussion: S falls as 1/(1 + 2a); comment on how well the measurements fit.>`

### Frame error rate (FER)

*Table 3 – Efficiency as a function of FER (C = 9600 bit/s, T<sub>prop</sub> = 0, L = 1000 bytes).*

| BER | FER | Retransmissions | Timeouts | Time (s) | S measured | S theoretical |
|---|---|---|---|---|---|---|
| 0 | | | | | | |
| | | | | | | |

*Figure 3 – S as a function of FER, measured and theoretical.* `<figure>`

`<Discussion: a REJ triggers an immediate retransmission, while a lost frame, header error or corrupted RR costs a full 4 s timeout. Timeouts therefore push S below (1 − FER).>`

### Frame size (L)

*Table 4 – Efficiency as a function of L (C = 9600 bit/s, T<sub>prop</sub> = `<value>`, BER = `<value>`).*

| L (bytes) | Time (s) | R (bit/s) | S measured | S theoretical |
|---|---|---|---|---|
| 125 | | | | |
| 1000 | | | | |

*Figure 4 – S as a function of L.* `<figure>`

`<Discussion: larger frames reduce a and the share of header overhead, but they raise the FER when BER > 0; identify the best L.>`

## Conclusions

`<Two or three sentences: does S = (1 − FER)/(1 + 2a) predict the measurements? Name the main sources of deviation (10-bit byte framing, header and stuffing overhead, timeout cost of lost frames) and say which parameter limits efficiency the most.>`
