#include "esphome.h"

class MiPurifier : public Component, public UARTDevice, public CustomAPIDevice {
public:
  static const int max_line_length = 80;
  char recv_buffer[max_line_length];
  char send_buffer[max_line_length];
  bool is_preset;
  int last_heartbeat, last_query;
  
  Sensor *airquality_sensor = new Sensor();
  Sensor *filterlife_sensor = new Sensor();

  MiPurifier(UARTComponent *uart) : UARTDevice(uart) {}

  int readline(int readch, char *buffer, int len) {
    static int pos = 0;
    int rpos;
    
    if (readch > 0) {
      switch (readch) {
        case '\r': // Return on CR
          rpos = pos;
          pos = 0;  // Reset position index ready for next time
          return rpos;
        default:
          if (pos < len-1) {
            buffer[pos++] = readch;
            buffer[pos] = 0;
          }
      }
    }
    // No end of line has been found, so return -1.
    return -1;
  }

  // only run setup() after a Wi-Fi connection has been established successfully
  float get_setup_priority() const override { return esphome::setup_priority::AFTER_WIFI; } 

  void turn_on() {
    strcpy(send_buffer, "down set_properties 2 1 true");
  }

  void turn_off() {
    strcpy(send_buffer, "down set_properties 2 1 false");
  }

  void enable_beeper() {
    strcpy(send_buffer, "down set_properties 6 1 true");
  }

  void disable_beeper() {
    strcpy(send_buffer, "down set_properties 6 1 false");
  }

  void lock() {
    strcpy(send_buffer, "down set_properties 8 1 true");
  }

  void unlock() {
    strcpy(send_buffer, "down set_properties 8 1 false");
  }

  void set_mode(std::string mode) {
    // 0: auto, 1: sleep, 2: manual, 3: low, 4: med, 5: high
    if (mode == "auto") {
      strcpy(send_buffer, "down set_properties 2 4 0");
    } else if (mode == "night") {
      strcpy(send_buffer, "down set_properties 2 4 1");
    } else if (mode == "manual") {
      strcpy(send_buffer, "down set_properties 2 4 2");
    }
  }

  void set_brightness(std::string brightness) {
    if (brightness == "off") {
      strcpy(send_buffer, "down set_properties 7 2 2");
    } else if (brightness == "low") {
      strcpy(send_buffer, "down set_properties 7 2 1");
    } else if (brightness == "high") {
      strcpy(send_buffer, "down set_properties 7 2 0");
    }
  }

  void set_manualspeed(int speed) {
    snprintf(send_buffer, max_line_length, "down set_properties 9 1 %i", speed);
  }

  void send_command(std::string s) {
    strcpy(send_buffer, s.c_str());
  }

  void update_property(char* id, char* val) {
    if (strcmp(id, "34") == 0) {
      airquality_sensor->publish_state(atof(val));
    } else if (strcmp(id, "41") == 0) {
      filterlife_sensor->publish_state(atof(val));
    } else if (strcmp(id, "21") == 0) {
      // power (on, off)
      power_switch->publish_state(strcmp(val, "true") == 0);
    } else if (strcmp(id, "24") == 0) {
      // mode (auto, night, manual, preset)
      is_preset = false;
      switch (atoi(val)) {
        case 0:
          mode_select->publish_state("auto");
          break;
        case 1:
          mode_select->publish_state("night");
          break;
        case 2:
          mode_select->publish_state("manual");
          break;
        case 3:
          is_preset = true;
          break;
      }
    } else if (strcmp(id, "61") == 0) {
      // beeper (on, off)
      beeper_switch->publish_state(strcmp(val, "true") == 0);
    } else if (strcmp(id, "81") == 0) {
      // lock (on, off)
      lock_switch->publish_state(strcmp(val, "true") == 0);
    } else if (strcmp(id, "72") == 0) {
      // display brightness (off, low, high)
      switch (atoi(val)) {
        case 0:
          brightness_select->publish_state("high");
          break;
        case 1:
          brightness_select->publish_state("low");
          break;
        case 2:
          brightness_select->publish_state("off");
          break;
      }
    } else if (strcmp(id, "91") == 0) {
      // manual speed
      manualspeed->publish_state(atof(val)+1);
    }
  }

  void setup() override {
    register_service(&MiPurifier::send_command, "send_command", {"command"});
    // get initial state & settings
    strcpy(send_buffer, "down get_properties 2 2 2 4 6 1 7 2 8 1 9 1");
  }
  
  void loop() override {
    while (available()) {
      if(readline(read(), recv_buffer, max_line_length) > 0) {
        char *cmd = strtok(recv_buffer, " ");
        if (strcmp(cmd, "net") == 0) {
            write_str("local");
        } else if (strcmp(cmd, "time") == 0) {
          write_str("0");
        } else if (strcmp(cmd, "get_down") == 0) {
          // send command from send_buffer
          if (strlen(send_buffer) > 0) {
            write_str(send_buffer);
            send_buffer[0] = '\0';
            ESP_LOGD("mipurifier", "sent send_buffer");
          } else if (millis() - last_heartbeat > 60000) {
            // send mysterious heartbeat message
            write_str("down set_properties 13 9 60");
            last_heartbeat = millis();
            ESP_LOGD("purifier", "sent heartbeat");
          } else if (millis() - last_query > 60000) {
            // force sensor update
            write_str("down get_properties 3 4 4 1");
            last_query = millis();
            ESP_LOGD("purifier", "sent query string");
          } else {
            write_str("down none");
          }
        } else if (strcmp(cmd, "properties_changed") == 0) {
          ESP_LOGD("mipurifier", "parsing properties_changed message");
          char *id1 = strtok(NULL, " ");
          char *id2 = strtok(NULL, " ");
          char *id = strcat(id1, id2);
          char *val = strtok(NULL, " ");
          update_property(id, val);
          write_str("ok");
        } else if (strcmp(cmd, "result") == 0) {         
          // loop over all properties and update
          ESP_LOGD("mipurifier", "parsing result message");
          char *id1, *id2, *id, *val;
          while (true) {
            if (!(id1 = strtok(NULL, " "))) break;
            if (!(id2 = strtok(NULL, " "))) break;
            id = strcat(id1, id2);
            strtok(NULL, " "); // skip 0
            if (!(val = strtok(NULL, " "))) break;
            update_property(id, val);
          }
          write_str("ok");
        } else {
          // just acknowledge any other message
          write_str("ok");
        }
      }
    }
  }
};