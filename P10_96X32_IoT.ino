
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <String.h>
#include <DMD32_B.h>       
#include "fonts/SystemFont5x7_greek.h"
#include "fonts/Verdana_Greek.h"
#include <WiFiClientSecure.h>
#include <Arduino_JSON.h>
#include "time.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DS1302.h>

// DS1302 RTC instance
Ds1302 rtc(4, 32, 25);
// GPIO where the DS18B20 is connected to
const int oneWireBus = 35;
// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);
// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);

#define OPEN_HOT_SPOT 36
#define REG_BRIGHTNESS 20
#define EEP_BRIGHT_ADDRESS 0X100

const char* ssid = "rousis";
const char* passphrase = "rousis074520198";

uint8_t Clock_Updated_once = 0;
uint8_t Connect_status = 0;
uint8_t Show_IP = 0;
uint8_t Last_Update = 0xff;
char mac_address[18];
String st;
String content;
String esid;
String epass = "";
int i = 0;
int statusCode;

const char Company[] = { "Rousis Systems" };
const char Device[] = { "IoT LED display" };
const char Version[] = { "V.2.1" };
static char page_buf[250];
uint32_t page_len;
char timedisplay[8];
char datedisplay[8];

bool testWifi(void);
void launchWeb(void);
void setupAP(void);

//Establishing Local server at port 80
WebServer server(80);

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0; // 7200;
const int   daylightOffset_sec = 0; // 3600;

#define LED_PIN 13

// Code with critica section
//void IRAM_ATTR onTime() {
//    portENTER_CRITICAL_ISR(&timerMux);
//    myLED.scanDisplay();
//    count++;
//    //digitalWrite (LED_PIN, !digitalRead(LED_PIN)) ;
//    portEXIT_CRITICAL_ISR(&timerMux);
//}
//Fire up the DMD library as dmd
//Fire up the DMD library as dmd
#define DISPLAYS_ACROSS 2 //4
#define DISPLAYS_DOWN 1
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN, PROTOCOL_QIANGLI);
#define PIXELS_X (DISPLAYS_ACROSS * 32)
#define PIXELS_Y (DISPLAYS_DOWN * 16)
hw_timer_t* scan_timer = NULL;

/*--------------------------------------------------------------------------------------
  Interrupt handler for Timer1 (TimerOne) driven DMD refresh scanning, this gets
  called at the period set in Timer1.initialize();
--------------------------------------------------------------------------------------*/

portMUX_TYPE Scan_timerMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR triggerScan()
{
    portENTER_CRITICAL_ISR(&Scan_timerMux);
    dmd.scanDisplayBySPI();
    portEXIT_CRITICAL_ISR(&Scan_timerMux);
}

void test_patern_slash() {
    uint8_t n = 0x55;

}

char* remove_quotes(char* s1) {
    size_t len = strlen(s1);
    if (s1[0] == '"' && s1[len - 1] == '"') {
        s1[len - 1] = '\0';
        memmove(s1, s1 + 1, len - 1);
    }
    return s1;
}

const char* Host = "app.smart-q.eu";  // Server URL
String jsonBuffer;

void setTimezone(String timezone) {
    Serial.printf("  Setting Timezone to %s\n", timezone.c_str());
    setenv("TZ", timezone.c_str(), 1);  //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
    tzset();
}

WiFiClientSecure client;
// Code without critical section
/*void IRAM_ATTR onTime() {
   count++;
}*/

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    sensors.begin();
    sensors.setResolution(12);
    rtc.init();
    //Set off the tricle charge of the DS1302

    //-----------------------------------------------------------------------------------------
    uint8_t cpuClock = ESP.getCpuFreqMHz();

    scan_timer = timerBegin(0, cpuClock, true);
    timerAttachInterrupt(scan_timer, &triggerScan, true);
    timerAlarmWrite(scan_timer, 500, true); //timerAlarmWrite(scan_timer, 1000, true);
    //timerAlarmEnable(scan_timer);    
    //------------------------------------------------------------------------------------------

    //clear/init the DMD pixels held in RAM
    dmd.clearScreen(true);   //true is normal (all pixels off), false is negative (all pixels on)
    dmd.selectFont(SystemFont5x7_greek);
    //myLED.displayEnable();     // This command has no effect if you aren't using OE pin
    //myLED.selectFont(SystemFont5x7_greek); //font1
    delay(200);

    dmd.drawString(0, 0, "Initialize WiFi", 15, GRAPHICS_NORMAL, 1);
    delay(3000);
    dmd.clearScreen(true);
    timerAlarmDisable(scan_timer);
    //dmd.drawString(0, 0, "Disable Display", 15, GRAPHICS_NORMAL);


    Serial.println("Disconnecting current wifi connection");
    WiFi.disconnect();
    EEPROM.begin(512); //Initialasing EEPROM
    delay(10);
    pinMode(OPEN_HOT_SPOT, INPUT_PULLUP);
    pinMode(OPEN_HOT_SPOT, INPUT);
    Serial.println();
    Serial.println();
    Serial.println("Startup");

    //---------------------------------------- Read eeprom for ssid and pass
    Serial.println("Reading EEPROM ssid");


    for (int i = 0; i < 32; ++i)
    {
        esid += char(EEPROM.read(i));
    }
    Serial.println();
    Serial.print("SSID: ");
    Serial.println(esid);
    Serial.println("Reading EEPROM pass");

    for (int i = 32; i < 96; ++i)
    {
        epass += char(EEPROM.read(i));
    }
    Serial.print("PASS: ");
    Serial.println(epass);


    WiFi.begin(esid.c_str(), epass.c_str());
    //Connect_status = testWifi();

    if (testWifi() && digitalRead(OPEN_HOT_SPOT))
    {
        Connect_status = 1;
        Serial.println(" connection status positive");
        //return;
    }
    else
    {
        Serial.print("ESP Board MAC Address:  ");
        Serial.println(WiFi.macAddress());
        Connect_status = 0;
        Serial.println("Connection Status Negative / D15 HIGH");
        Serial.println("Turning the HotSpot On");
        launchWeb();
        setupAP();// Setup HotSpot
    }

    Serial.println();
    Serial.println("Waiting.");

    while ((WiFi.status() != WL_CONNECTED))
    {
        Serial.print(".");
        delay(100);
        server.handleClient();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Connect_status = 1;
    }

    Serial.print("RRSI: ");
    Serial.println(WiFi.RSSI());
    char rssi[] = { "RSSI:           " };

    itoa(WiFi.RSSI(), &rssi[8], 10);

    timerAlarmEnable(scan_timer);
    dmd.clearScreen(true);;
    dmd.drawString(0, 0, "WiFi Connected!", 15, GRAPHICS_NORMAL,1);
    dmd.drawString(0, 8, rssi, 16, GRAPHICS_NORMAL,1);
    delay(2000);
    //myLED.selectFont(SystemFont5x7_greek); //font1
    ////myLED.displayBrightness(100);
    ////myLED.normalMode();
    //Serial.begin(115200);

    // Configure LED output


    //// Configure the Prescaler at 80 the quarter of the ESP32 is cadence at 80Mhz
    //// 80000000 / 80 = 1000 tics / seconde
    //timer = timerBegin(0, 30, true);
    //timerAttachInterrupt(timer, &onTime, true);

    //// Sets an alarm to sound every second
    //timerAlarmWrite(timer, 10000, true);
    //timerAlarmEnable(timer);
    //delay(200);
    //myLED.test_buffer();
    Serial.println();
    Serial.print("ESP Board MAC Address:  ");
    Serial.println(WiFi.macAddress());
    String helpmac = WiFi.macAddress();
    mac_address[0] = helpmac[0]; mac_address[1] = helpmac[1];
    mac_address[2] = helpmac[3]; mac_address[3] = helpmac[4];
    mac_address[4] = helpmac[6]; mac_address[5] = helpmac[7];
    mac_address[6] = helpmac[9]; mac_address[7] = helpmac[10];
    mac_address[8] = helpmac[12]; mac_address[9] = helpmac[13];
    mac_address[10] = helpmac[15]; mac_address[11] = helpmac[16];
    mac_address[12] = 0;

    //-------------------------------------------------------------
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    //Europe/Athens	EET-2EEST,M3.5.0/3,M10.5.0/4
    setTimezone("EET-2EEST,M3.5.0/3,M10.5.0/4");
    printLocalTime();
    SetExtRTC();

    //helpmac.toCharArray(mac_address, 16, 0);
    Serial.println("Display the Start up Version:");
    Serial.println(Company);
    Serial.println(Device);
    Serial.println(Version);

    dmd.clearScreen(true);;
    dmd.drawString(0, 0, Company, sizeof(Company) - 1, GRAPHICS_NORMAL, 1);
    dmd.drawString(0, 8, Device, sizeof(Device) - 1, GRAPHICS_NORMAL, 1);
    delay(2000);

    dmd.clearScreen(true);
    dmd.drawString(0, 0, "Software Version", 16, GRAPHICS_NORMAL,1);
    dmd.drawString(0, 8, Version, sizeof(Version) - 1, GRAPHICS_NORMAL,1);
    delay(2000);
    
    dmd.clearScreen(true);
    dmd.drawString(0, 0, "Sign ID:", 8, GRAPHICS_NORMAL,1);
    dmd.drawString(0, 8, mac_address, sizeof(mac_address), GRAPHICS_NORMAL,1);
    delay(4000);
     
    dmd.selectFont(Verdana_Greek);
    
    Serial.println();
    Serial.println("Waiting.");

    while ((WiFi.status() != WL_CONNECTED))
    {
        Serial.print(".");
        delay(100);
        server.handleClient();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Connect_status = 1;
    }
}

void loop() {
    uint16_t center1;
    digitalWrite(LED_PIN, HIGH);
    //test_patern_slash();

    Serial.println("\nStarting connection to server...");
    client.setInsecure();//skip verification
    if (!client.connect(Host, 443)) {
        Serial.println("Connection failed!");
        //NotDisplay = 1;
    }
    else {
        Serial.println("Connected to server!");
        // Make a HTTP request:
        client.print("GET /djson?mac=");
        client.print(mac_address);
        client.print("&ver=");
        client.print(Version);
        client.println(" HTTP/1.1");
        client.println("Host: app.smart-q.eu");
        client.println("Connection: close");
        client.println();


        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                Serial.println("headers received");
                break;
            }
        }


        // if there are incoming bytes available
        // from the server, read them and print them:
        uint8_t startjson = 0;
        jsonBuffer = "";
        while (client.available()) {
            char c = client.read();
            //Serial.write(c);
            if (c == '{' || startjson) {
                jsonBuffer += c;
                startjson = 1;
            }

        }
        client.stop();

    }
    JSONVar myObject = JSON.parse(jsonBuffer);
    JSONVar value;
    // JSON.typeof(jsonVar) can be used to get the type of the var
    if (JSON.typeof(myObject) == "undefined") {
        Serial.println("Parsing input failed!");
        return;
    }
    /*Serial.print("enable: ");
    Serial.println(myObject["page1"]["enable"]);*/
    String page; String enable;
    int speed;
    int i;
    //------------------------------------------------------------------------------------------------------
        //String page1 = JSON.stringify(myObject["page1"]["text"]);    
    digitalWrite(LED_PIN, LOW);
    //Reading Settings
    Serial.println("Reading Settings:");
    Serial.print("Active: ");
    Serial.println(myObject["settings"]["active"]);
    Serial.print("Clock Display: ");
    Serial.println(myObject["settings"]["clockcheck"]);
    Serial.print("Date Display: ");
    Serial.println(myObject["settings"]["datecheck"]);
    Serial.print("Temperature: ");
    Serial.println(myObject["settings"]["tempcheck"]);
    Serial.print("Brightness: ");
    Serial.println(myObject["settings"]["brightness"]);
    Serial.println("--------------------------------------------");
    /*String brightness = myObject["settings"]["brightness"];
    dmd.setBrightness(brightness.toInt());*/
    //------------------------------------------------------------------------------------------------------
    page = myObject["settings"]["active"];
    if (page == "0") {
        Serial.print("Sign is inactive...");
        dmd.clearScreen(true);
        dmd.drawString(0, 0, "inactive", 8, GRAPHICS_NORMAL, 1);
        return;
    }
    //------------------------------------------------------------------------------------------------------
    page = myObject["page1"]["enable"];
    speed = myObject["page1"]["speed"];
    if (page == "1") {
        page = JSON.stringify(myObject["page1"]["text"]);
        int str_len = page.length() + 1;
        char* char_array = new char[str_len];
        page.toCharArray(char_array, str_len);
        remove_quotes(char_array);
        decodeUTF8(char_array);
        Serial.print("Printed page len: ");
        Serial.println(page_len);
        Serial.println(page);
        Serial.println("--------------------------------------------");
        dmd.clearScreen(true);
        center1 = get_center(page_buf, page_len, 1);
        if (center1 > PIXELS_X) {
            dmd.scrollingString(0, 0, page_buf, page_len, 1, speed);
            
        }
        else {
            dmd.drawString((PIXELS_X - center1) / 2, 0, page_buf, page_len, GRAPHICS_NORMAL, 1);
            delay(3000);
        }
        Serial.println();
        
    }

    page = myObject["page2"]["enable"];
    speed = myObject["page2"]["speed"];
    if (page == "1") {
        page = JSON.stringify(myObject["page2"]["text"]);
        int str_len = page.length() + 1;
        char* char_array = new char[str_len];
        page.toCharArray(char_array, str_len);
        remove_quotes(char_array);
        decodeUTF8(char_array);
        Serial.print("Printed page len: ");
        Serial.println(page_len);
        Serial.println(page);
        Serial.println("--------------------------------------------");
        dmd.clearScreen(true);
        center1 = get_center(page_buf, page_len, 1);

        if (center1 > PIXELS_X) {
            dmd.scrollingString(0, 0, page_buf, page_len, 1, speed);
        }
        else {
            dmd.drawString((PIXELS_X - center1) / 2, 0, page_buf, page_len, GRAPHICS_NORMAL, 1);
            delay(3000);
        }
        Serial.println();
        
    }

    page = myObject["page3"]["enable"];
    speed = myObject["page3"]["speed"];
    if (page == "1") {
        page = JSON.stringify(myObject["page3"]["text"]);
        int str_len = page.length() + 1;
        char* char_array = new char[str_len];
        page.toCharArray(char_array, str_len);
        remove_quotes(char_array);
        decodeUTF8(char_array);
        Serial.print("Printed page len: ");
        Serial.println(page_len);
        Serial.println(page);
        Serial.println("--------------------------------------------");
        dmd.clearScreen(true);
        center1 = get_center(page_buf, page_len, 1);
        if (center1 > PIXELS_X) {
            dmd.scrollingString(0, 0, page_buf, page_len, 1, speed);
        }
        else {
            dmd.drawString((PIXELS_X - center1) / 2, 0, page_buf, page_len, GRAPHICS_NORMAL, 1);
        }
        Serial.println();
        delay(3000);
    }
    printLocalTime();
    //------------------------------------------------------------------------------------------------------
    page = myObject["settings"]["clockcheck"];
    if (page == "1") {
        center1 = get_center(timedisplay, sizeof(timedisplay), 1);
        dmd.clearScreen(true);
        dmd.drawString((PIXELS_X - center1) / 2, 0, timedisplay, sizeof(timedisplay), GRAPHICS_NORMAL, 1);
        delay(1000);
        printLocalTime();
        dmd.clearScreen(true);
        dmd.drawString((PIXELS_X - center1) / 2, 0, timedisplay, sizeof(timedisplay), GRAPHICS_NORMAL, 1);
        delay(1000);
        printLocalTime();
        dmd.clearScreen(true);
        dmd.drawString((PIXELS_X - center1) / 2, 0, timedisplay, sizeof(timedisplay), GRAPHICS_NORMAL, 1);
        delay(1000);
    }
    //---------------------------------------------------------------------------------------
    page = myObject["settings"]["datecheck"];
    if (page == "1") {
        center1 = get_center(datedisplay, sizeof(datedisplay), 1);
        dmd.clearScreen(true);
        dmd.drawString((PIXELS_X - center1) / 2, 0, datedisplay, sizeof(datedisplay), GRAPHICS_NORMAL, 1);
        delay(3000);
    }

    page = myObject["settings"]["tempcheck"];
    if (page == "1") {
        sensors.requestTemperatures();
        float temperatureC = sensors.getTempCByIndex(0);
        Serial.print("Temperature: ");
        Serial.print(temperatureC);
        Serial.println(" *C");
        char temp[8];
        dtostrf(temperatureC, 4, 1, temp);
        const char degreeStr[3] = { 176,'C',0 };
        strcat(temp, degreeStr);
        center1 = get_center(temp, sizeof(temp), 1);
        dmd.clearScreen(true);
        dmd.drawString((PIXELS_X - center1) / 2, 0, temp, sizeof(temp), GRAPHICS_NORMAL, 1);
        delay(3000);
    }
}
//........................................................................................

uint16_t get_center(String arr, uint8_t len, uint8_t space)
{
    uint16_t Legth_page = 0;
    uint8_t count = 0;

    for (size_t i = 0; i < len; i++)
    {
        if (arr[i] == 0)
            break;
        count++;
        Legth_page += dmd.charWidth(arr[i]) + space;
    }
    //Serial.print("Characters: "); Serial.println(count);
    return Legth_page;
}


void printLocalTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }

    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

    char timehelp[3];
    strftime(timehelp, 3, "%H", &timeinfo);
    timedisplay[0] = timehelp[0]; timedisplay[1] = timehelp[1]; timedisplay[2] = ':';
    strftime(timehelp, 3, "%M", &timeinfo);
    timedisplay[3] = timehelp[0]; timedisplay[4] = timehelp[1]; timedisplay[5] = ':';
    strftime(timehelp, 3, "%S", &timeinfo);
    timedisplay[6] = timehelp[0]; timedisplay[7] = timehelp[1];

    strftime(timehelp, 3, "%d", &timeinfo);
    datedisplay[0] = timehelp[0]; datedisplay[1] = timehelp[1]; datedisplay[2] = '/';
    strftime(timehelp, 3, "%m", &timeinfo);
    datedisplay[3] = timehelp[0]; datedisplay[4] = timehelp[1]; 
    if (DISPLAYS_ACROSS < 3)
    {
        datedisplay[5] = 0;
    }
    else
    {
        datedisplay[5] = '/';
        strftime(timehelp, 3, "%y", &timeinfo);
        datedisplay[6] = timehelp[0]; datedisplay[7] = timehelp[1];
    }
}

//----------------------------------------------- Fuctions used for WiFi credentials saving and connecting to it which you do not need to change
bool testWifi(void)
{
    int c = 0;
    Serial.println("Waiting for Wifi to connect");
    while (c < 20) {
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("");
            return true;
        }
        delay(500);
        Serial.print("*");
        c++;
    }
    Serial.println("");
    Serial.println("Connect timed out, opening AP");
    return false;
}

void launchWeb()
{
    Serial.println("");
    if (WiFi.status() == WL_CONNECTED)
        Serial.println("WiFi connected");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("SoftAP IP: ");
    Serial.println(WiFi.softAPIP());
    createWebServer();
    // Start the server
    server.begin();
    Serial.println("Server started");
}

void setupAP(void)
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0)
        Serial.println("no networks found");
    else
    {
        Serial.print(n);
        Serial.println(" networks found");
        for (int i = 0; i < n; ++i)
        {
            // Print SSID and RSSI for each network found
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.print(WiFi.SSID(i));
            Serial.print(" (");
            Serial.print(WiFi.RSSI(i));
            Serial.print(")");
            //Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
            delay(10);
        }
    }
    Serial.println("");
    st = "<ol>";
    for (int i = 0; i < n; ++i)
    {
        // Print SSID and RSSI for each network found
        st += "<li>";
        st += WiFi.SSID(i);
        st += " (";
        st += WiFi.RSSI(i);

        st += ")";
        //st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
        st += "</li>";
    }
    st += "</ol>";
    delay(100);
    WiFi.softAP("Rousis_ESP32", "");
    Serial.println("Initializing_softap_for_wifi credentials_modification");
    launchWeb();
    Serial.println("over");
}

void createWebServer()
{
    {
        server.on("/", []() {

            IPAddress ip = WiFi.softAPIP();
            String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
            content = "<!DOCTYPE HTML>\r\n<html>Welcome to ROUSIS_Wifi Credentials Update page";
            content += "<form action=\"/scan\" method=\"POST\"><input type=\"submit\" value=\"scan\"></form>";
            content += ipStr;
            content += "<p>";
            content += st;
            content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
            content += "</html>";
            server.send(200, "text/html", content);
        });
        server.on("/scan", []() {
            //setupAP();
            IPAddress ip = WiFi.softAPIP();
            String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

            content = "<!DOCTYPE HTML>\r\n<html>go back";
            server.send(200, "text/html", content);
        });

        server.on("/setting", []() {
            String qsid = server.arg("ssid");
            String qpass = server.arg("pass");
            if (qsid.length() > 0 && qpass.length() > 0) {
                Serial.println("clearing eeprom");
                for (int i = 0; i < 96; ++i) {
                    EEPROM.write(i, 0);
                }
                Serial.println(qsid);
                Serial.println("");
                Serial.println(qpass);
                Serial.println("");

                Serial.println("writing eeprom ssid:");
                for (int i = 0; i < qsid.length(); ++i)
                {
                    EEPROM.write(i, qsid[i]);
                    Serial.print("Wrote: ");
                    Serial.println(qsid[i]);
                }
                Serial.println("writing eeprom pass:");
                for (int i = 0; i < qpass.length(); ++i)
                {
                    EEPROM.write(32 + i, qpass[i]);
                    Serial.print("Wrote: ");
                    Serial.println(qpass[i]);
                }
                EEPROM.commit();

                content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
                statusCode = 200;
                ESP.restart();
            }
            else {
                content = "{\"Error\":\"404 not found\"}";
                statusCode = 404;
                Serial.println("Sending 404");
            }
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(statusCode, "application/json", content);

        });
    }
}

void decodeUTF8(char str[]) {
    page_buf[0] = 0; page_len = 0;
    unsigned int a = 0;
    char get_C = 0xff;
    size_t i = 0;
    while (get_C != 0)
    {
        get_C = str[i];
        if (get_C == 0xCE) { //Greek Windows-1253 or ISO/IEC 8859-7 -> UTF-8
            i++;
            get_C = str[i];
            page_buf[a++] = (get_C + 0x30);
        }
        else if (get_C == 0xCF) { //Greek Windows-1253 or ISO/IEC 8859-7 -> UTF-8
            i++;
            get_C = str[i];
            page_buf[a++] = (get_C + 0x70);
        }
        else {
            page_buf[a++] = get_C;
        }
        i++;
        page_len++;
    }
    page_buf[a++] = 0;
    page_len--;
    Serial.println("/");
}

void SetExtRTC() {

    Serial.println("Setting the external RTC");
    setTimezone("GMT0");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        get_time_externalRTC();
        return;
    }
    setTimezone("EET-2EEST,M3.5.0/3,M10.5.0/4");
    /*
        Serial.print("Time to set DS1302: ");
        Serial.print(timeinfo.tm_year); Serial.print(" ");
        Serial.print(timeinfo.tm_mon); Serial.print(" ");
        Serial.print(timeinfo.tm_mday); Serial.print(" ");
        Serial.print(timeinfo.tm_hour); Serial.print(" ");
        Serial.print(timeinfo.tm_min); Serial.print(" ");
        Serial.println(timeinfo.tm_sec);
        Serial.println("--------------------------------------");*/

    Ds1302::DateTime dt = {
           .year = timeinfo.tm_year,
           .month = timeinfo.tm_mon + 1,
           .day = timeinfo.tm_mday,
           .hour = timeinfo.tm_hour,
           .minute = timeinfo.tm_min,
           .second = timeinfo.tm_sec,
           .dow = timeinfo.tm_wday
    };
    rtc.setDateTime(&dt);

    Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
    Serial.println("----------------------------------------");
}

void get_time_externalRTC() {
    // get the current time
    Ds1302::DateTime now;
    rtc.getDateTime(&now);

    Serial.println();
    Serial.println("Read and set the external Time from DS1302:");

    Serial.print("20");
    Serial.print(now.year);    // 00-99
    Serial.print('-');
    if (now.month < 10) Serial.print('0');
    Serial.print(now.month);   // 01-12
    Serial.print('-');
    if (now.day < 10) Serial.print('0');
    Serial.print(now.day);     // 01-31
    Serial.print(' ');
    Serial.print(now.dow); // 1-7
    Serial.print(' ');
    if (now.hour < 10) Serial.print('0');
    Serial.print(now.hour);    // 00-23
    Serial.print(':');
    if (now.minute < 10) Serial.print('0');
    Serial.print(now.minute);  // 00-59
    Serial.print(':');
    if (now.second < 10) Serial.print('0');
    Serial.print(now.second);  // 00-59
    Serial.println();
    Serial.println("---------------------------------------------");

    setTimezone("GMT0");
    setTime(now.year + 2000, now.month, now.day, now.hour, now.minute, now.second, 1);
    setTimezone("EET-2EEST,M3.5.0/3,M10.5.0/4");
}

void setTime(int yr, int month, int mday, int hr, int minute, int sec, int isDst) {
    struct tm tm;

    tm.tm_year = yr - 1900;   // Set date
    tm.tm_mon = month - 1;
    tm.tm_mday = mday;
    tm.tm_hour = hr;      // Set time
    tm.tm_min = minute;
    tm.tm_sec = sec;
    tm.tm_isdst = isDaylightSavingTime(&tm);  // 1 or 0
    time_t t = mktime(&tm);
    Serial.printf("Setting time: %s", asctime(&tm));
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, NULL);
}

bool isDaylightSavingTime(struct tm* timeinfo) {
    // Implement your logic to check if DST is in effect based on your timezone's rules
    // For example, you might check if the current month and day fall within the DST period.

    int month = timeinfo->tm_mon + 1; // Month is 0-11, so add 1
    int day = timeinfo->tm_mday;

    // Add your DST logic here
    // For example, assuming DST is from April to October
    if ((month > 3) && (month < 11)) {
        return true;
    }

    return false;
}