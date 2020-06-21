//#define LIGHTWEIGHT 1
//#define DEBUG_MODE 1

#include <ESP8266WiFi.h>
#include <aREST.h>
#include <ESP8266HTTPClient.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>

//aREST setup
aREST rest = aREST(); 
WiFiServer server(80);


//Settings
const int hold_count = 10;
const int hold_press_time = 500;
const int set_keep_alive_time =  300000;
const int power_button_delay = 1000;
const int set_cooldown_time = 5000;
const int silentOnTime = 1000;
const unsigned long restart_interval = 43000000;

//Define Pins
//On Boards LEDs
const int RED_LED = 16;
const int BLUE_LED = 2;

//Function Pins
//IR LED Pin
const int IR_LED = 14;
//IR Receiver Data Pin
const int RECV_PIN = 5;
//Green Debug LED
const int GREEN_LED = 9;
//Pin connected to PSU molex (Via optocoupler)
const int FAN = 10;
//Pin connected to Motherboard power pins (Via optocoupler)
const int POWER = 4;

//Define things
const String TV_IP = "192.168.1.77";
const String tv_auth_key = "1111";

//Define IRCC Codes
const String power_on = "AAAAAQAAAAEAAAAuAw==";
const String input_hdmi2 = "AAAAAgAAABoAAABbAw==";
const String power_off = "AAAAAQAAAAEAAAAvAw==";
const String youtube_app = "AAAAAgAAAMQAAABHAw==";

//Define WiFi Credentials
const char* ssid = "SKY506F5";
const char* password = "PBNMCNXVMN";

//KeyCodes
//rec
const int sony_red = 21225;
const int sony_blue = 4841;
const int sony_yellow = 29417;
const int sony_green = 13033;
const int sony_record = 745;
const int sony_vol_up = 1168;
const int sony_vol_down = 3216;
const int sony_mute = 656;
const int sony_google_play = 12579;


//send
const int LG_stop = 0x3434A05F;
const int LG_power_toggle = 0x34347887;
const int LG_vol_up = 0x3434E817;
const int LG_vol_down = 0x34346897; 
const int LG_mute = 0x3434F807;

//Initiate Variables
//holds the time the first in a series of red button presses is received, to work out if it is held down.
unsigned long first_pressed = 0;

//holds the time of silent on command so the change in fan status does not turn on the TV
unsigned long silentOnMillis = 0;

//Counts the number of times the red button signal has been received (i.e. held down for)
int button_presses = 0;

//detects current status of the fan power (via molex)
int current_fan_status;
//value to determine if fan/PC power status has changed
int prev_fan_status;

//creates uint64_t variable to hold the results from the IR receiver value
uint64_t result;
//creates uint64_t variable to hold the results from the IR receiver value between loop cycles
uint64_t prev_result = 0;

//Stores the times of the last keep alive signal
unsigned long keep_alive_time = 0;

//button cooldown is applied to
uint64_t cooldown_button;
unsigned long cooldown_time = 0;


//aREST function Setup
int silentOn(String command) {
  Serial.println("silent on");
    if(prev_fan_status == LOW){
    PC_power_button();
    silentOnMillis = millis();
  }
}

int silentOff(String command) {  
  Serial.println("silent off");
if(prev_fan_status == HIGH){
    PC_power_button();
    if(getTVStatus()){
      sendIRCC(power_off);
    }
  }
}

//Create Objects
//class declaration for receiver object
IRrecv irrecv(RECV_PIN);
//class declaration for results returned from irrec
decode_results results;
//IR SEND OBJECT
IRsend irsend(IR_LED);

void setup() {
  //establish aREST variables and functions
  rest.variable("status",&current_fan_status);
  rest.function("on", silentOn);
  rest.function("off", silentOff);

  
  pinMode(POWER, OUTPUT);
  pinMode(FAN, INPUT);
  pinMode(RECV_PIN, INPUT);
  pinMode(RED_LED, OUTPUT);
  
  digitalWrite(RED_LED, LOW);
  digitalWrite(POWER, LOW);
  digitalWrite(GREEN_LED, LOW);

  Serial.begin(115200);

  //Start the IR receiver and sender objects
  irrecv.enableIRIn();

  irsend.begin();

  //Connect to Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  
//begin aREST server
 server.begin();

  digitalWrite(RED_LED, HIGH);

  Serial.println();
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  prev_fan_status = digitalRead(FAN);
  current_fan_status = digitalRead(FAN);
}


//shorts power button (i.e. push)
void PC_power_button() {
  Serial.println("power button pressed");
  digitalWrite(POWER, HIGH);
  delay(power_button_delay);
  digitalWrite(POWER, LOW);
}

//Sends the IRCC_code as HTTP post
int sendIRCC(String IRCC_code) {
  Serial.println("sending HTTP request:");
  Serial.println(IRCC_code);
  //Declare object of class HTTPClient
  HTTPClient http;

  //Address POST sent to (replace %s with TV_IP)
  String ircc_post_address = "http://%s/sony/IRCC";
  ircc_post_address.replace("%s", TV_IP);

  //begin http communication
  http.begin(ircc_post_address);

  //Specify content-type headers
  http.addHeader("X-Auth-PSK", tv_auth_key);
  http.addHeader("SOAPAction", "\"urn:schemas-sony-com:service:IRCC:1#X_SendIRCC\"");

  //POST Payload (replace %s with IRCC_code)
  String ircc_post_body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:X_SendIRCC xmlns:u=\"urn:schemas-sony-com:service:IRCC:1\"><IRCCCode>%s</IRCCCode></u:X_SendIRCC></s:Body></s:Envelope>";
  ircc_post_body.replace("%s", IRCC_code);
  //Send the request and store returned code
  int httpCode = http.POST(ircc_post_body);

  //For Debugging
  String return_payload = http.getString();
  Serial.println("http return code:");
  Serial.println(httpCode);//Print HTTP return code
  Serial.println("http response payload:");
  Serial.println(return_payload);

  http.end();//Close connection
  return httpCode;
}


//returns true if TV is set to HDMI2
bool getTVStatus() {
  Serial.println("getting TV power status");
  //variable to hold booleon of weather HDMI2 is the active input
  bool hdmi2;

  //Declare object of class HTTPClient
  HTTPClient http;
  //Address POST sent to (replace %s with TV_IP)
  String status_post_address = "http://%s/sony/avContent";
  status_post_address.replace("%s", TV_IP);

  //begin http communication
  http.begin(status_post_address);

  http.addHeader("X-Auth-PSK", tv_auth_key);

  //Send the request and store returned code
  int httpCode = http.POST("{\"method\": \"getPlayingContentInfo\", \"params\": [], \"id\": 1, \"version\": \"1.0\"}");

  String return_payload = http.getString();

  //For debugging
  Serial.println("http return code:");
  Serial.println(httpCode);//Print HTTP return code
  Serial.println("http response payload:");
  Serial.println(return_payload);


  //Close connection
  http.end();

  //checks if "HDMI 2" appears in return string
  if (return_payload.indexOf("\"HDMI 2\"") != (-1)) {
    hdmi2 = true;
  }
  else {
    hdmi2 = false;
  }

  return hdmi2;
}

void resest_hold_variables() {
  button_presses = 0;
  first_pressed = 0;
  
}


void sony_red_hold() {
  Serial.println("red_held");
  if(prev_fan_status == LOW){
    PC_power_button();
  }
  sendIRCC(power_on);
  sendIRCC(input_hdmi2);
}

void sony_blue_hold() {
  Serial.println("blue held");
if(prev_fan_status == HIGH){
    PC_power_button();
    if(getTVStatus()){
      sendIRCC(power_off);
    }
  }
}

void sony_yellow_hold() {
  Serial.println("yellow held");
}

void sony_green_hold() {
  Serial.println("green held");
}
  

void loop() {
	
  //Set WiFi Status LED
    if (WiFi.status() != WL_CONNECTED) {
      digitalWrite(RED_LED, LOW);
    }
	else{
		digitalWrite(RED_LED, HIGH);
	}
	

  //If its been more than 5 minutes since the last keep alive IR signal was sent, send another and reset the keep alive time to current time
  if ((millis() - keep_alive_time) > set_keep_alive_time) {
    irsend.sendSAMSUNG(LG_stop, 32);
    keep_alive_time = millis();
  }



  //check if fan status has changed
  current_fan_status = digitalRead(FAN);

  //check if silent on is activated and if it is over the set time. if so, reset.
  if (((millis() - silentOnMillis) > silentOnTime) && (silentOnMillis != 0)){
    Serial.println("silent on timer reset");
      Serial.println("heapsize:");
      Serial.println(ESP.getFreeHeap());
    silentOnMillis = 0;
  }

  if ((current_fan_status == LOW) && (prev_fan_status == HIGH)) {
    Serial.println("Fans high to low");
      Serial.println(silentOnMillis);
    if(getTVStatus()){
      sendIRCC(power_off);   
    }
  }
  
  else if ((current_fan_status == HIGH) && (prev_fan_status == LOW) && (silentOnMillis == 0)) {
      Serial.println("Fans low to high");
        Serial.println(silentOnMillis);
      sendIRCC(power_on);
      sendIRCC(input_hdmi2);
  }
  
  prev_fan_status = current_fan_status;


  //looks at the memory location where IR results variable is stored.
  if (irrecv.decode(&results)) {
    result = (results.value);
    irrecv.resume();
    //if Sony record button, send standby toggle to Soundbar
    if (result == sony_record) {
      Serial.println("record button pressed");
      irsend.sendSAMSUNG(LG_power_toggle, 32);
    }

	//pass thorugh sony volume controls to Soundbar
	else if (result == sony_vol_up){
		Serial.println("vol up pressed");
		irsend.sendSAMSUNG(LG_vol_up, 32);
	}
		
	else if (result == sony_vol_down){
		Serial.println("vol down pressed");
		irsend.sendSAMSUNG(LG_vol_down, 32);
	}	
	
	//Google play button launches youtube
	else if (result == sony_google_play){
		Serial.println("google play button pressed");
		sendIRCC(youtube_app);
	}
	
	//Colour buttons held
    else if((result == prev_result) && (result != cooldown_button)){
      button_presses += 1;

      if (first_pressed == 0){
        first_pressed = millis();
      }
      else if((millis() - first_pressed) > hold_press_time){
        resest_hold_variables();
      }
      else if (button_presses > hold_count){
        resest_hold_variables();
        
        cooldown_button = result;
        cooldown_time = millis();

        if(result == sony_red){
          sony_red_hold();
        }
        else if(result == sony_blue){
          sony_blue_hold();
        }
        else if(result == sony_yellow){
          sony_yellow_hold();
        }
        else if(result == sony_green){
          sony_green_hold();
        }
      }        
    }
    else{
      button_presses += 1;    
    }
    prev_result = result;

  }

if(cooldown_time != 0){
  if(millis() - cooldown_time > set_cooldown_time){
    cooldown_time = 0;
    cooldown_button = 0;
  }
}



if(millis() > restart_interval){
  Serial.println("restarting");
  ESP.restart();
}

  //aREST Section

//  WiFiClient client = server.available();
//  if (client) {
// 
//    while(!client.available()){
//      delay(5);
//    }
//    rest.handle(client);
//  }

//  WiFiClient client = server.available();
//  if (!client) {
//    return;
//  }
//  while(!client.available()){
//    delay(1);
//  }
//  rest.handle(client);

  // Handle aREST calls
  WiFiClient client = server.available();
  if (!client) {
    return;
   }
   Serial.println("Client Connected. Waiting for Data:");

  while (client.connected()) {
   if (client.available()) {
      rest.handle(client);
      client.stop();
    } // if (client.available())
  } // while (client.connected())


}
