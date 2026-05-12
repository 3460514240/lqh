
#ifndef _CAN_TWAI_H
#define _CAN_TWAI_H
#include "driver/twai.h"
#include "Arduino.h"
class CAN_twai
{
public:
    CAN_twai(int num);
    void CAN_Init(uint8_t TX_PIN,uint8_t RX_PIN);
    void CAN_Send(uint32_t* id_buf,uint8_t* buf);

private:
    int _num;
};


#endif

