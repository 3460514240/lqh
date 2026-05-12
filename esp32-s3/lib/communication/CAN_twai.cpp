/*************备注*****************/
/****简单封装了初始化和发送****/
/****接收部分还未封装****/

#include "CAN_twai.h"
static bool driver_installed = false;
CAN_twai::CAN_twai(int num)
{
    _num = num;
}

void CAN_twai::CAN_Init(uint8_t TX_PIN,uint8_t RX_PIN)
{
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();  //Look in the api-reference for other speed sets.
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("Driver installed");
  } else {
    Serial.println("Failed to install driver");
    return;
  }

  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    Serial.println("Driver started");
  } else {
    Serial.println("Failed to start driver");
    return;
  }

  // Reconfigure alerts to detect TX alerts and Bus-Off errors
  uint32_t alerts_to_enable = TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("CAN Alerts reconfigured");
  } else {
    Serial.println("Failed to reconfigure alerts");
    return;
  }

  // TWAI driver is now successfully installed and started
  driver_installed = true;
}


void CAN_twai::CAN_Send(uint32_t* id_buf,uint8_t* buf) {
  // Send message

  // Configure message to transmit
  twai_message_t message;
  message.flags = 0x01;
  message.identifier = id_buf[0];
  message.data_length_code = 8;
  for (int i = 0; i < 8; i++) {
    message.data[i] = buf[i];
  }
  // Queue message for transmission
  //超时时间为1ms，pdMS_TO_TICKS(1000)
  if (twai_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
    printf("Message queued for transmission\n");
  } else {
    printf("Failed to queue message for transmission\n");
  }
}



