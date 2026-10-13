#include "device_control.h"
#include "recorder.h"
#include "key_sound.h"
#include "tinyusb.h"
#include "esp_system.h"
#include "esp_private/usb_phy.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static bool requested;
static void restart_task(void *argument) {
    bool boot=(bool)(uintptr_t)argument;
    vTaskDelay(pdMS_TO_TICKS(300)); // Allow the USB RPC acknowledgement to reach the host.
    if(boot) {
        REG_SET_BIT(RTC_CNTL_OPTION1_REG,RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        tud_disconnect();
        tinyusb_driver_uninstall();
        usb_phy_handle_t phy;
        const usb_phy_config_t config={.controller=USB_PHY_CTRL_SERIAL_JTAG,.target=USB_PHY_TARGET_INT};
        usb_new_phy(&config,&phy);
    } else REG_CLR_BIT(RTC_CNTL_OPTION1_REG,RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
esp_err_t device_request_restart(bool bootloader) {
    recorder_status_t r;recorder_snapshot(&r);
    if(key_sound_busy() || r.mounting || r.downloading || (r.state!=REC_IDLE && r.state!=REC_ERROR))
        return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);
    bool busy=requested;if(!busy)requested=true;
    portEXIT_CRITICAL(&lock);
    if(busy)return ESP_ERR_INVALID_STATE;
    if(xTaskCreate(restart_task,"usb-restart",4096,(void*)(uintptr_t)bootloader,2,NULL)!=pdPASS) {
        portENTER_CRITICAL(&lock);requested=false;portEXIT_CRITICAL(&lock);return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
