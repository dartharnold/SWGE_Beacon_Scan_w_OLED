/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include "NimBLEEddystoneTLM.h"
#include "NimBLEBeacon.h"
#include <Wire.h>
#incldue <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define ENDIAN_CHANGE_U16(x) ((((x) & 0xFF00) >> 8) + (((x) & 0xFF) << 8))

// ----------------------- Device Info -----------------------
const char* DEVNAME = "SWGESCAN";

// ----------------------- Timers & Counters -----------------------
#define CHANGEDELY 5*1000     // 5 second Delay (was 15s in original)  

// ----------------------- Location IDs -----------------------
#define NOBEACON     0
#define MARKETPLACE  1
#define DROIDDEPOT   2
#define RESISTANCE   3
#define UNKNOWN      4
#define ALERT        5
#define DOKONDARS    6
#define FIRSTORDER   7

const char* strLocation[8] = {"No Beacon","Marketplace","Droid Depot","Resistance","Unknown","Alert","Dok Ondars","First Order"};

// ----------------------- Filters -----------------------
#define RSSI           -75                // Minimum RSSI to consider   
#define BLE_DISNEY     0x0183             // Manufacturer Company ID    

const String IGNOREHOST = "SITH-TLBX";    // Ignore a specific beacon host  

// ----------------------- State -----------------------
uint32_t last_activity;
int8_t   scan_rssi;
uint16_t area_num = 0, last_area_num = 9;
String   beacon_name = "";

int         scanTime = 5 * 1000; // In milliseconds
NimBLEScan* pBLEScan;

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        int rssi = advertisedDevice->getRSSI();
        if (rssi < RSSI) return;

        // Manufacturer data: first 2 bytes = company ID (little-endian on BLE advertising)
        std::string mfg = advertisedDevice->getManufacturerData();
        if (mfg.size() < 5) return; // we need at least [0]=LSB company, [1]=MSB, [2]=type, [3..]=payload

        const uint8_t* md = reinterpret_cast<const uint8_t*>(mfg.data());
        uint16_t company = (uint16_t)md[1] << 8 | md[0];

        // Is this a Disney beacon?
        if (company != BLE_DISNEY) return;

        // Your original beacon type check: md[2] == 0x0A for "Location"
        if (md[2] != 0x0A) return;

        if (advertisedDevice->haveName()) {
            // Serial.print("Device name: ");
            // Serial.println(advertisedDevice->getName().c_str());
            beacon_name = advertisedDevice->getName().c_str();
            // Serial.println("");
        }

        if (beacon_name == IGNOREHOST) return;
        if ((millis() - last_activity) < CHANGEDELY) return;

        last_activity = millis();

        // Area/location ID is at md[4] in your original payload layout
        area_num = md[4];
        scan_rssi = rssi;

        // Serial.print("RSSI: ");
        // Serial.println(rssi);
        // Serial.print("Company ID: ");
        // Serial.println(company, HEX);
        Serial.print("Location: ");
        Serial.println(strLocation[area_num]);        

        if (advertisedDevice->haveServiceUUID()) {
            NimBLEUUID devUUID = advertisedDevice->getServiceUUID();
            // Serial.print("Found ServiceUUID: ");
            // Serial.println(devUUID.toString().c_str());
            // Serial.println("");
        } else if (advertisedDevice->haveManufacturerData() == true) {
            std::string strManufacturerData = advertisedDevice->getManufacturerData();
            if (strManufacturerData.length() == 25 && strManufacturerData[0] == 0x4C && strManufacturerData[1] == 0x00) {
                // Serial.println("Found an iBeacon!");
                NimBLEBeacon oBeacon = NimBLEBeacon();
                oBeacon.setData(reinterpret_cast<const uint8_t*>(strManufacturerData.data()), strManufacturerData.length());
                // Serial.printf("iBeacon Frame\n");
                // Serial.printf("ID: %04X Major: %d Minor: %d UUID: %s Power: %d\n",
                //               oBeacon.getManufacturerId(),
                //               ENDIAN_CHANGE_U16(oBeacon.getMajor()),
                //               ENDIAN_CHANGE_U16(oBeacon.getMinor()),
                //               oBeacon.getProximityUUID().toString().c_str(),
                //               oBeacon.getSignalPower());
            } else {
                // Serial.println("Found another manufacturers beacon!");
                // Serial.printf("strManufacturerData: %d ", strManufacturerData.length());
                // for (int i = 0; i < strManufacturerData.length(); i++) {
                //     Serial.printf("[%X]", strManufacturerData[i]);
                // }
                // Serial.printf("\n");
            }
            return;
        }

        NimBLEUUID eddyUUID = (uint16_t)0xfeaa;

        if (advertisedDevice->getServiceUUID().equals(eddyUUID)) {
            std::string serviceData = advertisedDevice->getServiceData(eddyUUID);
            if (serviceData[0] == 0x20) {
                // Serial.println("Found an EddystoneTLM beacon!");
                NimBLEEddystoneTLM foundEddyTLM = NimBLEEddystoneTLM();
                foundEddyTLM.setData(reinterpret_cast<const uint8_t*>(serviceData.data()), serviceData.length());

                // Serial.printf("Reported battery voltage: %dmV\n", foundEddyTLM.getVolt());
                // Serial.printf("Reported temperature from TLM class: %.2fC\n", (double)foundEddyTLM.getTemp());
                int   temp     = (int)serviceData[5] + (int)(serviceData[4] << 8);
                float calcTemp = temp / 256.0f;
                //Serial.printf("Reported temperature from data: %.2fC\n", calcTemp);
                // Serial.printf("Reported advertise count: %d\n", foundEddyTLM.getCount());
                // Serial.printf("Reported time since last reboot: %ds\n", foundEddyTLM.getTime());
                // Serial.println("\n");
                // Serial.print(foundEddyTLM.toString().c_str());
                // Serial.println("\n");
            }
        }
    }
} scanCallbacks;

void setup() {
    Serial.begin(115200);
    Serial.println("Scanning...");

    NimBLEDevice::init("Beacon-scanner");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setScanCallbacks(&scanCallbacks);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(100);
}

void loop() {
    NimBLEScanResults foundDevices = pBLEScan->getResults(scanTime, false);
    // Serial.print("Devices found: ");
    // Serial.println(foundDevices.getCount());
    // Serial.println("Scan done!");
    pBLEScan->clearResults(); // delete results scan buffer to release memory
    delay(2000);
}