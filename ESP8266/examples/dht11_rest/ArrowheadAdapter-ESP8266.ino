//generic ESP8266 libs
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <WiFiUdp.h>

#include "DHTesp.h"
#include <NTPClient.h>

//json parser library: https://arduinojson.org
//NOTE: using version 5.13.1, since 6.x is in beta (but the Arduino IDE updates it!)
//Make sure you are using 5.13.x within the Package Manager!
#include <ArduinoJson.h>

//https://github.com/me-no-dev/ESPAsyncTCP
#include <ESPAsyncTCP.h>
//HTTP server library: https://github.com/me-no-dev/ESPAsyncWebServer
#include <ESPAsyncWebServer.h>

//NTP
const long utcOffsetInSeconds = 3600;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "time1.google.com", utcOffsetInSeconds, 60000);

//TODO: change params, if needed!
#define SERVER_PORT 8454 
#define serviceVersion 1
 char* serviceURI = "temperature";
 char* ssid = "Arrowhead-RasPi-IoT8";
 char* password = "arrowhead";

//TODO: modify these accordingly, and add additional service metadata at line 94 if needed!
 char* serviceRegistry_addr = "http://arrowhead.tmit.bme.hu:8342";//"http://arrowhead.tmit.bme.hu:8442";
 char* serviceDefinition = "IndoorTemperature";
 char* systemName = "InsecureTemperatureSensor";

//async webserver instance
AsyncWebServer server(SERVER_PORT);

float temperature = 0.0;
double dTemp = 0.0;
String tempval = (String)temperature;

DHTesp dht;

unsigned long epochTime = 0;

//Use the following lines for a fix IP-address
//IPAddress ip(192, 168, 43, 5); 
//IPAddress gateway(192, 168, 43, 1);
//IPAddress subnet(255, 255, 255, 0);

void setup() {
  //debug log on serial
  Serial.begin(115200);
  delay(1000);   //Delay needed before calling the WiFi.begin
  Serial.println("Startup.");
  WiFi.config(ip,gateway,subnet,gateway);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { //Check for the connection
        delay(1000);
        Serial.print("Connecting to WiFi SSID: ");
        Serial.println(ssid);
  }
  Serial.print("Connected, my IP is: ");
  Serial.println(WiFi.localIP());

  //building ServiceRegistryEntry on my own
  StaticJsonBuffer<500> SREntry;
  JsonObject& root = SREntry.createObject();
  JsonObject& providedService = root.createNestedObject("providedService");
  providedService["serviceDefinition"] = serviceDefinition;
  JsonObject& metadata = providedService.createNestedObject("metadata");
  JsonArray& interfaces = providedService.createNestedArray("interfaces");
  interfaces.add("json");

  //TODO: add service metadata if you need!
  metadata["unit"]="celsius";

  root.createNestedObject("provider");
  root["provider"]["systemName"] = systemName;
  root["provider"]["address"] = WiFi.localIP().toString();
  root["provider"]["port"] =SERVER_PORT;
  root["port"] =SERVER_PORT;
  root["serviceURI"] = serviceURI;
  root["version"] =serviceVersion;

  String SRentry;
  root.prettyPrintTo(SRentry);

  //registering myself in the ServiceRegistry
  HTTPClient http_sr;
  http_sr.begin(String(serviceRegistry_addr) + "/serviceregistry/register"); //Specify destination for HTTP request
  http_sr.addHeader("Content-Type", "application/json"); //Specify content-type header
  int httpResponseCode_sr = http_sr.POST(String(SRentry)); //Send the actual POST request
  
  if (httpResponseCode_sr<0) {
    Serial.println("ServiceRegistry is not available!");
    http_sr.end();  //Free resources
  } else {
    Serial.print("Registered to SR with status code:");
    Serial.println(httpResponseCode_sr);
    String response_sr = http_sr.getString();                       //Get the response to the request
    http_sr.end();  //Free resources
    if (httpResponseCode_sr!=HTTP_CODE_CREATED) { //SR responded properly, check if registration was successful
        Serial.println("Service registration failed with response:");
        Serial.println(response_sr);           //Print request answer
  
        //need to remove our previous entry and then re-register
        HTTPClient http_remove;
        http_remove.begin(String(serviceRegistry_addr) + "/serviceregistry/remove"); //Specify destination for HTTP request
        http_remove.addHeader("Content-Type", "application/json"); //Specify content-type header
        int httpResponseCode_remove = http_remove.PUT(String(SRentry)); //Send the actual PUT request
        Serial.print("Removed previous entry with status code:");
        Serial.println(httpResponseCode_remove);
        String response_remove = http_remove.getString();                       //Get the response to the request
        Serial.println(response_remove);           //Print request answer
        http_remove.end();  //Free resources
  
        HTTPClient http_renew;
        http_renew.begin(String(serviceRegistry_addr) + "/serviceregistry/register"); //Specify destination for HTTP request
        http_renew.addHeader("Content-Type", "application/json"); //Specify content-type header
        int httpResponseCode_renew = http_renew.POST(String(SRentry)); //Send the actual POST request
        Serial.print("Re-registered with status code:");
        Serial.println(httpResponseCode_renew);
        String response_renew = http_renew.getString();                       //Get the response to the request
        Serial.println(response_renew);           //Print request answer
        http_renew.end();  //Free resources
      }    
    } // if-else: SR is online and its responses were valid

    //waiting here a little for no reason, can be deleted!
    delay(1000);

  Serial.println("Starting server!");

  //adding a '/' in front of the serviceURI!
  char path[30];
  String pathString = String("/") + String(serviceURI);
  pathString.toCharArray(path, 20);

  //starting the webserver
  server.on(path, HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Received HTTP request, responding with:");

    //build the SenML format

    StaticJsonBuffer<500> doc;
    JsonObject& root = doc.createObject();
    root["bn"]  = systemName;
    root["bt"]  = epochTime;
    root["bu"]  = "celsius";
    root["ver"] = 1;
    JsonArray& e = root.createNestedArray("e");
    JsonObject& meas = e.createNestedObject();
    meas["n"] = systemName;
    meas["v"] = tempval;
    meas["t"] = epochTime;

    String response;
    root.prettyPrintTo(response);
    request->send(200, "application/json", response);
    Serial.println(response);
  });
  server.begin();

  timeClient.begin();

  dht.setup(2, DHTesp::DHT11); // Connect DHT sensor to GPIO1

  //prepare LED pin
  //pinMode(LED_BUILTIN, OUTPUT);
}

//optionally blink 1Hz if everything is set up, provider is running
//int ledStatus = 0;

void loop(){
  delay(dht.getMinimumSamplingPeriod());
  
  while(true){
      temperature = dht.getTemperature();
      if(!isnan(temperature))
        break;
  }
  
  tempval = (String)temperature;

  timeClient.update();
  epochTime = timeClient.getEpochTime();
  
  /*
  if (ledStatus) {
    digitalWrite(LED_BUILTIN, HIGH);
    ledStatus =0;
  }
  else {
     digitalWrite(LED_BUILTIN, LOW);
     ledStatus = 1;
  }
  */
  
  delay(1000);
}
