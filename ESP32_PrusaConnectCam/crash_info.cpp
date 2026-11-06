/**
   @file crash_info.cpp

   @brief /crash_info: why the camera last reset, memory and task stack headroom, and the summary
          of the core dump the panic handler saved to flash, so a camera without a serial console
          can report a crash. Decode coredump_bt with addr2line and the ELF of the build whose
          SHA-256 starts with coredump_app_elf_sha256.
*/

#include "WebServer.h"
#include "esp_core_dump.h"
#include "esp_app_desc.h"

/**
   @brief Append the lowest free stack a task has had since boot
   @param String& out
   @param const char* name
   @param TaskHandle_t task - NULL when the task does not exist on this board
   @return void
*/
static void CrashInfo_AddStack(String &out, const char *name, TaskHandle_t task) {
  if (NULL != task) {
    out += "stack_free_" + String(name) + ": " + String(uxTaskGetStackHighWaterMark(task)) + "\n";
  }
}

/**
   @brief Register /crash_info
   @param none
   @return void
*/
void Server_InitWebServer_CrashInfo() {
  server.on("/crash_info", HTTP_GET, [](AsyncWebServerRequest *request) {
    SystemLog.AddEvent(LogLevel_Verbose, F("WEB server: get crash_info"));
    if (Server_CheckBasicAuth(request) == false)
      return;

    char sha[17] = { 0 };
    esp_app_get_elf_sha256(sha, sizeof(sha));
    String out = "sw_version: " + String(SW_VERSION) + "\n";
    out += "sw_build: " + String(SW_BUILD) + "\n";
    out += "app_elf_sha256: " + String(sha) + "\n";
    out += "reset_reason: " + System_printMcuResetReasonSimple() + "\n";
    out += "uptime_ms: " + String(millis()) + "\n";
    out += "heap_free: " + String(ESP.getFreeHeap()) + "\n";
    out += "heap_min_free: " + String(ESP.getMinFreeHeap()) + "\n";
    out += "heap_largest: " + String(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)) + "\n";
    out += "psram_free: " + String(ESP.getFreePsram()) + "\n";
    out += "psram_min_free: " + String(ESP.getMinFreePsram()) + "\n";
    out += "psram_largest: " + String(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)) + "\n";
    CrashInfo_AddStack(out, "SystemMain", Task_SystemMain);
    CrashInfo_AddStack(out, "CapturePhotoAndSend", Task_CapturePhotoAndSend);
    CrashInfo_AddStack(out, "WiFiManagement", Task_WiFiManagement);
    CrashInfo_AddStack(out, "SdCardCheck", Task_SdCardCheck);
    CrashInfo_AddStack(out, "SerialCfg", Task_SerialCfg);
    CrashInfo_AddStack(out, "SystemTelemetry", Task_SystemTelemetry);
    CrashInfo_AddStack(out, "SysLed", Task_SysLed);
    CrashInfo_AddStack(out, "WiFiWatchdog", Task_WiFiWatchdog);

    esp_core_dump_summary_t *dump = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
    if ((NULL != dump) && (ESP_OK == esp_core_dump_get_summary(dump))) {
      out += "coredump_task: " + String(dump->exc_task) + "\n";
      out += "coredump_pc: 0x" + String(dump->exc_pc, HEX) + "\n";
      out += "coredump_bt_corrupted: " + String(dump->exc_bt_info.corrupted) + "\n";
      out += "coredump_bt:";
      for (uint32_t i = 0; (i < dump->exc_bt_info.depth) && (i < 16); i++) {
        out += " 0x" + String(dump->exc_bt_info.bt[i], HEX);
      }
      out += "\ncoredump_app_elf_sha256: ";
      /* the dump stays in flash until the next crash, so it may be from an earlier build */
      bool same_build = true;
      size_t n = 0;
      for (; (n < sizeof(dump->app_elf_sha256)) && (0 != dump->app_elf_sha256[n]); n++) {
        out += (char)dump->app_elf_sha256[n];
        if ((n >= (sizeof(sha) - 1)) || (sha[n] != (char)dump->app_elf_sha256[n])) {
          same_build = false;
        }
      }
      out += "\n";
      out += "coredump_from_running_build: " + String(((0 < n) && (true == same_build)) ? "yes" : "no") + "\n";
    } else {
      out += "coredump: none\n";
    }
    free(dump);
    request->send(200, "text/plain", out);
  });
}

/* EOF */
