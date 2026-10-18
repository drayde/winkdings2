#ifndef PROG_PROTO_H
#define PROG_PROTO_H

#define PROTO_CMD_INFO   0x00u
#define PROTO_CMD_READ   0x01u
#define PROTO_CMD_WRITE  0x02u
#define PROTO_CMD_EXIT   0x03u
#define PROTO_CMD_RESET  0x04u

#define PROTO_EV_EXIT    1
#define PROTO_EV_RESET   2

void proto_init(void);

/* Drain RX fifo through the protocol state machine.
 * Returns 0 normally, PROTO_EV_EXIT or PROTO_EV_RESET once. */
int  proto_poll(void);

#endif
