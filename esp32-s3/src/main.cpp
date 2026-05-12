#include <Arduino.h>
#include <HardwareSerial.h>
#include "sbus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <CAN_comm.h>


// 模拟模式开关
#define SIMULATION_MODE false  // true=模拟模式, false=真实SBUS模式

// SBUS接收机配置 - 使用UART2，自定义引脚GPIO16和GPIO17
HardwareSerial sbusSerial(2);  // 使用UART2
bfs::SbusRx sbus_rx(&sbusSerial, 16, 17, true);  // RX=GPIO16, TX=GPIO17, 信号反相

// FreeRTOS任务句柄和队列
TaskHandle_t sbusTaskHandle = NULL;
TaskHandle_t ledTaskHandle = NULL;
TaskHandle_t statusTaskHandle = NULL;
TaskHandle_t MonitorTaskHandle = NULL;
QueueHandle_t sbusDataQueue = NULL;


// SBUS数据结构
struct SbusQueueData {
    uint16_t channels[16];  // 16个通道数据
    bool failsafe;          // 故障保护状态
    bool lost_frame;        // 丢帧状态
};

// 系统状态变量
bool ledState = false;      // LED当前状态
bool sbusMotorState_U = false;   // 电机当前状态上
bool sbusMotorState_D = false;   // 电机当前状态下
bool sbusMotorState_L = false;   // 电机当前状态左
bool sbusMotorState_R = false;   // 电机当前状态右
uint32_t lastSbusTime = 0;  // 最后收到SBUS数据的时间
bool sbusConnected = false;  // SBUS连接状态
uint32_t sbusFrameCount = 0; // SBUS帧计数器
bool simulationMode = SIMULATION_MODE; // 模拟模式标志

//电机控制参数结构体定义
MIT MITCtrlParam; // 初始化 MIT发送数据结构体
MIT devicesState[8];// 初始化 MIT接收数据结构体数组

void CANControl_P(float v,float k,float t,int id);//速度为正
void CANControl_N(float v,float k,float t,int id);//速度为负
void stopMotor();
float Speed_Motor(int channels);
float Motor_Speed=0,Motor_k=0,Motor_T=0;


// SBUS数据处理任务
void sbusTask(void *parameter) {
    SbusQueueData sbusData;
    while (1) {
            // 真实模式：读取实际SBUS数据
            if (sbus_rx.Read()) {
                bfs::SbusData data = sbus_rx.data();
                sbusFrameCount++;
                
                // 填充队列数据
                for (int i = 0; i < 16; i++) {
                    sbusData.channels[i] = data.ch[i];
                }
                sbusData.failsafe = data.failsafe;
                sbusData.lost_frame = data.lost_frame;
                
                // 发送数据到队列
                if (xQueueSend(sbusDataQueue, &sbusData, 0) == pdTRUE) {
                    lastSbusTime = millis();
                    sbusConnected = true;
                }
            }
            vTaskDelay(2 / portTICK_PERIOD_MS);
    }
}

float Speed_Motor(int channels)
{
    float Speed = 0;
    if(channels>1200)
        channels=1200;
    else if (channels<100)
        channels=0;
    Speed = 30.0 - channels / 60.0;
    return Speed;
}

void  MonitorTask(void *parameter){
    SbusQueueData sbusData;
    uint32_t lastBlinkTime = 0;
    sbusData.channels[0]=1300;
    while(1){
        bool currentConnected = (millis() - lastSbusTime < 1000);
        if (currentConnected && sbusConnected) {
            if (xQueueReceive(sbusDataQueue, &sbusData, 50 / portTICK_PERIOD_MS) == pdTRUE) {
                if (sbusData.channels[0]>1700){
                    sbusMotorState_R = true;
                }
                else if(sbusData.channels[0]<450){
                    sbusMotorState_L = true;
                }
                else if(sbusData.channels[1]<100){
                    sbusMotorState_U = true;
                }
                else if (sbusData.channels[1]>1400){
                    sbusMotorState_D = true;
                }
                else{
                    sbusMotorState_R = false;
                    sbusMotorState_L = false;
                    sbusMotorState_U = false;
                    sbusMotorState_D = false;
                }
                Motor_Speed=Speed_Motor(sbusData.channels[2]);//通道3控制速度
                Motor_k=0.06*Motor_Speed+0.6;//通道3控制k
                Motor_T=0.04*Motor_Speed+0.8;//通道3控制t
            
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
}

void CANControl_P(float v,float k,float t,int id)//逆时针
{
  MITCtrlParam.pos = 0;//p=10,v=0,kp=32,kd=0.003,tor=0
  MITCtrlParam.vel = v;//v=10,k=0.59,tor=1.22
  MITCtrlParam.kp =0;
  MITCtrlParam.kd = k;
  MITCtrlParam.tor = t;
  recCANMessage(); // CAN接收函数
  //Serial.printf("%.2f,%.2f,%.2f,%.2f\n", MITCtrlParam.pos,MITCtrlParam.vel,devicesState[0].pos, devicesState[0].vel); //打印对应关节电机接收信息  devicesState[0]中的0代表ID为1的电机获取数据，如果获取ID为2的关节电机数据将devicesState[0]修改为devicesState[1]即可
  //发送对应控制命令指令
  sendMITCommand(id, MITCtrlParam);
  
}

void CANControl_N(float v,float k,float t,int id)//顺时针
{
  MITCtrlParam.pos = 0;//p=10,v=0,kp=32,kd=0.003,tor=0
  MITCtrlParam.vel = -v;//v=10,k=0.59,tor=1.22
  MITCtrlParam.kp =0;
  MITCtrlParam.kd = k;
  MITCtrlParam.tor = -t;
  recCANMessage(); // CAN接收函数
  //Serial.printf("%.2f,%.2f,%.2f,%.2f\n", MITCtrlParam.pos,MITCtrlParam.vel,devicesState[0].pos, devicesState[0].vel); //打印对应关节电机接收信息  devicesState[0]中的0代表ID为1的电机获取数据，如果获取ID为2的关节电机数据将devicesState[0]修改为devicesState[1]即可
  //发送对应控制命令指令
  sendMITCommand(id, MITCtrlParam);
}


void stopMotor()
{
  MITCtrlParam.pos = 0;
  MITCtrlParam.vel = 0;
  MITCtrlParam.kp = 0;
  MITCtrlParam.kd = 0;
  MITCtrlParam.tor = 0;
  recCANMessage(); // CAN接收函数
  sendMITCommand(0x05, MITCtrlParam);
  sendMITCommand(0x02, MITCtrlParam);
  sendMITCommand(0x03, MITCtrlParam);
  sendMITCommand(0x06, MITCtrlParam);
}


void setup() {
    Serial.begin(115200);
    delay(1000);
    CANInit();    // 初始化CAN总线
    enable(0x05); // 使能关节电机，0x01为对应关节电机ID号
    enable(0x02); // 使能关节电机，0x02为对应关节电机ID号
    enable(0x03); // 使能关节电机，0x03为对应关节电机ID号
    enable(0x06); // 使能关节电机，0x06为对应关节电机ID号
    Serial.println("\n ESP32-S3 SBUS Receiver (UART2 Version - GPIO16/17)");
    Serial.println("========================================");
    
    
    // 初始化SBUS（仅在真实模式下需要）
    if (!simulationMode) {
        sbusSerial.begin(100000, SERIAL_8E2, 16, 17);
        sbus_rx.Begin();
        Serial.println(" REAL SBUS Mode Initialized");
    } else {
        Serial.println(" SIMULATION Mode Activated");
        Serial.println("  Generating simulated SBUS data");
        Serial.println("  Connect remote to switch to REAL mode");
    }
    
    Serial.println("  Using: UART2 (RX16/TX17)");
    
    // 创建数据队列
    sbusDataQueue = xQueueCreate(10, sizeof(SbusQueueData));
    if (sbusDataQueue == NULL) {
        Serial.println(" Failed to create data queue!");
        return;
    }
    
    // 创建FreeRTOS任务
    Serial.println(" Creating FreeRTOS Tasks...");
    
    xTaskCreatePinnedToCore(sbusTask, "SBUS_Task", 4096, NULL, 8, &sbusTaskHandle, 1);//0的时候可以
    xTaskCreatePinnedToCore(MonitorTask, "Monitor_Task", 4096, NULL, 5, &MonitorTaskHandle, 1);
    Serial.println("========================================\n");
}

void loop() {
    static uint32_t lastLoopTime = 0;
    if (millis() - lastLoopTime > 5000) {
    
        lastLoopTime = millis();
    }  
    if(sbusMotorState_R){
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x05);
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x02);
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x03);
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x06);
  //    Serial.printf("%.2f,%.2f,%.2f,%.2f\n",devicesState[0].vel, devicesState[1].vel,devicesState[2].vel,devicesState[5].vel);
    }
    else if(sbusMotorState_L){
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x05);
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x02);
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x03);
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x06);
      //Serial.printf("%.2f,%.2f,%.2f,%.2f,%.2f\n",MITCtrlParam.vel,devicesState[0].vel, devicesState[1].vel,devicesState[2].vel,devicesState[3].vel);
    }PACK_STRUCT_END
    else if(sbusMotorState_U){
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x05);
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x02);
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x03);
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x06);
      Serial.printf("%.2f,%.2f,%.2f,%.2f\n",devicesState[0].vel, devicesState[1].vel,devicesState[2].vel,devicesState[5].vel);
      //CANControl(10,0.595,1.2,0x03);
      //Serial.printf("%.2f,%.2f,%.2f,%.2f,%.2f\n",MITCtrlParam.vel,devicesState[0].vel, devicesState[1].vel,devicesState[2].vel,devicesState[3].vel);
    }
    else if(sbusMotorState_D){
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x05);
      CANControl_N(Motor_Speed,Motor_k,Motor_T,0x02);
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x03);
      CANControl_P(Motor_Speed,Motor_k,Motor_T,0x06);
      //CANControl(-10,0.59,1.22,0x04);
      //Serial.printf("%.2f,%.2f,%.2f,%.2f,%.2f\n",MITCtrlParam.vel,devicesState[0].vel, devicesState[1].vel,devicesState[2].vel,devicesState[3].vel);
    }
    else{
      stopMotor();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}