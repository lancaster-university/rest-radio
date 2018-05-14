#include "Hub.h"
#include "EventModel.h"

#define SLIP_END                0xC0
#define SLIP_ESC                0xDB
#define SLIP_ESC_END            0xDC
#define SLIP_ESC_ESC            0xDD

#define HISTORY_COUNT           20
#define APP_ID_MSK              0xFFFF0000
#define PACKET_ID_MSK           0x0000FFFF

extern void log_string_priv(const char *);

static uint32_t id_history[HISTORY_COUNT] = { 0 };
static uint16_t historyIndexHead = 0;

bool Bridge::searchHistory(uint16_t app_id, uint16_t id)
{
    for (int idx = 0; idx < HISTORY_COUNT; idx++)
    {
        if (((id_history[idx] & APP_ID_MSK) >> 16) == app_id && (id_history[idx] & PACKET_ID_MSK) == id)
            return true;
    }
    
    return false;
}

void Bridge::addToHistory(uint16_t app_id, uint16_t id)
{
    id_history[historyIndexHead] = ((app_id << 16) | id);   
    historyIndexHead = (historyIndexHead + 1) % HISTORY_COUNT;
}

void Bridge::onRadioPacket(MicroBitEvent e)
{
    DataPacket* r = radio.rest.recvRaw(e.value);

    if (r == NULL)
        return;

    uint8_t* packetPtr = (uint8_t *)r;
    uint16_t len = r->len;

    bool seen = searchHistory(r->app_id, r->id);

    if (!seen)
    {
        addToHistory(r->app_id, r->id);

        for (uint16_t i = 0; i < len; i++)
        {
            if (packetPtr[i] == SLIP_ESC)
            {
                serial.putc(SLIP_ESC);
                serial.putc(SLIP_ESC_ESC);
                continue;
            }

            if(packetPtr[i] == SLIP_END)
            {
                serial.putc(SLIP_ESC);
                serial.putc(SLIP_ESC_END);
                continue;
            }

            serial.putc(packetPtr[i]);
        }

        serial.putc(SLIP_END);
    }

    delete r;
}

void Bridge::onSerialPacket(MicroBitEvent)
{
    DataPacket* packet = (DataPacket*) malloc(sizeof(DataPacket));
    uint8_t* packetPtr = (uint8_t*)packet;

    char c = 0;

    while ((c = serial.read()) != SLIP_END)
    {
        if (packet->len >= RADIO_MAX_PACKET_SIZE)
            continue;

        if (c == SLIP_ESC)
        {
            char next = serial.read();

            if (next == SLIP_ESC_END)
                c = SLIP_END;
            else if (next == SLIP_ESC_ESC)
                c = SLIP_ESC;
            else
            {
                *packetPtr++ = c;
                *packetPtr++ = next;
                packet->len += 2;
            }

            continue;
        }

        packet->len++;

        *packetPtr++ = c;
    }

    radio.rest.send(packet);

    serial.eventOn((char)SLIP_END);
}

Bridge::Bridge(Radio& r, MicroBitSerial& s, MicroBitMessageBus& b) : radio(r), serial(s)
{
    memset(id_history, 0, sizeof(uint32_t) * HISTORY_COUNT);

    b.listen(RADIO_REST_ID, MICROBIT_EVT_ANY, this, &Hub::onRadioPacket);
    b.listen(MICROBIT_ID_SERIAL, MICROBIT_SERIAL_EVT_DELIM_MATCH, this, &Hub::onSerialPacket);

    s.setRxBufferSize(255);

    s.eventOn((char)SLIP_END);
}
