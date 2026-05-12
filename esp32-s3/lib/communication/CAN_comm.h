#ifndef _CAN_COMM_H_
#define _CAN_COMM_H_

#include "driver/twai.h"
#include "config.h"

// #include <ESP32CAN.h>
// #include <CAN_config.h>

#define DEVICEID_BITS 5
#define COMMANDID_BITS 6

#define HEARTBEAT_ID 0X00 // 电机➔端 以MIT的格式发送心跳信号



#define MITCOMMAND_ID 0X01 // 端➔电机 以MIT的格式发送控制指令

extern uint32_t sendNum; // for test send speed
extern uint32_t recNum;

extern MIT devicesState[8];

// Function ID
#define HEARTBEAT_FUNC_ID 0X700 // 电机➔端 以MIT的格式发送心跳信号
#define FUNC_ID_NMT 0x000 
#define FUNC_ID_RPDO1 0x200
#define FUNC_ID_RPDO2 0x300
#define FUNC_ID_RPDO3 0x400
#define FUNC_ID_RPDO4 0x500
#define FUNC_ID_TPDO1 0x180
#define FUNC_ID_TPDO2 0x280
#define FUNC_ID_TPDO3 0x380
#define FUNC_ID_TPDO4 0x480
#define FUNC_ID_SDO_REQUEST 0x600
#define FUNC_ID_SDO_RESPONSE 0x580


#define LIMIT_MIN_MAX(x,min,max) (x) = (((x)<=(min))?(min):(((x)>=(max))?(max):(x)))
const int rx_queue_size = 10;       // Receive Queue size



void CANInit();
// void doCANCommand();

void MITState_callback(uint8_t nodeID, uint8_t *data);

void sendCANCommand(uint32_t nodeID, uint32_t msgID, uint8_t *data);
void recCANMessage();

void sendMITCommand(uint8_t nodeID, MIT command);
void disable(uint8_t nodeID);
void enable(uint8_t nodeID);
void zeroPos(uint8_t nodeID);
float uint_to_float(int x_int, float x_min, float x_max, int bits);
uint16_t float_to_uint(float x, float x_min, float x_max, uint8_t bits);





#endif

